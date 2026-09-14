/*
 * ptos-elf-pack.c - pack an ELF binary's load relocations for bdos/elfld.c
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * Reads a statically linked ARM or m68k ELF32 executable of either kind
 * bdos/elfld.c already accepts -- a fixed base ET_EXEC linked with
 * "ld --emit-relocs", or a position independent ET_DYN linked with
 * "ld -pie --no-dynamic-linker" -- and rewrites it into the compact,
 * architecture neutral relocation format documented in doc/elfload.txt:
 * a ".ptos.reloc" payload (an 8 byte header plus a ULEB128 delta stream)
 * referenced by a new PT_PTOS_RELOC program header, instead of the
 * SHT_REL/SHT_RELA sections the input format above relies on.
 *
 * The rewrite only appends bytes: the input file's own content -- ELF
 * header aside -- is copied through unchanged at the same offsets, so
 * nothing that already worked stops working. Only two things change in
 * the output:
 *
 *   - a RELA RELATIVE slot (see main()'s relocation loop below) gets its
 *     addend materialised into the file bytes at its p_offset when that
 *     slot is file-backed, which is what it needs to hold for the compact
 *     format's single "add the load bias" operation to be correct for
 *     every listed slot;
 *   - the ELF header's e_phoff/e_phnum are repointed at a new program
 *     header table (a copy of the original entries plus one new
 *     PT_PTOS_RELOC entry), appended after the .ptos.reloc payload.
 *
 * This is a host build tool: it is compiled with the native ($(NATIVECC))
 * compiler, not a target cross compiler, and never assumes the host's
 * endianness or word size matches the ELF file it is processing -- every
 * multi-byte field is read and written explicitly according to the
 * input's e_ident[EI_DATA].
 *
 * Usage: ptos-elf-pack [--strip-shdr] [--allow-no-relocations] \
 *            <input.elf> <output.elf>
 *
 * --strip-shdr additionally clears e_shoff/e_shnum/e_shentsize/e_shstrndx
 * in the output, so a loader has no SHT_REL/SHT_RELA fallback to fall
 * back to if PT_PTOS_RELOC discovery ever regresses -- the section header
 * bytes themselves are left in the file (this tool does not yet reclaim
 * that space), just unreferenced.
 *
 * --allow-no-relocations lifts the default refusal to pack a fixed-base
 * ET_EXEC with zero relocated slots, a shape indistinguishable here from
 * one linked without the documented -q/--emit-relocs flag (see the check
 * itself for why); pass it only once you have confirmed the input
 * genuinely needs no load-time fixups.
 *
 * This is the first, minimal cut of the tool (see issue #309): it does
 * not yet strip the now-superseded relocation/symbol *data* it
 * supersedes (only --strip-shdr's own four header fields above), or add
 * a matching section header entry for the payload -- both are left as
 * later refinements, tracked under issue #308.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

/* ELF32 constants and field byte offsets (System V ABI); kept as plain
 * byte offsets rather than a packed struct so this tool never depends on
 * the host compiler's struct layout or alignment rules. */
#define EI_NIDENT       16
#define EI_CLASS        4
#define EI_DATA         5
#define ELFCLASS32      1
#define ELFDATA2LSB     1
#define ELFDATA2MSB     2

#define EHDR_SIZE       52
#define EHDR_E_TYPE         16
#define EHDR_E_MACHINE      18
#define EHDR_E_ENTRY        24
#define EHDR_E_PHOFF        28
#define EHDR_E_SHOFF        32
#define EHDR_E_PHENTSIZE    42
#define EHDR_E_PHNUM        44
#define EHDR_E_SHENTSIZE    46
#define EHDR_E_SHNUM        48
#define EHDR_E_SHSTRNDX     50

#define ET_EXEC         2
#define ET_DYN          3
#define EM_68K          4
#define EM_ARM          40

#define PHDR_SIZE       32
#define PHDR_P_TYPE     0
#define PHDR_P_OFFSET   4
#define PHDR_P_VADDR    8
#define PHDR_P_FILESZ   16
#define PHDR_P_MEMSZ    20

#define PT_LOAD         1UL
#define PT_PHDR         6UL
#define PT_PTOS_RELOC   0x60000001UL
#define PT_PTOS_IMPORTS 0x60000002UL

#define SHDR_SIZE       40
#define SHDR_SH_TYPE    4
#define SHDR_SH_FLAGS   8
#define SHDR_SH_ADDR    12
#define SHDR_SH_OFFSET  16
#define SHDR_SH_SIZE    20
#define SHDR_SH_LINK    24
#define SHDR_SH_INFO    28
#define SHDR_SH_ENTSIZE 36

#define SHT_RELA        4UL
#define SHT_DYNAMIC     6UL
#define SHT_REL         9UL
#define SHT_RELR        19UL
/* android's packed-dynamic-relocation format, predating the now-standard
 * SHT_RELR above; still emitted by some lld/bionic configurations
 * (linker flag --pack-dyn-relocs=android) */
#define SHT_ANDROID_REL   0x60000001UL
#define SHT_ANDROID_RELA  0x60000002UL
#define SHF_ALLOC       0x2UL

#define REL_SIZE        8
#define RELA_SIZE       12

#define SYM_SIZE        16
#define SYM_ST_NAME     0
#define SYM_ST_INFO     12
#define SYM_ST_SHNDX    14
#define SHN_UNDEF       0U
#define SHN_LORESERVE   0xff00U
#define SHN_ABS         0xfff1U
#define SHN_COMMON      0xfff2U

/* ELF32_ST_TYPE(st_info): low 4 bits of the symbol's st_info byte */
#define STT_OBJECT      1U
#define STT_FUNC        2U

#define DYN_SIZE        8       /* one Elf32_Dyn entry: d_tag + d_val/d_ptr */
#define DT_NEEDED       1UL

#define PTOS_RELOC_MAGIC    0x50544c31UL
#define PTOS_RELOC_VERSION  1

#define PTOS_IMPORTS_MAGIC    0x50544c32UL
#define PTOS_IMPORTS_VERSION  1

#define PTOSABI_KIND_FUNCTION   0
#define PTOSABI_KIND_DATA       1

#define PTOS_BIND_CODE_ADDRESS  0
#define PTOS_BIND_DATA_ADDRESS  1

/* must match bdos/elfld.c's PTOS_IMPORT_MAX_COUNT: the loader rejects a
 * .ptos.imports payload with more entries than this, so producing one
 * here would just move the failure from pack time to load time */
#define PTOS_IMPORT_MAX_COUNT   512U

/* must match bdos/elfld.c's PTOS_IMPORT_NAME_MAX: the loader reads at
 * most this many bytes of a "namespace:name" string (plus its own
 * NUL), so a longer one packed here would load fine here but always
 * fail at Pexec() time with no way for the packer's own success to
 * have warned about it */
#define PTOS_IMPORT_NAME_MAX    63U

/* the SDK's link-time-fiction stub library every pTOS ABI import must
 * come from (doc/elfload.txt); its SONAME's trailing version number is
 * read directly as the ABI major version every symbol pulled from it is
 * imported at. */
#define PTOS_ABI_SONAME_PREFIX  "libptos-abi.so."

/* Relocation types confirmed (either from the target's ELF psABI or by
 * inspecting real "readelf -r" output from the documented build recipes in
 * doc/elfload.txt) to need no load-time value fixup: PC-relative branches,
 * link-time-resolved veneer markers, and the reserved "no relocation" type.
 * Anything NOT in this list, and not the DIR32/RELATIVE type already
 * handled explicitly, is unrecognised and must be rejected rather than
 * silently skipped (see #309's acceptance criteria) -- skipping a type that
 * actually needs a fixup would silently corrupt the packed image. */
static const uint32_t arm_no_fixup_types[] = {
    0,   /* R_ARM_NONE */
    1,   /* R_ARM_PC24 (deprecated) */
    3,   /* R_ARM_REL32 */
    10,  /* R_ARM_THM_CALL */
    28,  /* R_ARM_CALL */
    29,  /* R_ARM_JUMP24 */
    30,  /* R_ARM_THM_JUMP24 */
    40,  /* R_ARM_V4BX -- seen from a plain "ld -q" ARM build in practice */
    42,  /* R_ARM_PREL31 */
    51,  /* R_ARM_THM_JUMP19 */
    /* R_ARM_CALL/JUMP24/THM_CALL/THM_JUMP24/THM_JUMP19 have never actually
     * been observed retained in this file's own testing -- this binutils
     * version appears to fully resolve and drop ordinary same-image branch
     * relocations under --emit-relocs rather than retaining them -- but
     * they are unambiguously PC-relative call/branch types by the ARM ELF
     * ABI's own definition (never an absolute value), so allowlisting them
     * ahead of ever encountering one carries none of R_ARM_TARGET1's
     * ambiguity below. A sufficiently large Thumb image (a long-range call
     * needing a veneer) is the plausible case that would actually retain
     * one of these. */
    /* deliberately NOT R_ARM_TARGET1 (38): the ARM ELF ABI lets the linker
     * resolve it as either R_ARM_ABS32- or R_ARM_REL32-like depending on
     * --target1-abs/--target1-rel, so unlike every type above it is not
     * unambiguously PC-relative -- an ABS32-resolved TARGET1 slot holds an
     * absolute address and does need the load bias. Nothing in this file's
     * verified build recipes has been seen to emit it; reject it rather
     * than guess which mode produced it. */
};

static const uint32_t m68k_no_fixup_types[] = {
    0,  /* R_68K_NONE */
    4,  /* R_68K_PC32 */
    5,  /* R_68K_PC16 */
    6,  /* R_68K_PC8 */
};

