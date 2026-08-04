/*
 * elfld.c - alternative ELF based program loader
 *
 * Copyright (C) 2024 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * This is a second executable loader, selected by Pexec() from the file's
 * magic number, that sits alongside the classic GEMDOS PRG loader in
 * kpgmld.c.  It accepts two flavours of statically linked ELF, both of
 * which load a program at an arbitrary base in the single, MMU-less pTOS
 * address space:
 *
 *  - ET_EXEC linked with "ld --emit-relocs" (ld -q): an ordinary fixed
 *    base executable that keeps its relocation entries instead of having
 *    them consumed at link time.  With -mword-relocations the absolute
 *    slots are plain R_ARM_ABS32 / R_68K_32 words.
 *
 *  - ET_DYN, i.e. a position independent executable (PIE, "ld -pie
 *    --no-dynamic-linker"): its only load-time fixups are the additive
 *    R_*_RELATIVE dynamic relocations in .rel.dyn, and a program that
 *    embeds no absolute pointers in its initialised data needs none at all.
 *
 * In both cases the loader applies "actual_load_addr - link_base" to every
 * absolute slot, exactly the additive fixup the PRG loader does with its
 * own byte stream relocation table (see kpgmld.c:pgfix01()).  Relocation
 * information is read from the section headers, so the binary must not be
 * stripped of them.
 *
 * ARM needs this because it cannot produce the m68k specific PRG format;
 * m68k can use it too as a normal alternative to PRG.
 */

/* #define ENABLE_KDEBUG */

#include "config.h"

#if CONF_WITH_ELF_LOADER

#include "portab.h"
#include "endian.h"
#include "fs.h"
#include "proc.h"
#include "gemerror.h"
#include "pghdr.h"
#include "string.h"
#include "kprint.h"

/*
 * minimal ELF32 definitions (see the System V ABI).  All fields use the
 * portab.h fixed width types so that struct layouts match the on-disk
 * format on both 16-bit-int m68k and 32-bit-int ARM.  Only native endian
 * binaries are accepted (see elf_check_ehdr()), so the fields can be read
 * straight into these structures.
 */

#define EI_NIDENT   16
#define EI_MAG0     0
#define EI_MAG1     1
#define EI_MAG2     2
#define EI_MAG3     3
#define EI_CLASS    4
#define EI_DATA     5

#define ELFMAG0     0x7f
#define ELFMAG1     'E'
#define ELFMAG2     'L'
#define ELFMAG3     'F'

#define ELFCLASS32  1
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

#define ET_EXEC     2
#define ET_DYN      3       /* position independent executable (PIE) */
#define PT_LOAD     1
#define SHT_RELA    4
#define SHT_REL     9

/* machine type and the "add the load bias to a 32-bit word" relocation
 * type for the architecture we are built for. */
#if ARCH_ARM
#define ELF_EM_EXPECTED 40      /* EM_ARM */
#define ELF_R_DIR32     2       /* R_ARM_ABS32 */
#define ELF_R_RELATIVE  23      /* R_ARM_RELATIVE */
#else
#define ELF_EM_EXPECTED 4       /* EM_68K */
#define ELF_R_DIR32     1       /* R_68K_32 */
#define ELF_R_RELATIVE  22      /* R_68K_RELATIVE */
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
#define ELF_DATA_EXPECTED ELFDATA2LSB
#else
#define ELF_DATA_EXPECTED ELFDATA2MSB
#endif

#define ELF32_R_TYPE(i) ((UBYTE)(i))

typedef struct {
    UBYTE   e_ident[EI_NIDENT];
    UWORD   e_type;
    UWORD   e_machine;
    ULONG   e_version;
    ULONG   e_entry;
    ULONG   e_phoff;
    ULONG   e_shoff;
    ULONG   e_flags;
    UWORD   e_ehsize;
    UWORD   e_phentsize;
    UWORD   e_phnum;
    UWORD   e_shentsize;
    UWORD   e_shnum;
    UWORD   e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    ULONG   p_type;
    ULONG   p_offset;
    ULONG   p_vaddr;
    ULONG   p_paddr;
    ULONG   p_filesz;
    ULONG   p_memsz;
    ULONG   p_flags;
    ULONG   p_align;
} Elf32_Phdr;

typedef struct {
    ULONG   sh_name;
    ULONG   sh_type;
    ULONG   sh_flags;
    ULONG   sh_addr;
    ULONG   sh_offset;
    ULONG   sh_size;
    ULONG   sh_link;
    ULONG   sh_info;
    ULONG   sh_addralign;
    ULONG   sh_entsize;
} Elf32_Shdr;

typedef struct {
    ULONG   r_offset;
    ULONG   r_info;
} Elf32_Rel;

typedef struct {
    ULONG   r_offset;
    ULONG   r_info;
    ULONG   r_addend;
} Elf32_Rela;

/*
 * summary of what a program needs in memory, computed once from the
 * program headers and shared by the sizing pass (elf_pgmhdrld) and the
 * load pass (elf_pgmld).
 */
