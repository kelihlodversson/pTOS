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
 *    R_*_RELATIVE dynamic relocations in .rel.dyn / .rela.dyn, and a program
 *    that embeds no absolute pointers in its initialised data needs none at
 *    all.
 *
 * In both cases the loader applies "actual_load_addr - link_base" to every
 * absolute slot, exactly the additive fixup the PRG loader does with its
 * own byte stream relocation table (see kpgmld.c:pgfix01()).  Relocation
 * information is read from the section headers, so the binary must not be
 * stripped of them.  Both ELF relocation encodings are handled: REL, which
 * ARM emits and which keeps the addend in the target word, and RELA, which
 * m68k emits and which carries an explicit addend field.
 *
 * ARM needs this because it cannot produce the m68k specific PRG format;
 * m68k can use it too as a normal alternative to PRG.
 *
 * A third, preferred form of the relocation data can be present alongside
 * either of the above: a PT_PTOS_RELOC program header pointing at a compact,
 * architecture neutral ULEB128 delta stream (doc/elfload.txt), produced by
 * post-processing a linked binary with tools/ptos-elf-pack.c.  When present
 * it is used instead of the section-based relocations above, and the
 * section header table is not consulted at all -- see elf_relocate_ptos().
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
#include "ptosabi.h"

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
#define SHF_ALLOC   0x2

/* pTOS-private OS-specific program header type (PT_LOOS..PT_HIOS is
 * 0x60000000..0x6fffffff): a compact, architecture neutral load relocation
 * stream produced by tools/ptos-elf-pack.c.  See doc/elfload.txt. */
#define PT_PTOS_RELOC       0x60000001UL

#define PTOS_RELOC_MAGIC    0x50544c31UL   /* checked verbatim, native endian */
#define PTOS_RELOC_VERSION  1

/* pTOS-private OS-specific program header type for native ABI imports
 * (same PT_LOOS..PT_HIOS range as PT_PTOS_RELOC above); locates the
 * ".ptos.imports" payload documented in doc/elfload.txt's "Native pTOS
 * ABI imports" section. */
#define PT_PTOS_IMPORTS       0x60000002UL

#define PTOS_IMPORTS_MAGIC    0x50544c32UL   /* checked verbatim, native endian */
#define PTOS_IMPORTS_VERSION  1

#define PTOS_BIND_CODE_ADDRESS  0
#define PTOS_BIND_DATA_ADDRESS  1
#define PTOS_BIND_GOT_SLOT      2

/* sanity cap on import_count/bind_count: no real statically linked
 * application needs anywhere near this many distinct pTOS ABI imports
 * (the entire "gemdos" namespace today is a few dozen symbols), and the
 * per-bind duplicate-slot check below is O(bind_count^2) -- an
 * unbounded value from a crafted file would turn Pexec() into an
 * effectively unbounded loop rather than a load-time error. */
#define PTOS_IMPORT_MAX_COUNT   512U

/* machine type and the "add the load bias to a 32-bit word" relocation
 * type for the architecture we are built for.  ELF_SLOT_ALIGN is the
 * alignment a 32-bit relocated slot must have: ARM faults on a 32-bit
 * access that is not 4-byte aligned, whereas m68k only requires 2-byte
 * alignment (and routinely relocates 2-byte-aligned instruction operands). */
#if ARCH_ARM
#define ELF_EM_EXPECTED 40      /* EM_ARM */
#define ELF_R_DIR32     2       /* R_ARM_ABS32 */
#define ELF_R_RELATIVE  23      /* R_ARM_RELATIVE */
#define ELF_SLOT_ALIGN  4
#else
#define ELF_EM_EXPECTED 4       /* EM_68K */
#define ELF_R_DIR32     1       /* R_68K_32 */
#define ELF_R_RELATIVE  22      /* R_68K_RELATIVE */
#define ELF_SLOT_ALIGN  2
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
#define ELF_DATA_EXPECTED ELFDATA2LSB
#else
#define ELF_DATA_EXPECTED ELFDATA2MSB
#endif

#define ELF32_R_TYPE(i) ((UBYTE)(i))
#define ELF_LONG_MAX    0x7fffffffUL

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

/* the 8 byte header at the start of a .ptos.reloc payload; the ULEB128
 * delta stream follows immediately after it (doc/elfload.txt) */
typedef struct {
    ULONG   magic;
    UWORD   version;
    UWORD   reserved;
} PTOSRELOCHDR;

/* the 32 byte header at the start of a .ptos.imports payload; the import
 * table, bind table and string table it describes follow it, each
 * located by its own *_off field (relative to this header's own file
 * offset, i.e. the PT_PTOS_IMPORTS program header's p_offset) rather
 * than assumed adjacent -- see doc/elfload.txt. */