static int type_needs_no_fixup(uint32_t machine, uint32_t type)
{
    const uint32_t *list;
    size_t count, i;

    if (machine == EM_ARM)
    {
        list = arm_no_fixup_types;
        count = sizeof(arm_no_fixup_types) / sizeof(arm_no_fixup_types[0]);
    }
    else
    {
        list = m68k_no_fixup_types;
        count = sizeof(m68k_no_fixup_types) / sizeof(m68k_no_fixup_types[0]);
    }

    for (i = 0; i < count; i++)
    {
        if (list[i] == type)
            return 1;
    }
    return 0;
}

/* Of the no-fixup types above, the overwhelming majority genuinely name a
 * symbol (a branch/call target), so their r_info's symbol field must be
 * resolved and checked like any other -- an index of 0 there is a real
 * STN_UNDEF, not a convention. Exactly two are the ABI-defined exception:
 * R_ARM_NONE, whose whole record is a no-op regardless of any field, and
 * R_ARM_V4BX, whose symbol table index the ARM ELF ABI requires to be
 * zero -- it marks an interworking veneer's BX instruction rather than
 * referencing a symbol at all (confirmed emitted this way, with symbol
 * index 0, by a plain "ld -q" ARM build: see richtest_arm.elf in this
 * tool's test history). R_68K_NONE is the same no-op case on m68k. */
static int type_symbol_field_is_meaningless(uint32_t machine, uint32_t type)
{
    if (machine == EM_ARM)
        return type == 0 /* R_ARM_NONE */ || type == 40 /* R_ARM_V4BX */;
    return type == 0; /* R_68K_NONE */
}

/* one input relocation this tool cares about, resolved to the load-time
 * operation the compact format always applies: "*slot += load_bias" */
typedef struct {
    uint32_t vaddr;
} SLOT;

/* one pTOS ABI import this tool has converted a dynamic relocation into
 * (doc/elfload.txt's ".ptos.imports payload"); "name" already carries
 * the "namespace:" prefix (always "gemdos:" in this version -- see
 * find_abi_needed()) */
typedef struct {
    char     *name;
    uint32_t  abi_major;
    uint32_t  abi_minor;
    int       kind;      /* PTOSABI_KIND_* */
} IMPORT;

/* one ".ptos.bind" entry: which import, which slot, which operation */
typedef struct {
    uint32_t import_index;
    uint32_t slot_vaddr;
    int      bind_op;    /* PTOS_BIND_* */
} BIND;

/* one input PT_LOAD segment's file<->memory mapping, needed to find the
 * file byte a RELA/RELATIVE slot's addend must be materialised into */
typedef struct {
    uint32_t vaddr;
    uint32_t offset;
    uint32_t filesz;
    uint32_t memsz;
} SEGMENT;

static const char *g_argv0;
static int g_is_be;

static void die(const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "%s: ", g_argv0);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

static uint32_t rd32(const unsigned char *p)
{
    if (g_is_be)
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
             | ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[1] << 8)  |  (uint32_t)p[0];
}

static uint16_t rd16(const unsigned char *p)
{
    if (g_is_be)
        return (uint16_t)(((uint32_t)p[0] << 8) | (uint32_t)p[1]);
    return (uint16_t)(((uint32_t)p[1] << 8) | (uint32_t)p[0]);
}

static void wr32(unsigned char *p, uint32_t v)
{
    if (g_is_be)
    {
        p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
        p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
    }
    else
    {
        p[3] = (unsigned char)(v >> 24); p[2] = (unsigned char)(v >> 16);
        p[1] = (unsigned char)(v >> 8);  p[0] = (unsigned char)v;
    }
}

static void wr16(unsigned char *p, uint16_t v)
{
    if (g_is_be)
    {
        p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v;
    }
    else
    {
        p[1] = (unsigned char)(v >> 8); p[0] = (unsigned char)v;
    }
}

/* growable output buffer */
typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} BUF;

static void buf_reserve(BUF *b, size_t extra)
{
    if (b->len + extra <= b->cap)
        return;

    while (b->cap < b->len + extra)
        b->cap = b->cap ? b->cap * 2 : 4096;

    b->data = realloc(b->data, b->cap);
    if (!b->data)
        die("out of memory");
}