typedef struct {
    ULONG   link_base;      /* lowest p_vaddr of any PT_LOAD segment */
    ULONG   file_end;       /* highest p_vaddr + p_filesz            */
    ULONG   mem_end;        /* highest p_vaddr + p_memsz             */
} ELFINFO;

/* read a fixed size structure from an absolute file offset */
static LONG read_at(FH h, ULONG offset, void *buf, LONG len)
{
    LONG r;

    r = xlseek((LONG)offset, h, 0);
    if (r < 0L)
        return r;

    r = xread(h, len, buf);
    if (r < 0L)
        return r;
    if (r != len)
        return EPLFMT;

    return 0;
}

/* validate the ELF header and make sure it targets this machine */
static LONG elf_check_ehdr(const Elf32_Ehdr *e)
{
    if (e->e_ident[EI_MAG0] != ELFMAG0 || e->e_ident[EI_MAG1] != ELFMAG1
     || e->e_ident[EI_MAG2] != ELFMAG2 || e->e_ident[EI_MAG3] != ELFMAG3)
        return EPLFMT;

    if (e->e_ident[EI_CLASS] != ELFCLASS32)
        return EPLFMT;

    if (e->e_ident[EI_DATA] != ELF_DATA_EXPECTED)
        return EPLFMT;

    if (e->e_type != ET_EXEC && e->e_type != ET_DYN)
        return EPLFMT;

    if (e->e_machine != ELF_EM_EXPECTED)
        return EPLFMT;

    if (e->e_phnum == 0 || e->e_phentsize < (UWORD)sizeof(Elf32_Phdr))
        return EPLFMT;

    return 0;
}

/*
 * scan the program headers, filling ELFINFO with the load-time extents.
 * Returns 0 on success or a negative GEMDOS error.
 */
static LONG elf_scan(FH h, const Elf32_Ehdr *e, ELFINFO *info)
{
    Elf32_Phdr ph;
    ULONG seg_end;
    LONG r;
    UWORD i;
    BOOL seen = FALSE;

    info->link_base = 0;
    info->file_end = 0;
    info->mem_end = 0;

    for (i = 0; i < e->e_phnum; i++)
    {
        r = read_at(h, e->e_phoff + (ULONG)i * e->e_phentsize,
                    &ph, (LONG)sizeof(ph));
        if (r < 0L)
            return r;

        if (ph.p_type != PT_LOAD)
            continue;

        if (ph.p_filesz > ph.p_memsz)
            return EPLFMT;

        if (!seen || ph.p_vaddr < info->link_base)
            info->link_base = ph.p_vaddr;

        seg_end = ph.p_vaddr + ph.p_filesz;
        if (!seen || seg_end > info->file_end)
            info->file_end = seg_end;

        seg_end = ph.p_vaddr + ph.p_memsz;
        if (!seen || seg_end > info->mem_end)
            info->mem_end = seg_end;

        seen = TRUE;
    }

    if (!seen)
        return EPLFMT;

    return 0;
}

/*
 * elf_pgmhdrld - sizing pass, called by kpgmhdrld()
 *
 * Parses just enough of the ELF file to tell xexec() how big a TPA the
 * program needs, expressed through the classic PGMHDR01 fields so that the
 * memory allocation logic in proc.c stays shared with the PRG loader.
 */
LONG elf_pgmhdrld(FH h, PGMHDR01 *hd)
{
    Elf32_Ehdr ehdr;
    ELFINFO info;
    LONG r;

    r = read_at(h, 0UL, &ehdr, (LONG)sizeof(ehdr));
    if (r < 0L)
        return r;

    r = elf_check_ehdr(&ehdr);
    if (r < 0L)
        return r;

    r = elf_scan(h, &ehdr, &info);
    if (r < 0L)
        return r;

    /*
     * express the layout as text + bss: text covers everything backed by
     * the file, bss the zero filled tail.  Their sum is the whole memory
     * image, which is all proc.c needs to size the TPA.
     */
    hd->h01_tlen = (LONG)(info.file_end - info.link_base);
    hd->h01_dlen = 0;
    hd->h01_blen = (LONG)(info.mem_end - info.file_end);
    hd->h01_slen = 0;
    hd->h01_res1 = 0;
    hd->h01_flags = 0;      /* main RAM, clear the whole heap */
    hd->h01_abs = 0;

    return 0;
}

/* apply a single relocation: add the load bias to the 32-bit word at vaddr */
static LONG elf_fixup(BYTE *load_base, const ELFINFO *info, LONG bias,
                      ULONG vaddr, UBYTE type)
{
    ULONG *slot;

    if (type != ELF_R_DIR32 && type != ELF_R_RELATIVE)
        return 0;   /* PC-relative and other slots need no load-time fixup */

    /* the slot must lie fully inside the loaded image */
    if (vaddr < info->link_base || vaddr + sizeof(ULONG) > info->mem_end)
        return EPLFMT;

    slot = (ULONG *)(load_base + (vaddr - info->link_base));
    *slot += (ULONG)bias;

    return 0;
}