typedef struct {
    ULONG   magic;
    UWORD   version;
    UWORD   reserved;
    ULONG   import_count;
    ULONG   bind_count;
    ULONG   import_table_off;
    ULONG   bind_table_off;
    ULONG   strtab_off;
    ULONG   strtab_size;
} PTOSIMPORTSHDR;

/* one ".ptos.imports" import table entry (doc/elfload.txt) */
typedef struct {
    ULONG   name_off;   /* offset into the string table: "namespace:name" */
    UWORD   abi_major;
    UWORD   abi_minor;
    UBYTE   kind;        /* PTOSABI_KIND_FUNCTION / PTOSABI_KIND_DATA */
    UBYTE   reserved[3];
} PTOSIMPORTENT;

/* one ".ptos.bind" bind table entry (doc/elfload.txt) */
typedef struct {
    ULONG   import_index;
    ULONG   slot_vaddr;
    UBYTE   bind_op;      /* PTOS_BIND_* */
    UBYTE   reserved[3];
} PTOSBINDENT;

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

    /* ELF offsets are 32-bit unsigned; xlseek takes a signed LONG.
     * Offsets >= 0x80000000 would become negative and cause a bogus seek. */
    if (offset > (ULONG)0x7fffffffUL)
        return EPLFMT;

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

/* ULONG arithmetic helpers: return TRUE when the operation overflows u32 */
static BOOL u32_add_overflow(ULONG a, ULONG b, ULONG *sum)
{
    *sum = a + b;
    return *sum < a;
}