static void buf_append(BUF *b, const void *p, size_t n)
{
    buf_reserve(b, n);
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

static void buf_append_u8(BUF *b, unsigned char v)
{
    buf_append(b, &v, 1);
}

static void buf_append_uleb128(BUF *b, uint32_t v)
{
    for (;;)
    {
        unsigned char byte = (unsigned char)(v & 0x7f);
        v >>= 7;
        if (v != 0)
            buf_append_u8(b, (unsigned char)(byte | 0x80));
        else
        {
            buf_append_u8(b, byte);
            break;
        }
    }
}

/* read the whole input file into memory */
static unsigned char *read_file(const char *path, long *out_size)
{
    FILE *f;
    long size;
    unsigned char *buf;

    f = fopen(path, "rb");
    if (!f)
        die("cannot open '%s' for reading", path);

    if (fseek(f, 0, SEEK_END) != 0)
        die("cannot seek '%s'", path);
    size = ftell(f);
    if (size < 0)
        die("cannot determine size of '%s'", path);
    rewind(f);

    buf = malloc((size_t)size ? (size_t)size : 1);
    if (!buf)
        die("out of memory reading '%s'", path);

    if (size > 0 && fread(buf, 1, (size_t)size, f) != (size_t)size)
        die("short read on '%s'", path);

    fclose(f);
    *out_size = size;
    return buf;
}

/* find the PT_LOAD segment containing vaddr and translate it to a file
 * offset; dies with a clear message if vaddr falls outside every
 * segment's file-backed part (e.g. inside .bss).
 *
 * This is a real, permanent limitation of the version 1 wire format, not
 * just a missing byte to write into: a RELA+RELATIVE slot is represented
 * by baking its resolved value (bias + addend) into the slot's own file
 * bytes and then applying the *same* "+= bias" op every other listed slot
 * gets. A .bss-resident slot has no file bytes at all -- the loader's
 * zero-fill leaves 0 there -- so "0 += bias" would yield the wrong value
 * (bias, not bias + addend); representing this correctly needs a second
 * wire operation that carries its own addend, which version 1 does not
 * have (see doc/elfload.txt). Rather than extend the format under review
 * pressure, this is called out there as a known v1 boundary. */
/* report whether the whole [vaddr, vaddr+size) range lies within some
 * PT_LOAD segment's mapped memory image (p_vaddr..p_vaddr+p_memsz).
 * SHF_ALLOC alone does not prove this: a valid ELF can place an allocated
 * section outside every PT_LOAD (e.g. a PT_TLS-only section, or one a
 * linker script assigns to no segment at all), and this loader -- like
 * elf_fixup() -- only ever touches memory inside a PT_LOAD's own range,
 * so a symbol living outside all of them does not move under the load
 * bias the way an ordinary in-image symbol does. */
static int range_covered_by_load(const SEGMENT *segs, size_t nsegs,
                                 uint32_t vaddr, uint32_t size)
{
    size_t i;

    for (i = 0; i < nsegs; i++)
    {
        if (vaddr >= segs[i].vaddr
         && (uint64_t)(vaddr - segs[i].vaddr) + size <= segs[i].memsz)
            return 1;
    }
    return 0;
}

static int try_vaddr_to_file_offset(const SEGMENT *segs, size_t nsegs,
                                    uint32_t vaddr, uint32_t *out_offset)
{
    size_t i;

    for (i = 0; i < nsegs; i++)
    {
        /* the caller always writes a full 4-byte word at the returned
         * offset, so all 4 bytes -- not just the first -- must be
         * file-backed; checked in 64 bits so the addition can't wrap */
        if (vaddr >= segs[i].vaddr
         && (uint64_t)(vaddr - segs[i].vaddr) + 4 <= segs[i].filesz)
        {
            *out_offset = segs[i].offset + (vaddr - segs[i].vaddr);
            return 1;
        }
    }

    return 0;
}

/* report whether the whole 4-byte slot at vaddr lies within the SAME
 * covering segment's zero-filled tail (i.e. entirely at or past its
 * p_filesz, and entirely within its p_memsz) -- the one case where a
 * RELA+RELATIVE zero addend needs no write, since the loader's own
 * zero-fill already provides exactly that value for every one of the 4
 * bytes. A slot that straddles the file/zero-fill boundary (some bytes
 * file-backed, some not) is deliberately NOT covered by this or by
 * try_vaddr_to_file_offset() above: elf_pgmld() copies whatever those
 * leading file bytes actually contain (not necessarily zero) and only
 * zero-fills the rest, so neither "materialise into file bytes" nor
 * "trust the zero-fill" is correct for it -- the caller must reject it
 * outright rather than silently pick one and risk a corrupted value. */
static int slot_fully_zero_filled(const SEGMENT *segs, size_t nsegs,
                                  uint32_t vaddr)
{
    size_t i;

    for (i = 0; i < nsegs; i++)
    {
        if (vaddr >= segs[i].vaddr
         && (uint64_t)(vaddr - segs[i].vaddr) >= segs[i].filesz
         && (uint64_t)(vaddr - segs[i].vaddr) + 4 <= segs[i].memsz)
            return 1;
    }
    return 0;
}

static uint32_t vaddr_to_file_offset(const SEGMENT *segs, size_t nsegs,
                                     uint32_t vaddr)
{
    uint32_t file_off;

    if (try_vaddr_to_file_offset(segs, nsegs, vaddr, &file_off))
        return file_off;

    die("relocation at 0x%08lx targets memory with no file backing "
        "(likely .bss); a slot there needs an addend-carrying wire "
        "operation .ptos.reloc version 1 does not have -- a known format "
        "limitation, not a bug (see doc/elfload.txt)",
        (unsigned long)vaddr);
    return 0; /* unreachable */
}

/* resolve a relocation's symbol (r_info's top 24 bits, per ELF32_R_SYM)
 * against the section's linked symbol table and report whether it moves
 * with the image -- i.e. resolves to an ordinary allocated section, so
 * "+= bias" (DIR32) or "no fixup" (a PC-relative allowlist type) is
 * actually correct for it. Anything else -- SHN_ABS (a fixed,
 * linker-defined constant), SHN_UNDEF/SHN_COMMON (should not survive
 * into a fully linked ET_EXEC/ET_DYN at all, but checked rather than
 * trusted), any other reserved index, an out of range section index, or
 * a valid section with no SHF_ALLOC (not part of the loaded image) --
 * does not move the same way P does under a uniform load bias, so is
 * reported as not load-relative.
 *
 * This function is never called for RELATIVE relocations (their symbol
 * field is conventionally unused and exempted by the caller before
 * reaching here); every relocation that does reach it -- DIR32 and the
 * PC-relative no-fixup allowlist -- is expected to name a real symbol, so
 * symbol index 0 (STN_UNDEF) here means exactly what it says: unresolved,
 * not "field unused by convention". Likewise a relocation section with no
 * linked symbol table at all cannot prove anything about a symbol it
 * cannot look up. Both are therefore reported as not load-relative rather
 * than assumed safe.
 *
 * SHF_ALLOC alone is not enough either: a valid ELF can have an allocated
 * section that no PT_LOAD actually covers (e.g. a PT_TLS-only section, or
 * one a linker script leaves outside every segment) -- bdos/elfld.c's own
 * elf_fixup()/elf_scan() only ever look inside PT_LOAD ranges, so such a
 * symbol does not move under the load bias the way an ordinary in-image
 * one does. range_covered_by_load() checks the symbol's whole section
 * against the PT_LOAD segments already scanned in main(). */
static int reloc_symbol_is_load_relative(const char *in_path, uint32_t r_info,
                                         uint32_t symtab_offset, uint32_t symtab_size,
                                         uint32_t symtab_entsize, uint32_t in_size,
                                         const unsigned char *in, uint32_t e_shoff,
                                         uint16_t e_shnum, uint16_t e_shentsize,
                                         const SEGMENT *segs, size_t nsegs)
{
    uint32_t sym_index = r_info >> 8;
    uint32_t sym_off;
    uint16_t st_shndx;
    const unsigned char *sh;
    uint32_t sh_addr, sh_size;

    if (sym_index == 0 || symtab_offset == 0)
        return 0;

    if (sym_index >= symtab_size / symtab_entsize)
        die("'%s' has a relocation naming an out of range symbol table "
            "entry", in_path);
    if ((uint64_t)symtab_offset + (uint64_t)sym_index * symtab_entsize + SYM_SIZE
        > in_size)
        die("'%s' has a truncated symbol table", in_path);

    sym_off = symtab_offset + sym_index * symtab_entsize;
    st_shndx = rd16(in + sym_off + SYM_ST_SHNDX);

    if (st_shndx == SHN_UNDEF || st_shndx >= SHN_LORESERVE)
        return 0;   /* SHN_ABS/SHN_COMMON/any other reserved index included */
    if (st_shndx >= e_shnum)
        return 0;   /* malformed: treat as unsafe rather than trust it */

    if ((uint64_t)e_shoff + (uint64_t)st_shndx * e_shentsize + SHDR_SIZE > in_size)
        die("'%s' has a truncated section header table", in_path);
    sh = in + e_shoff + (uint32_t)st_shndx * e_shentsize;

    if ((rd32(sh + SHDR_SH_FLAGS) & SHF_ALLOC) == 0)
        return 0;

    sh_addr = rd32(sh + SHDR_SH_ADDR);
    sh_size = rd32(sh + SHDR_SH_SIZE);
    return range_covered_by_load(segs, nsegs, sh_addr, sh_size);
}

/* read a NUL-terminated C string starting at file offset 'off', bounds
 * checked against in_size. Returns a pointer directly into 'in' (valid
 * for the rest of this tool's run, since 'in' is never freed before
 * exit) -- dies rather than return an unterminated or out-of-range
 * string, matching every other malformed-input path in this file. */
static const char *read_c_string(const unsigned char *in, uint32_t limit,
                                 uint32_t off, const char *what,
                                 const char *in_path)
{
    uint32_t i;

    /* 'limit' is the end of the specific table this string is supposed to
     * live in (e.g. strtab_off + strtab_size), not just the end of the
     * file: scanning all the way to in_size would let a malformed table
     * with no NUL before its own declared end "borrow" a NUL from
     * whatever unrelated content follows it in the file, accepting a
     * name that was never actually inside the table it claims to be. */
    if (off >= limit)
        die("'%s' has a %s string offset past the end of its table", in_path, what);

    for (i = off; i < limit; i++)
    {
        if (in[i] == '\0')
            return (const char *)(in + off);
    }

    die("'%s' has a %s string that is not NUL-terminated within its table",
        in_path, what);
    return NULL; /* unreachable */
}

/* recognise "libptos-abi.so.<N>" for a decimal N and extract N, bounded
 * well under the wire format's UWORD abi_major field so a pathological
 * name cannot silently wrap; anything else (wrong prefix, no digits,
 * non-digit characters, out of range) is simply not recognised. */
static int parse_abi_soname(const char *name, uint32_t *out_major)
{
    static const char prefix[] = PTOS_ABI_SONAME_PREFIX;
    size_t plen = sizeof(prefix) - 1;
    const char *p;
    uint32_t val;

    if (strncmp(name, prefix, plen) != 0)
        return 0;

    p = name + plen;
    if (*p == '\0')
        return 0;

    val = 0;
    for (; *p != '\0'; p++)
    {
        if (*p < '0' || *p > '9')
            return 0;
        if (val > 6553U)   /* about to exceed 65535 on the next digit */
            return 0;
        val = val * 10 + (uint32_t)(*p - '0');
    }
    if (val > 65535U)
        return 0;

    *out_major = val;
    return 1;
}

/*
 * find_abi_needed - look for the SDK's libptos-abi.so.<N> dependency.
 *
 * Scans every SHT_DYNAMIC section (there is normally at most one) for
 * DT_NEEDED entries. No .dynamic section, or one with no DT_NEEDED
 * entries at all, is a plain binary that imports nothing: *out_recognized
 * is set to 0 and the function returns normally. Exactly one DT_NEEDED
 * naming "libptos-abi.so.<N>" is the only other accepted shape, setting
 * *out_recognized to 1 and *out_abi_major to N. Anything else -- more
 * than one DT_NEEDED, or one that does not match the pattern -- dies:
 * pTOS has no general purpose dynamic linker, so a real shared library
 * dependency can never be satisfied at load time (doc/elfload.txt).
 */
static void find_abi_needed(const unsigned char *in, uint32_t in_size,
                            uint32_t e_shoff, uint16_t e_shnum,
                            uint16_t e_shentsize, const char *in_path,
                            int *out_recognized, uint32_t *out_abi_major)
{
    uint16_t i;
    int needed_count = 0;
    uint32_t major = 0;

    for (i = 0; i < e_shnum; i++)
    {
        const unsigned char *sh;
        uint32_t sh_type, sh_offset, sh_size, sh_link;
        uint32_t strtab_off, strtab_size, strtab_limit;
        uint32_t count, j;

        if ((uint64_t)e_shoff + (uint64_t)i * e_shentsize + SHDR_SIZE > in_size)
            die("'%s' has a truncated section header table", in_path);
        sh = in + e_shoff + (uint32_t)i * e_shentsize;
        sh_type = rd32(sh + SHDR_SH_TYPE);
        if (sh_type != SHT_DYNAMIC)
            continue;

        sh_offset = rd32(sh + SHDR_SH_OFFSET);
        sh_size = rd32(sh + SHDR_SH_SIZE);
        sh_link = rd32(sh + SHDR_SH_LINK);

        if (sh_size % DYN_SIZE != 0)
            die("'%s' has a malformed .dynamic section", in_path);

        if (sh_link >= e_shnum)
            die("'%s' has a .dynamic section naming an out of range "
                "string table", in_path);
        {
            const unsigned char *strsh;

            if ((uint64_t)e_shoff + (uint64_t)sh_link * e_shentsize + SHDR_SIZE > in_size)
                die("'%s' has a truncated section header table", in_path);
            strsh = in + e_shoff + sh_link * e_shentsize;
            strtab_off = rd32(strsh + SHDR_SH_OFFSET);
            strtab_size = rd32(strsh + SHDR_SH_SIZE);
        }

        if ((uint64_t)strtab_off + (uint64_t)strtab_size > in_size)
            die("'%s' has a .dynamic string table extending past end of file",
                in_path);
        strtab_limit = strtab_off + strtab_size;

        count = sh_size / DYN_SIZE;
        for (j = 0; j < count; j++)
        {
            const unsigned char *ent;
            uint32_t d_tag, d_val;
            const char *name;

            if ((uint64_t)sh_offset + (uint64_t)j * DYN_SIZE + DYN_SIZE > in_size)
                die("'%s' has a truncated .dynamic section", in_path);
            ent = in + sh_offset + j * DYN_SIZE;

            d_tag = rd32(ent + 0);
            if (d_tag != DT_NEEDED)
                continue;
            d_val = rd32(ent + 4);

            if (d_val >= strtab_size)
                die("'%s' has a DT_NEEDED entry pointing outside its "
                    "string table", in_path);
            name = read_c_string(in, strtab_limit, strtab_off + d_val,
                                 "DT_NEEDED", in_path);

            needed_count++;
            if (!parse_abi_soname(name, &major))
                die("'%s' needs '%s', which is not the pTOS ABI stub "
                    "(expected \"%s<N>\"); pTOS has no general purpose "
                    "dynamic linker, so this dependency can never be "
                    "satisfied at load time", in_path, name,
                    PTOS_ABI_SONAME_PREFIX);
        }
    }

    if (needed_count > 1)
        die("'%s' needs more than one shared library; only a single "
            "\"%s<N>\" pTOS ABI stub dependency is supported",
            in_path, PTOS_ABI_SONAME_PREFIX);

    *out_recognized = (needed_count == 1);
    *out_abi_major = major;
}

/*
 * symbol_is_undefined - lightweight peek at whether a relocation's
 * symbol (r_info's top 24 bits) is SHN_UNDEF, without validating or
 * dying on anything -- used only to decide whether an otherwise-DIR32
 * relocation should be routed to the import path below instead of the
 * ordinary internal-relocation path; any malformed input this misreads
 * as "not undefined" still gets fully validated by whichever of the two
 * paths actually ends up handling it.
 */
static int symbol_is_undefined(uint32_t r_info, uint32_t symtab_offset,
                               uint32_t symtab_size, uint32_t symtab_entsize,
                               uint32_t in_size, const unsigned char *in)
{
    uint32_t sym_index = r_info >> 8;
    uint32_t sym_off;

    if (sym_index == 0 || symtab_offset == 0 || symtab_entsize == 0)
        return 0;
    if (sym_index >= symtab_size / symtab_entsize)
        return 0;
    if ((uint64_t)symtab_offset + (uint64_t)sym_index * symtab_entsize + SYM_SIZE
        > in_size)
        return 0;

    sym_off = symtab_offset + sym_index * symtab_entsize;
    return rd16(in + sym_off + SYM_ST_SHNDX) == SHN_UNDEF;
}

/*
 * resolve_dynamic_import - resolve a JUMP_SLOT/GLOB_DAT relocation's
 * symbol (r_info's top 24 bits) into a "gemdos:<name>" import name and
 * its kind, given the relocation section's own already-resolved linked
 * symbol table (symtab_offset/size/entsize, exactly as computed for
 * every other relocation section in main()'s loop).
 *
 * Only ever called once the caller has already decided this relocation
 * is a pTOS ABI import candidate: either its type is JUMP_SLOT/GLOB_DAT
 * (which exclusively arise from real dynamic-symbol references, never
 * something --emit-relocs produces for an internal branch/call), or it
 * is DIR32 AND the caller's own symbol_is_undefined() check found an
 * undefined symbol (DIR32 alone is not exclusive to imports -- it is
 * also the ordinary internal-relocation type -- see the call site).
 * Either way, every case this function itself rejects from here on is a
 * genuine packaging problem, not a plain binary's ordinary relocation
 * happening to look similar.
 */
static const char *resolve_dynamic_import(const char *in_path, uint32_t r_info,
                                          uint32_t symtab_offset, uint32_t symtab_size,
                                          uint32_t symtab_entsize, uint32_t in_size,
                                          const unsigned char *in, uint32_t e_shoff,
                                          uint16_t e_shnum, uint16_t e_shentsize,
                                          uint32_t sh_link, int *out_kind)
{
    uint32_t sym_index = r_info >> 8;
    uint32_t sym_off;
    uint32_t st_name;
    uint16_t st_shndx;
    uint8_t st_info;
    uint32_t st_type;
    const unsigned char *symsh;
    uint32_t strtab_off, strtab_size;
    const char *name;

    if (sym_index == 0 || symtab_offset == 0)
        die("'%s' has a dynamic-symbol relocation with no symbol", in_path);

    if (sym_index >= symtab_size / symtab_entsize)
        die("'%s' has a dynamic-symbol relocation naming an out of range "
            "symbol table entry", in_path);
    if ((uint64_t)symtab_offset + (uint64_t)sym_index * symtab_entsize + SYM_SIZE
        > in_size)
        die("'%s' has a truncated dynamic symbol table", in_path);

    sym_off = symtab_offset + sym_index * symtab_entsize;
    st_name = rd32(in + sym_off + SYM_ST_NAME);
    st_info = in[sym_off + SYM_ST_INFO];
    st_shndx = rd16(in + sym_off + SYM_ST_SHNDX);

    if (st_shndx != SHN_UNDEF)
        die("'%s' has a dynamic-symbol relocation against a symbol that "
            "is not undefined; only an import from the pTOS ABI stub "
            "(always undefined in the input) is expected here", in_path);

    st_type = (uint32_t)(st_info & 0xfU);
    if (st_type == STT_FUNC)
        *out_kind = PTOSABI_KIND_FUNCTION;
    else if (st_type == STT_OBJECT)
        *out_kind = PTOSABI_KIND_DATA;
    else
        die("'%s' imports a symbol that is neither a function nor a data "
            "object (ELF symbol type %lu); cannot classify it as a pTOS "
            "ABI import", in_path, (unsigned long)st_type);

    /* sh_link is the symtab SECTION's own index; its sh_link in turn
     * names the string table st_name is an offset into */
    if (sh_link >= e_shnum)
        die("'%s' has a relocation section naming an out of range "
            "symbol table", in_path);
    if ((uint64_t)e_shoff + (uint64_t)sh_link * e_shentsize + SHDR_SIZE > in_size)
        die("'%s' has a truncated section header table", in_path);
    symsh = in + e_shoff + sh_link * e_shentsize;
    {
        uint32_t str_shndx = rd32(symsh + SHDR_SH_LINK);
        const unsigned char *strsh;

        if (str_shndx >= e_shnum)
            die("'%s' has a symbol table naming an out of range string "
                "table", in_path);
        if ((uint64_t)e_shoff + (uint64_t)str_shndx * e_shentsize + SHDR_SIZE > in_size)
            die("'%s' has a truncated section header table", in_path);
        strsh = in + e_shoff + str_shndx * e_shentsize;
        strtab_off = rd32(strsh + SHDR_SH_OFFSET);
        strtab_size = rd32(strsh + SHDR_SH_SIZE);
    }

    if ((uint64_t)strtab_off + (uint64_t)strtab_size > in_size)
        die("'%s' has a dynamic string table extending past end of file",
            in_path);

    if (st_name >= strtab_size)
        die("'%s' has a dynamic symbol with a name offset past the end "
            "of its string table", in_path);
    name = read_c_string(in, strtab_off + strtab_size, strtab_off + st_name,
                         "dynamic symbol", in_path);
    if (name[0] == '\0')
        die("'%s' imports a symbol with an empty name", in_path);

    return name;
}

/* append (or find an existing, identical) import; returns its index.
 * "gemdos:" is prefixed here rather than carried by the caller, since
 * this version recognises exactly one namespace (see find_abi_needed());
 * a later stage adding more (aes:, vdi:, ...) would pass the namespace
 * in instead of hardcoding it here. */
static uint32_t import_list_add(IMPORT **imports, size_t *nimports, size_t *cap,
                                const char *sym_name, uint32_t abi_major,
                                int kind, const char *in_path)
{
    size_t i;
    char *full_name;

    if (strlen("gemdos:") + strlen(sym_name) > PTOS_IMPORT_NAME_MAX)
        die("'%s' imports a symbol whose full \"gemdos:%s\" name is %lu "
            "bytes, past the loader's own PTOS_IMPORT_NAME_MAX (%u); "
            "refusing rather than pack a binary Pexec() would always "
            "reject", in_path, sym_name,
            (unsigned long)(strlen("gemdos:") + strlen(sym_name)),
            PTOS_IMPORT_NAME_MAX);

    full_name = malloc(strlen("gemdos:") + strlen(sym_name) + 1);
    if (!full_name)
        die("out of memory");
    strcpy(full_name, "gemdos:");
    strcat(full_name, sym_name);

    for (i = 0; i < *nimports; i++)
    {
        if ((*imports)[i].abi_major == abi_major
         && strcmp((*imports)[i].name, full_name) == 0)
        {
            free(full_name);
            return (uint32_t)i;
        }
    }

    if (*nimports == *cap)
    {
        *cap = *cap ? *cap * 2 : 16;
        *imports = realloc(*imports, *cap * sizeof(**imports));
        if (!*imports)
            die("out of memory");
    }

    (*imports)[*nimports].name = full_name;
    (*imports)[*nimports].abi_major = abi_major;
    (*imports)[*nimports].abi_minor = 0;
    (*imports)[*nimports].kind = kind;
    return (uint32_t)(*nimports)++;
}

static void bind_list_add(BIND **binds, size_t *nbinds, size_t *cap,
                          uint32_t import_index, uint32_t slot_vaddr, int bind_op)
{
    if (*nbinds == *cap)
    {
        *cap = *cap ? *cap * 2 : 16;
        *binds = realloc(*binds, *cap * sizeof(**binds));
        if (!*binds)
            die("out of memory");
    }

    (*binds)[*nbinds].import_index = import_index;
    (*binds)[*nbinds].slot_vaddr = slot_vaddr;
    (*binds)[*nbinds].bind_op = bind_op;
    (*nbinds)++;
}

static int slot_cmp(const void *a, const void *b)
{
    uint32_t va = ((const SLOT *)a)->vaddr;
    uint32_t vb = ((const SLOT *)b)->vaddr;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

int main(int argc, char **argv)
{
    const char *in_path, *out_path;
    unsigned char *in;
    long in_size_l;
    uint32_t in_size;
    unsigned char e_ident_class, e_ident_data;
    uint32_t e_type, e_machine;
    uint32_t e_phoff, e_shoff, e_entry;
    uint16_t e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
    uint32_t dir32_type, relative_type, jump_slot_type, glob_dat_type;
    int have_abi_needed;
    uint32_t abi_major;
    IMPORT *imports;
    size_t nimports, import_cap;
    BIND *binds;
    size_t nbinds, bind_cap;
    SEGMENT *segs;
    size_t nsegs, seg_cap;
    SLOT *slots;
    size_t nslots, slot_cap;
    uint32_t link_base;
    int have_link_base;
    uint16_t i;
    FILE *out;
    BUF payload;
    BUF newphdrs;
    BUF imports_payload;
    unsigned char hdrbuf[8];
    uint32_t new_data_off, padded_len, new_phdr_off;
    uint32_t imports_data_off, imports_padded_len;
    uint32_t new_phnum;
    int strip_shdr;
    int allow_no_relocations;
    int argi;

    g_argv0 = argv[0] ? argv[0] : "ptos-elf-pack";

    strip_shdr = 0;
    allow_no_relocations = 0;
    argi = 1;
    while (argi < argc && argv[argi][0] == '-')
    {
        if (strcmp(argv[argi], "--strip-shdr") == 0)
            strip_shdr = 1;
        else if (strcmp(argv[argi], "--allow-no-relocations") == 0)
            allow_no_relocations = 1;
        else
            die("usage: %s [--strip-shdr] [--allow-no-relocations] "
                "<input.elf> <output.elf>", g_argv0);
        argi++;
    }

    if (argc - argi != 2)
        die("usage: %s [--strip-shdr] [--allow-no-relocations] "
            "<input.elf> <output.elf>", g_argv0);

    in_path = argv[argi];
    out_path = argv[argi + 1];

    in = read_file(in_path, &in_size_l);
    if (in_size_l < EHDR_SIZE)
        die("'%s' is too small to be a valid ELF32 file", in_path);
    /* every offset from here on is a plain uint32_t (matching the ELF32
     * field widths themselves), so an input too large to fit one would
     * silently truncate rather than fail loudly; comparing as unsigned
     * long is correct whether long is 32 or 64 bits (on a 32-bit host
     * this can never trip, since a 32-bit long's range is already within
     * uint32_t) */
    if ((unsigned long)in_size_l > 0xffffffffUL)
        die("'%s' is larger than the 4 GiB ELF32 format can address", in_path);
    in_size = (uint32_t)in_size_l;

    if (in[0] != 0x7f || in[1] != 'E' || in[2] != 'L' || in[3] != 'F')
        die("'%s' is not an ELF file", in_path);

    e_ident_class = in[EI_CLASS];
    e_ident_data = in[EI_DATA];
    if (e_ident_class != ELFCLASS32)
        die("'%s' is not ELF32", in_path);
    if (e_ident_data != ELFDATA2LSB && e_ident_data != ELFDATA2MSB)
        die("'%s' has an unrecognised ELF data encoding", in_path);
    g_is_be = (e_ident_data == ELFDATA2MSB);

    e_type = rd16(in + EHDR_E_TYPE);
    e_machine = rd16(in + EHDR_E_MACHINE);
    e_entry = rd32(in + EHDR_E_ENTRY);
    e_phoff = rd32(in + EHDR_E_PHOFF);
    e_shoff = rd32(in + EHDR_E_SHOFF);
    e_phentsize = rd16(in + EHDR_E_PHENTSIZE);
    e_phnum = rd16(in + EHDR_E_PHNUM);
    e_shentsize = rd16(in + EHDR_E_SHENTSIZE);
    e_shnum = rd16(in + EHDR_E_SHNUM);
    e_shstrndx = rd16(in + EHDR_E_SHSTRNDX);
    (void)e_entry;
    (void)e_shstrndx;

    if (e_type != ET_EXEC && e_type != ET_DYN)
        die("'%s' is neither ET_EXEC nor ET_DYN", in_path);

    if (e_machine == EM_ARM)
    {
        dir32_type = 2;      /* R_ARM_ABS32 */
        relative_type = 23;  /* R_ARM_RELATIVE */
        glob_dat_type = 21;  /* R_ARM_GLOB_DAT */
        jump_slot_type = 22; /* R_ARM_JUMP_SLOT */
    }
    else if (e_machine == EM_68K)
    {
        dir32_type = 1;      /* R_68K_32 */
        glob_dat_type = 20;  /* R_68K_GLOB_DAT */
        jump_slot_type = 21; /* R_68K_JMP_SLOT */
        relative_type = 22;  /* R_68K_RELATIVE */
    }
    else
        die("'%s' targets an unsupported machine (%u); only ARM and m68k "
            "are known to bdos/elfld.c", in_path, (unsigned)e_machine);

    if (e_phentsize != PHDR_SIZE)
        die("'%s' has an unexpected program header size", in_path);
    if (e_phnum == 0)
        die("'%s' has no program headers", in_path);

    /* scan PT_LOAD segments: needed both for vaddr_to_file_offset() below
     * and, for informational purposes only, the image's link_base */
    seg_cap = e_phnum;
    segs = malloc(seg_cap * sizeof(*segs));
    if (!segs)
        die("out of memory");
    nsegs = 0;
    have_link_base = 0;
    link_base = 0;

    for (i = 0; i < e_phnum; i++)
    {
        const unsigned char *ph;
        uint32_t p_type, p_offset, p_vaddr, p_filesz, p_memsz;

        if ((uint64_t)e_phoff + (uint64_t)i * e_phentsize + PHDR_SIZE > in_size)
            die("'%s' has a truncated program header table", in_path);
        ph = in + e_phoff + (uint32_t)i * e_phentsize;

        p_type = rd32(ph + PHDR_P_TYPE);
        if (p_type == PT_PTOS_RELOC)
            die("'%s' already has a PT_PTOS_RELOC segment -- already "
                "packed? Run ptos-elf-pack against the original, unpacked "
                "ELF instead; packing an already-packed file would append "
                "a second .ptos.reloc stream and PT_PTOS_RELOC entry, "
                "which this format has no defined meaning for", in_path);
        if (p_type != PT_LOAD)
            continue;

        p_offset = rd32(ph + PHDR_P_OFFSET);
        p_vaddr = rd32(ph + PHDR_P_VADDR);
        p_filesz = rd32(ph + PHDR_P_FILESZ);
        p_memsz = rd32(ph + PHDR_P_MEMSZ);

        if ((uint64_t)p_offset + p_filesz > in_size)
            die("'%s' has a PT_LOAD segment reaching past end of file", in_path);
        if (p_memsz < p_filesz)
            die("'%s' has a PT_LOAD segment with p_memsz < p_filesz", in_path);

        segs[nsegs].vaddr = p_vaddr;
        segs[nsegs].offset = p_offset;
        segs[nsegs].filesz = p_filesz;
        segs[nsegs].memsz = p_memsz;
        nsegs++;

        if (!have_link_base || p_vaddr < link_base)
        {
            link_base = p_vaddr;
            have_link_base = 1;
        }
    }

    if (!have_link_base)
        die("'%s' has no PT_LOAD segments", in_path);

    if (e_shoff == 0 || e_shnum == 0)
        die("'%s' has no section headers to read relocations from -- "
            "already packed, or stripped before packing?", in_path);
    if (e_shentsize != SHDR_SIZE)
        die("'%s' has an unexpected section header size", in_path);

    find_abi_needed(in, in_size, e_shoff, e_shnum, e_shentsize, in_path,
                    &have_abi_needed, &abi_major);

    imports = NULL;
    nimports = 0;
    import_cap = 0;
    binds = NULL;
    nbinds = 0;
    bind_cap = 0;

    /* copy the whole input file through unchanged; RELA/RELATIVE entries
     * may still patch a few bytes of it below (pack_addend) */
    {
        BUF out_copy;
        out_copy.data = malloc(in_size);
        if (!out_copy.data)
            die("out of memory");
        memcpy(out_copy.data, in, in_size);
        out_copy.len = in_size;
        out_copy.cap = in_size;
        free(in);
        in = out_copy.data;
        /* 'in' is now the mutable output copy; in_size unchanged */
    }

    slot_cap = 64;
    nslots = 0;
    slots = malloc(slot_cap * sizeof(*slots));
    if (!slots)
        die("out of memory");

    for (i = 0; i < e_shnum; i++)
    {
        const unsigned char *sh;
        uint32_t sh_type, sh_offset, sh_size, sh_link, sh_info, sh_entsize;
        uint32_t entsize, structsize, count, j;
        uint32_t symtab_offset, symtab_size, symtab_entsize;
        int rela;

        if ((uint64_t)e_shoff + (uint64_t)i * e_shentsize + SHDR_SIZE > in_size)
            die("'%s' has a truncated section header table", in_path);
        sh = in + e_shoff + (uint32_t)i * e_shentsize;

        sh_type = rd32(sh + SHDR_SH_TYPE);
        if (sh_type == SHT_RELR)
            die("'%s' has an SHT_RELR compact relocation section; "
                "ptos-elf-pack only understands SHT_REL/SHT_RELA -- relink "
                "without whatever produced packed/relative-only relocations "
                "(e.g. a linker's --pack-dyn-relocs=relr) before packing",
                in_path);
        if (sh_type == SHT_ANDROID_REL || sh_type == SHT_ANDROID_RELA)
            die("'%s' has an SHT_ANDROID_REL/SHT_ANDROID_RELA packed "
                "relocation section; ptos-elf-pack only understands "
                "SHT_REL/SHT_RELA -- relink without whatever produced "
                "packed relocations (e.g. a linker's "
                "--pack-dyn-relocs=android) before packing", in_path);
        if (sh_type != SHT_REL && sh_type != SHT_RELA)
            continue;

        rela = (sh_type == SHT_RELA);
        sh_offset = rd32(sh + SHDR_SH_OFFSET);
        sh_size = rd32(sh + SHDR_SH_SIZE);
        sh_link = rd32(sh + SHDR_SH_LINK);
        sh_info = rd32(sh + SHDR_SH_INFO);
        sh_entsize = rd32(sh + SHDR_SH_ENTSIZE);

        /* resolve this relocation section's symbol table (sh_link) once,
         * so the per-entry loop below can reject a PC-relative relocation
         * against an SHN_ABS symbol (see the loop for why) without
         * re-deriving this on every entry */
        symtab_offset = 0;
        symtab_size = 0;
        symtab_entsize = SYM_SIZE;
        if (sh_link != 0)
        {
            const unsigned char *symsh;

            if (sh_link >= e_shnum)
                die("'%s' has a relocation section naming an out of range "
                    "symbol table", in_path);
            if ((uint64_t)e_shoff + (uint64_t)sh_link * e_shentsize + SHDR_SIZE > in_size)
                die("'%s' has a truncated section header table", in_path);

            symsh = in + e_shoff + sh_link * e_shentsize;
            symtab_offset = rd32(symsh + SHDR_SH_OFFSET);
            symtab_size = rd32(symsh + SHDR_SH_SIZE);
            symtab_entsize = rd32(symsh + SHDR_SH_ENTSIZE);
            if (symtab_entsize == 0)
                symtab_entsize = SYM_SIZE;
            if (symtab_entsize != SYM_SIZE)
                die("'%s' has a symbol table with an unexpected entry size", in_path);
        }

        /* sh_info names the target section; 0 means "the whole image"
         * (used by .rel.dyn/.rela.dyn), matching bdos/elfld.c exactly */
        if (sh_info != 0)
        {
            const unsigned char *tsh;
            uint32_t t_flags;

            if (sh_info >= e_shnum)
                die("'%s' has a relocation section naming an out of range "
                    "target section", in_path);
            if ((uint64_t)e_shoff + (uint64_t)sh_info * e_shentsize + SHDR_SIZE > in_size)
                die("'%s' has a truncated section header table", in_path);

            tsh = in + e_shoff + sh_info * e_shentsize;
            t_flags = rd32(tsh + SHDR_SH_FLAGS);
            if (!(t_flags & SHF_ALLOC))
                continue;
        }

        structsize = rela ? RELA_SIZE : REL_SIZE;
        entsize = sh_entsize ? sh_entsize : structsize;
        if (entsize != structsize || sh_size % entsize != 0)
            die("'%s' has a malformed relocation section", in_path);

        count = sh_size / entsize;
        for (j = 0; j < count; j++)
        {
            const unsigned char *ent;
            uint32_t r_offset, r_info, type, addend;

            if ((uint64_t)sh_offset + (uint64_t)j * entsize + structsize > in_size)
                die("'%s' has a truncated relocation table", in_path);
            ent = in + sh_offset + j * entsize;

            r_offset = rd32(ent + 0);
            r_info = rd32(ent + 4);
            type = r_info & 0xffUL;
            addend = rela ? rd32(ent + 8) : 0;

            /* R_*_JUMP_SLOT/R_*_GLOB_DAT exclusively arise from a real
             * dynamic-symbol reference -- never something --emit-relocs
             * produces for an internal branch/call -- so these two types
             * are always the pTOS ABI import mechanism's own. A DIR32
             * against an undefined symbol is the same thing in disguise:
             * this m68k toolchain has no -fPIC/-fPIE code generation, so
             * a call to an imported function compiles to a plain
             * absolute reference (an ordinary R_68K_32 relocation on the
             * call's own address operand) rather than a PLT-relative
             * call -- confirmed directly against this toolchain's actual
             * "-pie --no-dynamic-linker" output, which emits exactly this
             * shape (R_68K_32 in .rela.dyn) alongside an R_68K_JMP_SLOT
             * PLT entry the generated code never actually references.
             * ARM's PIC-capable codegen does not produce this shape (only
             * JUMP_SLOT/GLOB_DAT), but the check costs nothing there. Any
             * of these three types is handled here instead of falling
             * into the DIR32/RELATIVE/no-fixup dispatch below
             * (doc/elfload.txt's "Native pTOS ABI imports" section). */
            if (type == jump_slot_type || type == glob_dat_type
             || (type == dir32_type
              && symbol_is_undefined(r_info, symtab_offset, symtab_size,
                                     symtab_entsize, in_size, in)))
            {
                const char *sym_name;
                int kind;
                int bind_op;
                uint32_t import_index;

                if (!have_abi_needed)
                    die("'%s' has a dynamic-symbol relocation (type %lu) "
                        "at 0x%08lx but no \"%s<N>\" DT_NEEDED dependency; "
                        "pTOS has no general purpose dynamic linker, so "
                        "an unresolved dynamic reference can never be "
                        "satisfied at load time", in_path,
                        (unsigned long)type, (unsigned long)r_offset,
                        PTOS_ABI_SONAME_PREFIX);

                /* the bind mechanism has no addend component in version 1
                 * (doc/elfload.txt): it writes the resolved import address
                 * as-is, so a nonzero addend here would be silently
                 * dropped rather than applied.
                 *
                 * This only needs checking for the DIR32-as-import case
                 * (m68k's plain absolute reference to an imported
                 * function/object): RELA carries a real addend in
                 * r_addend (already decoded above), and REL keeps one
                 * embedded in the slot's own bytes exactly like an
                 * ordinary DIR32/RELATIVE slot does. JUMP_SLOT/GLOB_DAT
                 * are different: under REL (ARM), the linker
                 * conventionally pre-fills that slot with a PLT-stub or
                 * resolver placeholder address, not an addend -- verified
                 * directly against this toolchain's actual PIE output, a
                 * real R_ARM_JUMP_SLOT slot pre-filled with a nonzero PLT
                 * address, not zero -- and RELA-encoded JUMP_SLOT/GLOB_DAT
                 * (m68k) already carries r_addend 0 by convention in
                 * every fixture built for this PR. So checking a
                 * JUMP_SLOT/GLOB_DAT slot's bytes the same way DIR32's
                 * are would reject perfectly ordinary PLT/GOT output. */
                if (type == dir32_type)
                {
                    if (rela)
                    {
                        if (addend != 0)
                            die("'%s' has a dynamic-symbol relocation at "
                                "0x%08lx with a nonzero addend (0x%08lx); "
                                "pTOS ABI imports do not support one",
                                in_path, (unsigned long)r_offset,
                                (unsigned long)addend);
                    }
                    else
                    {
                        uint32_t slot_off;
                        uint32_t slot_val;

                        if (!try_vaddr_to_file_offset(segs, nsegs, r_offset, &slot_off))
                            die("'%s' has a dynamic-symbol relocation at "
                                "0x%08lx with no file backing to read its "
                                "implicit addend from", in_path,
                                (unsigned long)r_offset);
                        slot_val = rd32(in + slot_off);
                        if (slot_val != 0)
                            die("'%s' has a dynamic-symbol relocation at "
                                "0x%08lx with a nonzero embedded addend "
                                "(0x%08lx); pTOS ABI imports do not support "
                                "one", in_path, (unsigned long)r_offset,
                                (unsigned long)slot_val);
                    }
                }

                sym_name = resolve_dynamic_import(in_path, r_info, symtab_offset,
                                                  symtab_size, symtab_entsize,
                                                  in_size, in, e_shoff, e_shnum,
                                                  e_shentsize, sh_link, &kind);

                if (type == jump_slot_type)
                    bind_op = PTOS_BIND_CODE_ADDRESS;
                else if (kind == PTOSABI_KIND_FUNCTION)
                    bind_op = PTOS_BIND_CODE_ADDRESS;
                else
                    bind_op = PTOS_BIND_DATA_ADDRESS;

                import_index = import_list_add(&imports, &nimports, &import_cap,
                                               sym_name, abi_major, kind, in_path);
                bind_list_add(&binds, &nbinds, &bind_cap, import_index,
                             r_offset, bind_op);

                continue;
            }

            if (type != dir32_type && type != relative_type)
            {
                if (type_needs_no_fixup(e_machine, type))
                {
                    /* A PC-relative value (S + A - P) is invariant under a
                     * uniform image shift only when its symbol S moves
                     * with the image, i.e. resolves to an ordinary
                     * allocated section. Anything else (a fixed SHN_ABS
                     * constant, an unresolved/common symbol, one in a
                     * non-allocated section, ...) breaks that: shifting P
                     * without shifting S changes the computed value, so
                     * this would need a fixup this format has no way to
                     * apply, not none at all. Look the symbol up and
                     * reject rather than assume the common case -- except
                     * for the couple of types whose symbol field the ABI
                     * itself defines as meaningless (R_ARM_NONE/R_68K_NONE,
                     * R_ARM_V4BX), which never named a symbol to check in
                     * the first place. */
                    if (!type_symbol_field_is_meaningless(e_machine, type)
                     && !reloc_symbol_is_load_relative(in_path, r_info, symtab_offset,
                                                       symtab_size, symtab_entsize,
                                                       in_size, in, e_shoff, e_shnum,
                                                       e_shentsize, segs, nsegs))
                        die("'%s' has a PC-relative relocation (type %lu) "
                            "at 0x%08lx against a symbol that does not "
                            "move with the image (absolute, unresolved, "
                            "or outside any loaded section); a uniform "
                            "load bias would silently corrupt it, and "
                            "this format has no operation to fix it up "
                            "correctly",
                            in_path, (unsigned long)type,
                            (unsigned long)r_offset);

                    continue;   /* PC-relative etc: no load-time fixup needed */
                }

                die("'%s' has a relocation of unrecognised type %lu at "
                    "0x%08lx; not known to need no load-time fixup, refusing "
                    "to guess -- extend the allowlist in %s if it genuinely "
                    "doesn't need one",
                    in_path, (unsigned long)type, (unsigned long)r_offset,
                    g_argv0);
            }

            /* DIR32's resolved value (S + A) must not receive the load
             * bias unless S moves with the image -- a fixed SHN_ABS
             * constant (e.g. a hardware register address defined via a
             * linker script), an unresolved/common symbol, or one in a
             * non-allocated section all break that the same way -- and
             * this format's "+= bias" op cannot tell those cases apart
             * from an ordinary image-relative pointer. RELATIVE is
             * exempt: by definition (and psABI convention) its value is
             * always image-base + addend with no symbol involved, so it
             * always needs the bias regardless of what r_info's unused
             * symbol field happens to contain. */
            if (type == dir32_type
             && !reloc_symbol_is_load_relative(in_path, r_info, symtab_offset,
                                               symtab_size, symtab_entsize,
                                               in_size, in, e_shoff, e_shnum,
                                               e_shentsize, segs, nsegs))
                die("'%s' has a DIR32 relocation at 0x%08lx against a "
                    "symbol that does not move with the image (absolute, "
                    "unresolved, or outside any loaded section); its "
                    "resolved value must not receive the load bias, which "
                    "this format cannot represent",
                    in_path, (unsigned long)r_offset);

            /* A DIR32 slot in an ET_DYN is not something the documented
             * "-pie --no-dynamic-linker" recipe should ever produce: with
             * no external symbols and no dynamic linker, the linker
             * resolves every internal absolute reference to R_*_RELATIVE
             * at link time (see doc/elfload.txt), so a DIR32 surviving
             * into a PIE's own relocations would mean something this tool
             * doesn't understand -- e.g. a real dynamic-symbol reference
             * needing resolution this freestanding loader cannot perform
             * -- produced it. Reject rather than apply the same "+= bias"
             * op RELATIVE gets and risk silently mishandling it. */
            if (type == dir32_type && e_type == ET_DYN)
                die("'%s' is a PIE (ET_DYN) with a DIR32-type relocation at "
                    "0x%08lx; only RELATIVE relocations are expected from "
                    "the documented -pie --no-dynamic-linker recipe -- "
                    "refusing to guess this one's semantics",
                    in_path, (unsigned long)r_offset);

            /* File-backing requirements differ by how a slot's value
             * reaches the loader:
             *
             * - REL-encoded RELATIVE needs no check and no write at all.
             *   Under REL there is no out-of-band addend field -- the
             *   value is understood to already be in the slot -- so a
             *   .bss-resident target (zero-filled, no file bytes) simply
             *   means the encoded addend was 0, and "0 += bias" is
             *   exactly correct.
             * - RELA + RELATIVE's addend is authoritative regardless of
             *   whether it is zero: unlike REL, the slot's own bytes are
             *   not part of RELA semantics at all, so a file-backed slot
             *   must always have the addend written into it -- even 0 --
             *   rather than trusting whatever bytes happen to already be
             *   there (they need not already be zero). Only when the slot
             *   is genuinely .bss-resident (no file bytes to write into,
             *   left zero-filled by the loader) does a zero addend need no
             *   write, matching what zero-fill already provides; a nonzero
             *   addend targeting .bss is the permanent v1 format
             *   limitation documented in doc/elfload.txt.
             * - DIR32 always needs file backing: its resolved value is
             *   baked into the slot's own bytes at link time (see
             *   doc/elfload.txt), which a compiler/linker can only
             *   arrange for a slot that actually has file bytes -- a
             *   literal-zero DIR32 target is never emitted against .bss
             *   in practice (a null-valued pointer is constant-folded
             *   away, never relocated), but a corrupted or hand-crafted
             *   input is checked here rather than trusted. */
            if (type == dir32_type)
                vaddr_to_file_offset(segs, nsegs, r_offset);
            else if (rela && type == relative_type)
            {
                uint32_t file_off;

                if (try_vaddr_to_file_offset(segs, nsegs, r_offset, &file_off))
                    wr32(in + file_off, addend);
                else if (!slot_fully_zero_filled(segs, nsegs, r_offset))
                    die("'%s' has a RELA RELATIVE relocation at 0x%08lx "
                        "whose 4-byte slot straddles the boundary between "
                        "a segment's file-backed part and its zero-filled "
                        "tail; the loader would copy whatever those "
                        "leading bytes happen to contain rather than the "
                        "authoritative addend, and this format has no "
                        "operation to correct that -- relink so this slot "
                        "does not cross a PT_LOAD's p_filesz boundary",
                        in_path, (unsigned long)r_offset);
                else if (addend != 0)
                    die("'%s' has a RELA RELATIVE relocation at 0x%08lx "
                        "with a nonzero addend (0x%08lx) targeting memory "
                        "with no file backing (likely .bss); a slot there "
                        "needs an addend-carrying wire operation .ptos.reloc "
                        "version 1 does not have -- a known format "
                        "limitation, not a bug (see doc/elfload.txt)",
                        in_path, (unsigned long)r_offset,
                        (unsigned long)addend);
                /* else: addend == 0 and the slot is .bss-resident -- the
                 * loader's own zero-fill already provides exactly this
                 * value, nothing to write */
            }

            if (nslots == slot_cap)
            {
                slot_cap *= 2;
                slots = realloc(slots, slot_cap * sizeof(*slots));
                if (!slots)
                    die("out of memory");
            }
            slots[nslots].vaddr = r_offset;
            nslots++;
        }
    }

    if (nslots > 1)
        qsort(slots, nslots, sizeof(*slots), slot_cmp);
    {
        size_t dupidx;
        for (dupidx = 0; dupidx + 1 < nslots; dupidx++)
        {
            if (slots[dupidx].vaddr == slots[dupidx + 1].vaddr)
                die("'%s' has two relocations targeting the same address "
                    "(0x%08lx); unsupported by .ptos.reloc version 1",
                    in_path, (unsigned long)slots[dupidx].vaddr);
        }
    }

    /* A fixed-base ET_EXEC with zero relocated slots is indistinguishable,
     * from this tool's point of view, from one whose linker simply wasn't
     * given the documented "-q"/"--emit-relocs" flag at all: both look
     * identical here (no SHT_REL/SHT_RELA sections survive either way),
     * but the latter needed fixups it never got a chance to retain --
     * ptos-elf-pack would then emit a valid-looking empty .ptos.reloc
     * stream over a binary that silently corrupts itself the moment it
     * loads anywhere but its link address. This ambiguity does not apply
     * to an ET_DYN (PIE): its position-independent code genuinely can
     * need zero R_*_RELATIVE fixups (doc/elfload.txt), so an empty
     * stream there is unremarkable. Require an explicit override for the
     * (much rarer) genuine zero-relocation ET_EXEC instead of guessing. */
    if (nslots == 0 && e_type == ET_EXEC && !allow_no_relocations)
        die("'%s' is a fixed-base ET_EXEC with no relocations to pack; "
            "this is indistinguishable from one linked without -q/"
            "--emit-relocs (see doc/elfload.txt), which would silently "
            "corrupt at load time if it actually needed any -- re-check "
            "the link command, or pass --allow-no-relocations if this "
            "binary genuinely stores no absolute address anywhere",
            in_path);

    /* build the .ptos.reloc payload: 8 byte header + ULEB128 delta stream */
    payload.data = NULL;
    payload.len = 0;
    payload.cap = 0;

    wr32(hdrbuf, PTOS_RELOC_MAGIC);
    wr16(hdrbuf + 4, PTOS_RELOC_VERSION);
    wr16(hdrbuf + 6, 0);
    buf_append(&payload, hdrbuf, sizeof(hdrbuf));

    {
        uint32_t prev = link_base;
        size_t k;
        for (k = 0; k < nslots; k++)
        {
            buf_append_uleb128(&payload, slots[k].vaddr - prev);
            prev = slots[k].vaddr;
        }
    }

    new_data_off = in_size;
    padded_len = (uint32_t)((payload.len + 3u) & ~3u);

    /* build the .ptos.imports payload (doc/elfload.txt): a 32 byte header,
     * the import table, the bind table, then a string table -- omitted
     * entirely when the input imports nothing (nimports == 0), which
     * includes every plain binary this tool already supported before
     * this feature existed. */
    imports_payload.data = NULL;
    imports_payload.len = 0;
    imports_payload.cap = 0;
    imports_data_off = 0;
    imports_padded_len = 0;

    if (nimports > 0)
    {
        BUF strtab;
        uint32_t *name_offs;
        uint32_t import_table_off, bind_table_off, strtab_off;
        unsigned char ihdrbuf[32];
        size_t k;

        if (nimports > PTOS_IMPORT_MAX_COUNT || nbinds > PTOS_IMPORT_MAX_COUNT)
            die("'%s' has more pTOS ABI imports/binds (%lu/%lu) than the "
                "loader accepts (%u each) -- see PTOS_IMPORT_MAX_COUNT in "
                "bdos/elfld.c", in_path, (unsigned long)nimports,
                (unsigned long)nbinds, PTOS_IMPORT_MAX_COUNT);

        strtab.data = NULL;
        strtab.len = 0;
        strtab.cap = 0;

        name_offs = malloc(nimports * sizeof(*name_offs));
        if (!name_offs)
            die("out of memory");
        for (k = 0; k < nimports; k++)
        {
            name_offs[k] = (uint32_t)strtab.len;
            buf_append(&strtab, imports[k].name, strlen(imports[k].name) + 1);
        }

        import_table_off = 32;
        bind_table_off = import_table_off + (uint32_t)nimports * 12u;
        strtab_off = bind_table_off + (uint32_t)nbinds * 12u;

        wr32(ihdrbuf, PTOS_IMPORTS_MAGIC);
        wr16(ihdrbuf + 4, PTOS_IMPORTS_VERSION);
        wr16(ihdrbuf + 6, 0);
        wr32(ihdrbuf + 8, (uint32_t)nimports);
        wr32(ihdrbuf + 12, (uint32_t)nbinds);
        wr32(ihdrbuf + 16, import_table_off);
        wr32(ihdrbuf + 20, bind_table_off);
        wr32(ihdrbuf + 24, strtab_off);
        wr32(ihdrbuf + 28, (uint32_t)strtab.len);
        buf_append(&imports_payload, ihdrbuf, sizeof(ihdrbuf));

        for (k = 0; k < nimports; k++)
        {
            unsigned char ent[12];
            wr32(ent + 0, name_offs[k]);
            wr16(ent + 4, (uint16_t)imports[k].abi_major);
            wr16(ent + 6, (uint16_t)imports[k].abi_minor);
            ent[8] = (unsigned char)imports[k].kind;
            ent[9] = ent[10] = ent[11] = 0;
            buf_append(&imports_payload, ent, sizeof(ent));
        }

        for (k = 0; k < nbinds; k++)
        {
            unsigned char ent[12];
            wr32(ent + 0, binds[k].import_index);
            wr32(ent + 4, binds[k].slot_vaddr);
            ent[8] = (unsigned char)binds[k].bind_op;
            ent[9] = ent[10] = ent[11] = 0;
            buf_append(&imports_payload, ent, sizeof(ent));
        }

        buf_append(&imports_payload, strtab.data, strtab.len);

        free(name_offs);
        free(strtab.data);

        imports_data_off = new_data_off + padded_len;
        imports_padded_len = (uint32_t)((imports_payload.len + 3u) & ~3u);
    }

    /* build the new program header table: the original entries, minus any
     * PT_PHDR (see below), plus one new PT_PTOS_RELOC entry */
    newphdrs.data = NULL;
    newphdrs.len = 0;
    newphdrs.cap = 0;
    for (i = 0; i < e_phnum; i++)
    {
        const unsigned char *ph = in + e_phoff + (uint32_t)i * e_phentsize;

        /* Drop a PT_PHDR entry rather than copy it forward: it would
         * describe the OLD table's now-stale location, and there is no
         * valid replacement value either -- the appended table lives
         * past every PT_LOAD's mapped range (this tool only appends
         * file bytes, never extends a segment's memsz), so it has no
         * p_vaddr a PT_PHDR entry could correctly describe at all.
         * bdos/elfld.c has no use for PT_PHDR anyway (it reads
         * e_phoff/e_phnum from the ELF header directly, not via
         * AT_PHDR), so omitting it is both correct and harmless --
         * PT_PHDR is optional, needed only by an ELF interpreter this
         * freestanding loader has no equivalent of. None of the
         * documented build recipes in doc/elfload.txt have been
         * observed to emit one in the first place. */
        if (rd32(ph + PHDR_P_TYPE) == PT_PHDR)
            continue;
        buf_append(&newphdrs, ph, e_phentsize);
    }

    {
        unsigned char newph[PHDR_SIZE];
        memset(newph, 0, sizeof(newph));
        wr32(newph + PHDR_P_TYPE, (uint32_t)PT_PTOS_RELOC);
        wr32(newph + PHDR_P_OFFSET, new_data_off);
        wr32(newph + PHDR_P_VADDR, 0);
        wr32(newph + 12 /* p_paddr */, 0);
        wr32(newph + PHDR_P_FILESZ, (uint32_t)payload.len);
        wr32(newph + PHDR_P_MEMSZ, 0);
        wr32(newph + 24 /* p_flags */, 0);
        /* align 1: this segment is metadata only, never mapped, so there is
         * no p_vaddr for p_offset to be congruent with -- p_align must not
         * claim a stronger constraint than that */
        wr32(newph + 28 /* p_align */, 1);
        buf_append(&newphdrs, newph, sizeof(newph));
    }

    if (nimports > 0)
    {
        unsigned char newph[PHDR_SIZE];
        memset(newph, 0, sizeof(newph));
        wr32(newph + PHDR_P_TYPE, (uint32_t)PT_PTOS_IMPORTS);
        wr32(newph + PHDR_P_OFFSET, imports_data_off);
        wr32(newph + PHDR_P_VADDR, 0);
        wr32(newph + 12 /* p_paddr */, 0);
        wr32(newph + PHDR_P_FILESZ, (uint32_t)imports_payload.len);
        wr32(newph + PHDR_P_MEMSZ, 0);
        wr32(newph + 24 /* p_flags */, 0);
        wr32(newph + 28 /* p_align */, 1);   /* metadata only, see PT_PTOS_RELOC above */
        buf_append(&newphdrs, newph, sizeof(newph));
    }

    /* new_data_off/padded_len/newphdrs.len are all uint32_t/size_t
     * values derived from an in_size already proven to fit uint32_t, but
     * their sum -- the packed file's total length -- is not itself
     * bounds-checked before being narrowed into new_phdr_off and written
     * into e_phoff (an ELF32 field). Check in 64 bits so a pathological
     * combination cannot wrap into a bogus, unusably small offset.
     *
     * The bound is the loader's, not the ELF32 format's: bdos/elfld.c's
     * read_at() (used both for the PT_PTOS_RELOC payload's p_offset and
     * for e_phoff itself) rejects any offset >= 0x80000000 outright,
     * because it goes through xlseek()'s signed LONG. A packed file
     * between 2 GiB and 4 GiB would satisfy the plain ELF32 4 GiB limit
     * yet still be unloadable, so bound the whole appended region --
     * every offset within it that the loader might read -- by 0x80000000
     * instead of 0xffffffff. */
    if ((uint64_t)new_data_off + padded_len + imports_padded_len + newphdrs.len
        > 0x80000000UL)
        die("'%s': packed output would exceed the loader's 0x80000000 "
            "signed-offset limit (bdos/elfld.c read_at())",
            in_path);

    new_phdr_off = new_data_off + padded_len + imports_padded_len;
    /* newphdrs.len is a whole number of entries: the copy loop above
     * appends exactly one PHDR_SIZE per non-PT_PHDR input entry, and the
     * block below appends exactly one more (the new PT_PTOS_RELOC) */
    new_phnum = (uint32_t)(newphdrs.len / PHDR_SIZE);
    if (new_phnum > 0xffffUL)
        die("'%s' already has too many program headers to add one more",
            in_path);

    /* patch the (copied) ELF header in place: only e_phoff/e_phnum change,
     * every other offset in the file -- including the original program and
     * section header tables -- is untouched and stays individually valid */
    wr32(in + EHDR_E_PHOFF, new_phdr_off);
    wr16(in + EHDR_E_PHNUM, (uint16_t)new_phnum);

    /* --strip-shdr: drop the pointer to the section header table (the
     * bytes themselves stay in the file, just unreferenced) so a loader
     * that finds no PT_PTOS_RELOC header has no SHT_REL/SHT_RELA fallback
     * to silently succeed through instead -- proves the program-header
     * path is what actually ran, not just that it's present */
    if (strip_shdr)
    {
        wr32(in + EHDR_E_SHOFF, 0);
        wr16(in + EHDR_E_SHNUM, 0);
        wr16(in + EHDR_E_SHENTSIZE, 0);
        wr16(in + EHDR_E_SHSTRNDX, 0);
    }

    out = fopen(out_path, "wb");
    if (!out)
        die("cannot open '%s' for writing", out_path);

    if (fwrite(in, 1, in_size, out) != in_size)
        die("short write to '%s'", out_path);
    if (fwrite(payload.data, 1, payload.len, out) != payload.len)
        die("short write to '%s'", out_path);
    if (padded_len > payload.len)
    {
        static const unsigned char zero[4] = { 0, 0, 0, 0 };
        if (fwrite(zero, 1, padded_len - payload.len, out) != padded_len - payload.len)
            die("short write to '%s'", out_path);
    }
    if (nimports > 0)
    {
        if (fwrite(imports_payload.data, 1, imports_payload.len, out)
            != imports_payload.len)
            die("short write to '%s'", out_path);
        if (imports_padded_len > imports_payload.len)
        {
            static const unsigned char zero[4] = { 0, 0, 0, 0 };
            if (fwrite(zero, 1, imports_padded_len - imports_payload.len, out)
                != imports_padded_len - imports_payload.len)
                die("short write to '%s'", out_path);
        }
    }
    if (fwrite(newphdrs.data, 1, newphdrs.len, out) != newphdrs.len)
        die("short write to '%s'", out_path);

    if (fclose(out) != 0)
        die("error closing '%s'", out_path);

    if (nimports > 0)
        fprintf(stderr, "%s: packed %lu relocation%s and %lu import%s "
                "(%lu bind%s) into '%s' (%lu bytes)\n",
                g_argv0, (unsigned long)nslots, nslots == 1 ? "" : "s",
                (unsigned long)nimports, nimports == 1 ? "" : "s",
                (unsigned long)nbinds, nbinds == 1 ? "" : "s",
                out_path, (unsigned long)(new_phdr_off + newphdrs.len));
    else
        fprintf(stderr, "%s: packed %lu relocation%s into '%s' (%lu bytes)\n",
            g_argv0, (unsigned long)nslots, nslots == 1 ? "" : "s",
            out_path, (unsigned long)(new_phdr_off + newphdrs.len));

    return 0;
}