/* walk one SHT_REL / SHT_RELA section and relocate every entry in it */
static LONG elf_relocate_section(FH h, const Elf32_Shdr *sh, BOOL rela,
                                 BYTE *load_base, const ELFINFO *info,
                                 LONG bias)
{
    union { Elf32_Rel rel; Elf32_Rela rela; } ent;
    ULONG entsize;
    ULONG offset;
    ULONG count;
    ULONG i;
    LONG r;

    entsize = sh->sh_entsize;
    if (entsize == 0)
        entsize = rela ? (ULONG)sizeof(Elf32_Rela) : (ULONG)sizeof(Elf32_Rel);

    count = sh->sh_size / entsize;
    for (i = 0; i < count; i++)
    {
        offset = sh->sh_offset + i * entsize;
        r = read_at(h, offset, &ent,
                    rela ? (LONG)sizeof(Elf32_Rela) : (LONG)sizeof(Elf32_Rel));
        if (r < 0L)
            return r;

        r = elf_fixup(load_base, info, bias, ent.rel.r_offset,
                      ELF32_R_TYPE(ent.rel.r_info));
        if (r < 0L)
            return r;
    }

    return 0;
}

/* apply every relocation section retained by ld --emit-relocs */
static LONG elf_relocate(FH h, const Elf32_Ehdr *e, BYTE *load_base,
                         const ELFINFO *info, LONG bias)
{
    Elf32_Shdr sh;
    LONG r;
    UWORD i;

    if (bias == 0)
        return 0;   /* loaded at its link address: nothing to relocate */

    if (e->e_shoff == 0 || e->e_shentsize < (UWORD)sizeof(Elf32_Shdr))
        return 0;   /* no section table: assume no relocations were kept */

    for (i = 0; i < e->e_shnum; i++)
    {
        r = read_at(h, e->e_shoff + (ULONG)i * e->e_shentsize,
                    &sh, (LONG)sizeof(sh));
        if (r < 0L)
            return r;

        if (sh.sh_type == SHT_REL)
            r = elf_relocate_section(h, &sh, FALSE, load_base, info, bias);
        else if (sh.sh_type == SHT_RELA)
            r = elf_relocate_section(h, &sh, TRUE, load_base, info, bias);
        else
            continue;

        if (r < 0L)
            return r;
    }

    return 0;
}

/*
 * elf_pgmld - load pass, called by kpgmld()
 *
 * Places the PT_LOAD segments in the TPA, zero fills the rest of it, then
 * relocates by the load bias.  The basepage TPA has already been allocated
 * by proc.c using the sizes returned by elf_pgmhdrld().
 */
LONG elf_pgmld(FH h, PD *p)
{
    Elf32_Ehdr ehdr;
    Elf32_Phdr ph;
    ELFINFO info;
    BYTE *load_base;
    LONG bias;
    LONG tpalen;
    LONG r;
    UWORD i;

    r = read_at(h, 0UL, &ehdr, (LONG)sizeof(ehdr));
    if (r < 0L)
        return r;

    r = elf_check_ehdr(&ehdr);
    if (r < 0L)
        return r;

    r = elf_scan(h, &ehdr, &info);
    if (r < 0L)
        return r;

    /* the image is loaded at the first byte after the basepage */
    load_base = (BYTE *)(p + 1);
    bias = (LONG)load_base - (LONG)info.link_base;

    tpalen = (LONG)(p->p_hitpa - p->p_lowtpa) - (LONG)sizeof(PD);
    if ((LONG)(info.mem_end - info.link_base) > tpalen)
    {
        KDEBUG(("BDOS elf_pgmld: ENSMEM\n"));
        return ENSMEM;
    }

    /* fill the PD segment fields; execution starts at the ELF entry point */
    p->p_tbase = load_base + (ehdr.e_entry - info.link_base);
    p->p_tlen  = (LONG)(info.file_end - info.link_base);
    p->p_dbase = load_base + (info.file_end - info.link_base);
    p->p_dlen  = 0;
    p->p_bbase = load_base + (info.file_end - info.link_base);
    p->p_blen  = (LONG)(info.mem_end - info.file_end);

    /* zero the whole image first so bss, inter-segment gaps and the rest
     * of the heap all start cleared, then read the file backed parts. */
    bzero(load_base, (LONG)p->p_hitpa - (LONG)load_base);

    for (i = 0; i < ehdr.e_phnum; i++)
    {
        r = read_at(h, ehdr.e_phoff + (ULONG)i * ehdr.e_phentsize,
                    &ph, (LONG)sizeof(ph));
        if (r < 0L)
            return r;

        if (ph.p_type != PT_LOAD || ph.p_filesz == 0)
            continue;

        r = xlseek((LONG)ph.p_offset, h, 0);
        if (r < 0L)
            return r;

        r = xread(h, (LONG)ph.p_filesz,
                  load_base + (ph.p_vaddr - info.link_base));
        if (r < 0L)
            return r;
        if (r != (LONG)ph.p_filesz)
            return EPLFMT;
    }

    return elf_relocate(h, &ehdr, load_base, &info, bias);
}

#endif /* CONF_WITH_ELF_LOADER */