static BOOL u32_mul_overflow(ULONG a, ULONG b, ULONG *prod)
{
    if (a != 0UL && b > 0xffffffffUL / a)
        return TRUE;

    *prod = a * b;
    return FALSE;
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
    ULONG phoff;
    ULONG ph_table_size;
    ULONG seg_end;
    ULONG span;
    LONG r;
    UWORD i;
    BOOL seen = FALSE;

    info->link_base = 0;
    info->file_end = 0;
    info->mem_end = 0;

    if (u32_mul_overflow((ULONG)e->e_phnum, (ULONG)e->e_phentsize, &ph_table_size)
     || u32_add_overflow(e->e_phoff, ph_table_size, &span))
        return EPLFMT;

    for (i = 0; i < e->e_phnum; i++)
    {
        if (u32_mul_overflow((ULONG)i, (ULONG)e->e_phentsize, &phoff)
         || u32_add_overflow(e->e_phoff, phoff, &phoff))
            return EPLFMT;

        r = read_at(h, phoff, &ph, (LONG)sizeof(ph));
        if (r < 0L)
            return r;

        if (ph.p_type != PT_LOAD)
            continue;

        if (ph.p_filesz > ph.p_memsz)
            return EPLFMT;

        /* reject a segment whose extent wraps past the top of the 32-bit
         * address space; p_filesz <= p_memsz, so testing p_memsz suffices */
        if (ph.p_vaddr + ph.p_memsz < ph.p_vaddr)
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

    span = info->file_end - info->link_base;
    if (span > ELF_LONG_MAX)
        return EPLFMT;

    span = info->mem_end - info->file_end;
    if (span > ELF_LONG_MAX)
        return EPLFMT;

    span = info->mem_end - info->link_base;
    if (span > ELF_LONG_MAX)
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
    /*
     * main RAM (no PF_TTRAMLOAD/PF_TTRAMMEM), and PF_FASTLOAD: elf_pgmld()
     * only clears the program's own footprint (info.mem_end -
     * info.link_base), not the whole TPA proc.c may have granted beyond
     * it -- see the bzero() call there. That is exactly what PF_FASTLOAD
     * documents for the PRG loader (kpgmld.c's pgmld01()), so this keeps
     * the two loaders' declared and actual behavior consistent.
     */
    hd->h01_flags = PF_FASTLOAD;
    hd->h01_abs = 0;

    return 0;
}

/*
 * apply a single relocation to the 32-bit word at vaddr.
 *
 * DIR32 (R_ARM_ABS32 / R_68K_32) always keeps the resolved link-time
 * absolute value in the target word itself, regardless of relocation
 * encoding: "ld --emit-relocs" still writes that value into the slot for
 * both REL (ARM) and RELA (m68k) output, so the fixup is simply "add the
 * load bias" either way.  This was verified directly against m68k
 * --emit-relocs output: e.g. a DIR32 slot referencing a symbol whose
 * link-time value is 0xed0 holds exactly 0xed0 in the file, with
 * r_addend left at 0 -- so recomputing from r_addend for RELA/DIR32
 * (as this function used to do) discards the correct value and
 * substitutes a wrong one, corrupting every absolute reference in an
 * ET_EXEC binary loaded via RELA relocations.
 *
 * RELATIVE is different, but only under RELA: a position independent
 * executable's RELATIVE slot may legitimately be left zero (that is the
 * point of carrying the addend out-of-band), so it must be recomputed as
 * bias + addend.  A REL-encoded RELATIVE relocation -- which is what ARM
 * would emit for a PIE -- still keeps its addend in the slot exactly
 * like DIR32, so it takes the same "add the bias" path as everything
 * else.
 */
static LONG elf_fixup(UBYTE *load_base, const ELFINFO *info, LONG bias,
                      ULONG vaddr, UBYTE type, BOOL rela, ULONG addend)
{
    ULONG *slot;

    if (type != ELF_R_DIR32 && type != ELF_R_RELATIVE)
        return 0;   /* PC-relative and other slots need no load-time fixup */

    /*
     * the slot must lie fully inside the loaded image.  Compute the bounds
     * without ever forming vaddr + sizeof(ULONG), which could wrap past the
     * top of the address space for a crafted r_offset.
     */
    if (vaddr < info->link_base)
        return EPLFMT;
    if (info->mem_end < (ULONG)sizeof(ULONG)
     || vaddr > info->mem_end - (ULONG)sizeof(ULONG))
        return EPLFMT;

    slot = (ULONG *)(load_base + (vaddr - info->link_base));

    /* the slot must satisfy the target's 32-bit access alignment (4 bytes on
     * ARM, 2 on m68k) or the load/store below would fault */
    if ((ULONG)slot & (ELF_SLOT_ALIGN - 1))
        return EPLFMT;

    /* unsigned arithmetic wraps modulo 2^32, so a negative bias applies
     * correctly whether we add to the slot or recompute it outright. */
    if (rela && type == ELF_R_RELATIVE)
        *slot = (ULONG)bias + addend;
    else
        *slot += (ULONG)bias;

    return 0;
}

/* walk one SHT_REL / SHT_RELA section and relocate every entry in it */
static LONG elf_relocate_section(FH h, const Elf32_Shdr *sh, BOOL rela,
                                 UBYTE *load_base, const ELFINFO *info,
                                 LONG bias)
{
    Elf32_Rela ent;     /* a RELA record is a REL record plus an addend */
    ULONG structsize;
    ULONG entsize;
    ULONG offset;
    ULONG count;
    ULONG addend;
    ULONG i;
    LONG r;

    structsize = rela ? (ULONG)sizeof(Elf32_Rela) : (ULONG)sizeof(Elf32_Rel);

    entsize = sh->sh_entsize;
    if (entsize == 0)
        entsize = structsize;

    /*
     * only trust a table whose entry size matches the record for its type
     * exactly and whose total size is a whole number of entries; otherwise
     * a malformed sh_entsize / sh_size could drive out-of-bounds reads or
     * leave a partially parsed relocation table.
     */
    if (entsize != structsize || (sh->sh_size % entsize) != 0)
        return EPLFMT;

    if (sh->sh_size != 0UL && u32_add_overflow(sh->sh_offset, sh->sh_size, &offset))
        return EPLFMT;

    count = sh->sh_size / entsize;
    for (i = 0; i < count; i++)
    {
        if (u32_mul_overflow(i, entsize, &offset)
         || u32_add_overflow(sh->sh_offset, offset, &offset))
            return EPLFMT;

        r = read_at(h, offset, &ent, (LONG)structsize);
        if (r < 0L)
            return r;

        addend = rela ? ent.r_addend : 0UL;
        r = elf_fixup(load_base, info, bias, ent.r_offset,
                      ELF32_R_TYPE(ent.r_info), rela, addend);
        if (r < 0L)
            return r;
    }

    return 0;
}

/* apply every relocation section retained by ld --emit-relocs */
static LONG elf_relocate(FH h, const Elf32_Ehdr *e, UBYTE *load_base,
                         const ELFINFO *info, LONG bias)
{
    Elf32_Shdr sh;
    ULONG shoff;
    ULONG sh_table_size;
    LONG r;
    UWORD i;

    if (bias == 0)
        return 0;   /* loaded at its link address: nothing to relocate */

    /*
     * we are loading at a non-link address, so relocations are mandatory.
     * A stripped binary with no usable section header table cannot be fixed
     * up, so reject it rather than silently loading an unrelocated image.
     */
    if (e->e_shoff == 0 || e->e_shnum == 0
     || e->e_shentsize < (UWORD)sizeof(Elf32_Shdr))
        return EPLFMT;

    if (u32_mul_overflow((ULONG)e->e_shnum, (ULONG)e->e_shentsize, &sh_table_size)
     || u32_add_overflow(e->e_shoff, sh_table_size, &shoff))
        return EPLFMT;

    for (i = 0; i < e->e_shnum; i++)
    {
        if (u32_mul_overflow((ULONG)i, (ULONG)e->e_shentsize, &shoff)
         || u32_add_overflow(e->e_shoff, shoff, &shoff))
            return EPLFMT;

        r = read_at(h, shoff, &sh, (LONG)sizeof(sh));
        if (r < 0L)
            return r;

        if (sh.sh_type != SHT_REL && sh.sh_type != SHT_RELA)
            continue;

        /*
         * sh_info names the section these relocations apply to, but for
         * dynamic relocations (.rel.dyn / .rela.dyn in a PIE, see the top
         * of this file) sh_info is 0 -- they apply to the load image as a
         * whole, not to one specific section -- and section index 0 is
         * always the reserved SHT_NULL entry, never SHF_ALLOC. Skipping
         * unconditionally on "target has no SHF_ALLOC" would therefore
         * skip every dynamic relocation and silently under-relocate any
         * ET_DYN binary. Only sh_info != 0 names a real target section
         * whose SHF_ALLOC-ness is worth checking.
         *
         * For a genuine section reference, a section with no SHF_ALLOC
         * flag (e.g. .debug_info) is never loaded, so its r_offset values
         * are offsets within that section on disk, not load-time virtual
         * addresses -- applying them through elf_fixup() would either
         * reject a perfectly valid binary (a debug-section offset that
         * happens to be misaligned) or, worse, silently corrupt the
         * loaded image (one that happens to land 4-byte aligned inside
         * [link_base, mem_end)). A non-stripped binary built with
         * "ld --emit-relocs" carries these sections, so this is not a
         * hypothetical: skip anything that isn't part of the loaded
         * image.
         */
        if (sh.sh_info >= (ULONG)e->e_shnum)
            return EPLFMT;

        if (sh.sh_info != 0)
        {
            Elf32_Shdr target;
            ULONG target_off;

            if (u32_mul_overflow(sh.sh_info, (ULONG)e->e_shentsize, &target_off)
             || u32_add_overflow(e->e_shoff, target_off, &target_off))
                return EPLFMT;

            r = read_at(h, target_off, &target, (LONG)sizeof(target));
            if (r < 0L)
                return r;

            if (!(target.sh_flags & SHF_ALLOC))
                continue;
        }

        if (sh.sh_type == SHT_REL)
            r = elf_relocate_section(h, &sh, FALSE, load_base, info, bias);
        else
            r = elf_relocate_section(h, &sh, TRUE, load_base, info, bias);

        if (r < 0L)
            return r;
    }

    return 0;
}

/*
 * decode one ULEB128 value from the .ptos.reloc delta stream, starting at
 * *pos (an absolute file offset) and never reading at or past limit.
 * Advances *pos past the value consumed. Returns a negative GEMDOS error on
 * a truncated stream or a value that would overflow a 32-bit ULONG.
 */
static LONG ptos_reloc_read_uleb128(FH h, ULONG *pos, ULONG limit, ULONG *out)
{
    ULONG value = 0;
    UWORD shift = 0;
    UBYTE b;
    LONG r;

    for (;;)
    {
        if (*pos >= limit)
            return EPLFMT;

        r = read_at(h, *pos, &b, (LONG)sizeof(b));
        if (r < 0L)
            return r;
        (*pos)++;

        /* a 6th continuation byte, or a 5th byte carrying bits above bit 31,
         * would overflow a 32-bit value -- reject rather than truncate it */
        if (shift >= 32 || (shift == 28 && (b & 0x70) != 0))
            return EPLFMT;

        value |= (ULONG)(b & 0x7f) << shift;

        if (!(b & 0x80))
        {
            *out = value;
            return 0;
        }

        shift += 7;
    }
}

/*
 * apply every slot listed in a .ptos.reloc payload (see doc/elfload.txt).
 * Every version 1 entry means the same thing elf_fixup() already does for
 * a DIR32 slot -- add the load bias -- so decoding just turns each delta
 * back into a vaddr and hands it to elf_fixup() unchanged.
 */
static LONG elf_relocate_ptos(FH h, const Elf32_Phdr *ph, UBYTE *load_base,
                              const ELFINFO *info, LONG bias)
{
    PTOSRELOCHDR rh;
    ULONG pos, limit;
    ULONG vaddr;
    ULONG delta;
    LONG r;
    BOOL first;

    if (bias == 0)
        return 0;   /* loaded at its link address: nothing to relocate */

    if (ph->p_filesz < (ULONG)sizeof(PTOSRELOCHDR))
        return EPLFMT;

    if (u32_add_overflow(ph->p_offset, ph->p_filesz, &limit))
        return EPLFMT;

    r = read_at(h, ph->p_offset, &rh, (LONG)sizeof(rh));
    if (r < 0L)
        return r;

    if (rh.magic != PTOS_RELOC_MAGIC || rh.version != PTOS_RELOC_VERSION)
        return EPLFMT;

    pos = ph->p_offset + (ULONG)sizeof(PTOSRELOCHDR);
    vaddr = info->link_base;
    first = TRUE;

    while (pos < limit)
    {
        r = ptos_reloc_read_uleb128(h, &pos, limit, &delta);
        if (r < 0L)
            return r;

        /* the format requires strictly ascending slots (doc/elfload.txt):
         * a zero delta past the first entry would reapply the fixup to the
         * slot just relocated, doubling its bias instead of being the
         * malformed stream it is */
        if (delta == 0 && !first)
            return EPLFMT;
        first = FALSE;

        if (u32_add_overflow(vaddr, delta, &vaddr))
            return EPLFMT;

        r = elf_fixup(load_base, info, bias, vaddr, ELF_R_DIR32, FALSE, 0);
        if (r < 0L)
            return r;
    }

    return 0;
}

#if CONF_WITH_PTOS_ABI_IMPORTS

/* generous headroom over the longest real import name today,
 * "gemdos:Ptermres" (15 bytes including the NUL) */
#define PTOS_IMPORT_NAME_MAX   63

/*
 * look up one already-split "namespace:name" import against the
 * kernel's own export tables. No namespace table is registered yet
 * (bdos/ptosabi.h's own top comment): a GEMDOS "gemdos:" table lived
 * here briefly, but the classic trap1()/TRAP #1 interface already
 * serves GEMDOS well enough that the added indirection was not worth
 * it (doc/elfload.txt). Extend this with a strcmp(namespace_name, ...)
 * arm and a PTOSABI_TABLE lookup (bdos/ptosabi.h) once a future
 * namespace (most plausibly "aes" or "vdi") registers one.
 *
 * Returns 0 and fills *out_addr and *out_kind on a match. Returns EPLFMT if
 * the namespace is unrecognised, the symbol is unknown within it, or
 * the export table's own ABI version cannot satisfy what the import
 * asks for (an exact major version match, at least the requested minor)
 * -- never binds a plausible-looking but wrong address.
 */
static LONG ptosabi_resolve(const char *namespace_name, const char *name,
                            UWORD abi_major, UWORD abi_minor,
                            PTOSABI_ADDR *out_addr, UBYTE *out_kind)
{
    (void)namespace_name;
    (void)name;
    (void)abi_major;
    (void)abi_minor;
    (void)out_addr;
    (void)out_kind;

    return EPLFMT;
}

/*
 * read one import's "namespace:name" string out of the .ptos.imports
 * string table into buf (which must be PTOS_IMPORT_NAME_MAX+2 bytes),
 * and split it in place at the first ':' into *out_ns and *out_name.
 * Rejects a string that doesn't fit in the buffer, isn't NUL-terminated
 * within the string table's own bounds, or has no ':' separator (or an
 * empty half on either side of one) -- never guesses a namespace or
 * name out of a malformed entry.
 *
 * The read window is PTOS_IMPORT_NAME_MAX+1 bytes, one more than the
 * longest *content* this accepts: a name of exactly PTOS_IMPORT_NAME_MAX
 * bytes still needs its own terminating NUL read and checked to tell it
 * apart from a longer, truly unterminated string -- capping the window
 * at PTOS_IMPORT_NAME_MAX itself would silently reject that valid
 * boundary case instead of accepting it.
 */
static LONG ptos_read_import_name(FH h, ULONG strtab_abs_off, ULONG strtab_size,
                                  ULONG name_off, char *buf,
                                  char **out_ns, char **out_name)
{
    ULONG avail, want, off;
    LONG r;
    char *colon;

    if (name_off >= strtab_size)
        return EPLFMT;

    avail = strtab_size - name_off;
    want = (avail < (ULONG)PTOS_IMPORT_NAME_MAX + 1) ? avail : (ULONG)PTOS_IMPORT_NAME_MAX + 1;

    if (u32_add_overflow(strtab_abs_off, name_off, &off))
        return EPLFMT;

    r = read_at(h, off, buf, (LONG)want);
    if (r < 0L)
        return r;
    buf[want] = '\0';

    if (strlen(buf) == want)
        return EPLFMT;   /* no NUL found within the read window */

    colon = strchr(buf, ':');
    if (colon == NULL || colon == buf || colon[1] == '\0')
        return EPLFMT;

    *colon = '\0';
    *out_ns = buf;
    *out_name = colon + 1;
    return 0;
}

/*
 * write one resolved import address into its 4-byte slot. Shares
 * elf_fixup()'s bounds/alignment checks, but unlike a relocation this
 * is not "add the load bias": the export table already holds real,
 * running kernel addresses, so the resolved address is written as-is.
 * All three PTOS_BIND_* operations write the plain address in version 1
 * (see doc/elfload.txt for why they are still kept distinct); any other
 * bind_op is a format error.
 *
 * addr is read through whichever PTOSABI_ADDR member matches kind
 * (PTOSABI_KIND_*, from the same export this bind resolved against) --
 * never the other one; see bdos/ptosabi.h's own comment on why the two
 * union members are not interchangeable in general, even though they
 * are the same size and representation on every architecture this
 * loader actually runs on.
 */
static LONG ptos_bind_apply(UBYTE *load_base, const ELFINFO *info,
                            ULONG vaddr, UBYTE bind_op, PTOSABI_ADDR addr,
                            UBYTE kind)
{
    ULONG *slot;
    ULONG value;

    if (bind_op != PTOS_BIND_CODE_ADDRESS && bind_op != PTOS_BIND_DATA_ADDRESS
     && bind_op != PTOS_BIND_GOT_SLOT)
        return EPLFMT;

    if (vaddr < info->link_base)
        return EPLFMT;
    if (info->mem_end < (ULONG)sizeof(ULONG)
     || vaddr > info->mem_end - (ULONG)sizeof(ULONG))
        return EPLFMT;

    slot = (ULONG *)(load_base + (vaddr - info->link_base));

    if ((ULONG)slot & (ELF_SLOT_ALIGN - 1))
        return EPLFMT;

    value = (kind == PTOSABI_KIND_DATA) ? (ULONG)addr.data : (ULONG)addr.func;
    *slot = value;
    return 0;
}

/*
 * read one import table entry and resolve it against the kernel's own
 * export tables, checking that its declared kind agrees with what the
 * export table actually is. Shared between elf_resolve_imports()'s two
 * passes over the import table (see that function's own comment): the
 * up-front validation of every import_count entry, and, again, while
 * applying each bind -- kept as a fresh read+resolve each time rather
 * than cached, matching elf_resolve_imports()'s own "never build an
 * in-memory table of file content" design.
 *
 * namebuf must be PTOS_IMPORT_NAME_MAX+2 bytes (see
 * ptos_read_import_name()); out_ns and out_name point into it and are
 * only valid as long as it is.
 */
static LONG ptosabi_validate_import(FH h, ULONG import_table_abs,
                                    ULONG strtab_abs, ULONG strtab_size,
                                    ULONG import_index, char *namebuf,
                                    char **out_ns, char **out_name,
                                    PTOSABI_ADDR *out_addr, UBYTE *out_kind)
{
    PTOSIMPORTENT imp;
    ULONG off;
    LONG r;

    if (u32_mul_overflow(import_index, (ULONG)sizeof(PTOSIMPORTENT), &off)
     || u32_add_overflow(import_table_abs, off, &off))
        return EPLFMT;

    r = read_at(h, off, &imp, (LONG)sizeof(imp));
    if (r < 0L)
        return r;

    r = ptos_read_import_name(h, strtab_abs, strtab_size, imp.name_off,
                              namebuf, out_ns, out_name);
    if (r < 0L)
        return r;

    r = ptosabi_resolve(*out_ns, *out_name, imp.abi_major, imp.abi_minor,
                        out_addr, out_kind);
    if (r < 0L)
        return r;

    /* the import's declared kind must agree with what the export table
     * actually is: an app importing a data symbol as if it were callable
     * (or vice versa) is a packaging/link error, not something to bind
     * anyway and hope for the best */
    if (*out_kind != imp.kind)
        return EPLFMT;

    return 0;
}

/*
 * elf_resolve_imports - resolve and apply every entry in a .ptos.imports
 * payload (doc/elfload.txt), found via a PT_PTOS_IMPORTS program header
 * exactly the way elf_relocate_ptos() finds PT_PTOS_RELOC.
 *
 * Two passes over the import table: the first validates every one of
 * its import_count entries against the kernel's own export tables,
 * whether or not any bind actually references it -- an import entry
 * with no bind at all must still fail the load if it cannot be
 * resolved (doc/elfload.txt: "An import naming an unknown symbol... "),
 * exactly as one that IS bound to would. Only then does the second pass
 * apply each bind_count record, re-validating its own import_index
 * again rather than caching the first pass's result: this keeps the
 * loader's memory footprint at a handful of stack-resident records
 * regardless of how many imports a program has, matching every other
 * pass in this file, none of which ever builds an in-memory table of
 * file content.
 */
static LONG elf_resolve_imports(FH h, const Elf32_Phdr *ph, UBYTE *load_base,
                                const ELFINFO *info)
{
    PTOSIMPORTSHDR ih;
    ULONG import_table_lim, bind_table_lim, strtab_lim;
    ULONG import_table_abs, bind_table_abs, strtab_abs;
    ULONG i, j;
    LONG r;

    if (ph->p_filesz < (ULONG)sizeof(PTOSIMPORTSHDR))
        return EPLFMT;

    r = read_at(h, ph->p_offset, &ih, (LONG)sizeof(ih));
    if (r < 0L)
        return r;

    if (ih.magic != PTOS_IMPORTS_MAGIC || ih.version != PTOS_IMPORTS_VERSION)
        return EPLFMT;

    if (ih.import_count > PTOS_IMPORT_MAX_COUNT || ih.bind_count > PTOS_IMPORT_MAX_COUNT)
        return EPLFMT;

    /* every table/region named by the header must lie fully inside the
     * payload (ph->p_filesz), checked without ever forming an
     * intermediate sum that could silently wrap for a crafted file */
    if (u32_mul_overflow(ih.import_count, (ULONG)sizeof(PTOSIMPORTENT), &import_table_lim)
     || u32_add_overflow(ih.import_table_off, import_table_lim, &import_table_lim)
     || import_table_lim > ph->p_filesz)
        return EPLFMT;

    if (u32_mul_overflow(ih.bind_count, (ULONG)sizeof(PTOSBINDENT), &bind_table_lim)
     || u32_add_overflow(ih.bind_table_off, bind_table_lim, &bind_table_lim)
     || bind_table_lim > ph->p_filesz)
        return EPLFMT;

    if (u32_add_overflow(ih.strtab_off, ih.strtab_size, &strtab_lim)
     || strtab_lim > ph->p_filesz)
        return EPLFMT;

    if (u32_add_overflow(ph->p_offset, ih.import_table_off, &import_table_abs)
     || u32_add_overflow(ph->p_offset, ih.bind_table_off, &bind_table_abs)
     || u32_add_overflow(ph->p_offset, ih.strtab_off, &strtab_abs))
        return EPLFMT;

    /* first pass: every import_count entry must resolve, whether or not
     * any bind references it -- see this function's own comment above. */
    for (i = 0; i < ih.import_count; i++)
    {
        char namebuf[PTOS_IMPORT_NAME_MAX + 2];
        char *ns, *name;
        /* zero-initialised: with no namespace currently registered (see
         * ptosabi_resolve()), every call below returns EPLFMT without
         * ever writing these, and -Wmaybe-uninitialized cannot see across
         * that call boundary that the "r < 0L" check always fires first */
        PTOSABI_ADDR addr = { NULL };
        UBYTE kind = 0;

        r = ptosabi_validate_import(h, import_table_abs, strtab_abs,
                                    ih.strtab_size, i, namebuf, &ns, &name,
                                    &addr, &kind);
        if (r < 0L)
            return r;
    }

    for (i = 0; i < ih.bind_count; i++)
    {
        PTOSBINDENT bind;
        char namebuf[PTOS_IMPORT_NAME_MAX + 2];
        char *ns, *name;
        PTOSABI_ADDR addr = { NULL };
        UBYTE kind = 0;
        ULONG off;

        if (u32_mul_overflow(i, (ULONG)sizeof(PTOSBINDENT), &off)
         || u32_add_overflow(bind_table_abs, off, &off))
            return EPLFMT;

        r = read_at(h, off, &bind, (LONG)sizeof(bind));
        if (r < 0L)
            return r;

        if (bind.import_index >= ih.import_count)
            return EPLFMT;

        /* no two binds may target the same slot -- see doc/elfload.txt.
         * Bounded to at most PTOS_IMPORT_MAX_COUNT^2 re-reads by the
         * cap checked above. */
        for (j = 0; j < i; j++)
        {
            PTOSBINDENT prior;
            ULONG prior_off;

            if (u32_mul_overflow(j, (ULONG)sizeof(PTOSBINDENT), &prior_off)
             || u32_add_overflow(bind_table_abs, prior_off, &prior_off))
                return EPLFMT;

            r = read_at(h, prior_off, &prior, (LONG)sizeof(prior));
            if (r < 0L)
                return r;

            if (prior.slot_vaddr == bind.slot_vaddr)
                return EPLFMT;
        }

        r = ptosabi_validate_import(h, import_table_abs, strtab_abs,
                                    ih.strtab_size, bind.import_index,
                                    namebuf, &ns, &name, &addr, &kind);
        if (r < 0L)
            return r;

        KDEBUG(("ptosabi: bind #%ld: %s:%s -> %p, slot=%08lx op=%d\n",
                (long)i, ns, name, (void *)addr.func,
                (unsigned long)bind.slot_vaddr, (int)bind.bind_op));

        r = ptos_bind_apply(load_base, info, bind.slot_vaddr, bind.bind_op,
                            addr, kind);
        if (r < 0L)
            return r;
    }

    return 0;
}

#endif /* CONF_WITH_PTOS_ABI_IMPORTS */

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
    /* zero-initialised so a build that cannot prove have_ptos_reloc/
     * have_ptos_imports imply an assignment (this one included) never
     * warns about a possibly-uninitialised read below; the real value
     * is always the one assigned in the loop, never this one */
    Elf32_Phdr ptos_reloc_ph = { 0 };
    Elf32_Phdr ptos_imports_ph = { 0 };
    ELFINFO info;
    UBYTE *load_base;
    LONG bias;
    LONG tpalen;
    ULONG phoff;
    ULONG ph_table_size;
    LONG r;
    UWORD i;
    BOOL have_ptos_reloc = FALSE;
    BOOL have_ptos_imports = FALSE;

    r = read_at(h, 0UL, &ehdr, (LONG)sizeof(ehdr));
    if (r < 0L)
        return r;

    r = elf_check_ehdr(&ehdr);
    if (r < 0L)
        return r;

    r = elf_scan(h, &ehdr, &info);
    if (r < 0L)
        return r;

    /* the entry point must fall inside the loaded image, or p_tbase would
     * point outside the TPA (underflow below link_base, or past mem_end) */
    if (ehdr.e_entry < info.link_base || ehdr.e_entry >= info.mem_end)
        return EPLFMT;

    /* the image is loaded at the first byte after the basepage */
    load_base = (UBYTE *)(p + 1);
    /* compute the bias in unsigned then reinterpret as signed; this avoids
     * signed overflow UB when either operand has its high bit set, and the
     * relocation arithmetic downstream already relies on unsigned wrap. */
    bias = (LONG)((ULONG)load_base - info.link_base);

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

    /* Zero the loaded image first so bss and inter-segment gaps start
     * cleared, then read the file backed parts over it. Only the
     * program's own footprint (mem_end - link_base, already validated
     * against tpalen above) needs clearing here -- not the whole TPA
     * out to p_hitpa, which is however much free memory Pexec() granted
     * the program and can be most or all of RAM. Zeroing that much
     * needlessly, and slowly (each never-touched guest page can fault
     * in fresh host memory under an emulator), looks indistinguishable
     * from a hang for a program given a large default allocation. */
    bzero(load_base, (LONG)(info.mem_end - info.link_base));

    if (u32_mul_overflow((ULONG)ehdr.e_phnum, (ULONG)ehdr.e_phentsize, &ph_table_size)
     || u32_add_overflow(ehdr.e_phoff, ph_table_size, &phoff))
        return EPLFMT;

    for (i = 0; i < ehdr.e_phnum; i++)
    {
        if (u32_mul_overflow((ULONG)i, (ULONG)ehdr.e_phentsize, &phoff)
         || u32_add_overflow(ehdr.e_phoff, phoff, &phoff))
            return EPLFMT;

        r = read_at(h, phoff, &ph, (LONG)sizeof(ph));
        if (r < 0L)
            return r;

        if (ph.p_type == PT_PTOS_RELOC)
        {
            /* last one wins if a malformed/hand-edited file has more than
             * one; elf_relocate_ptos() below validates it properly */
            ptos_reloc_ph = ph;
            have_ptos_reloc = TRUE;
            continue;
        }

        if (ph.p_type == PT_PTOS_IMPORTS)
        {
            /* same "last one wins, validated properly below" approach as
             * PT_PTOS_RELOC above */
            ptos_imports_ph = ph;
            have_ptos_imports = TRUE;
            continue;
        }

        if (ph.p_type != PT_LOAD || ph.p_filesz == 0)
            continue;

        /* p_offset and p_filesz are 32-bit unsigned; xlseek/xread take
         * signed LONGs.  Values >= 0x80000000 would become negative and
         * drive a bogus seek or an oversized read before the short-read
         * check below could catch it. */
        if (ph.p_offset > (ULONG)0x7fffffffUL
         || ph.p_filesz > (ULONG)0x7fffffffUL)
            return EPLFMT;

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

    if (have_ptos_reloc)
        r = elf_relocate_ptos(h, &ptos_reloc_ph, load_base, &info, bias);
    else
        r = elf_relocate(h, &ehdr, load_base, &info, bias);
    if (r < 0L)
        return r;

    if (have_ptos_imports)
    {
#if CONF_WITH_PTOS_ABI_IMPORTS
        return elf_resolve_imports(h, &ptos_imports_ph, load_base, &info);
#else
        /* no export table to resolve against: binding nothing and
         * pretending the program loaded correctly would leave every
         * import slot at 0, which a native application must never
         * silently call through (see doc/elfload.txt) */
        (void)ptos_imports_ph;
        return EPLFMT;
#endif
    }

    return 0;
}

#endif /* CONF_WITH_ELF_LOADER */
