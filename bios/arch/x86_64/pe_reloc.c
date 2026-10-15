/*
 * pe_reloc.c - re-apply this image's own PE base relocations for the
 *              higher-half bias
 *
 * See pe_reloc.h for why this exists (#343) instead of translating each
 * compile-time-initialized absolute pointer individually wherever it is
 * later read.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "portab.h"
#include "earlycon.h"
#include "pe_reloc.h"

/* Minimal subset of the PE32+ header structs (Microsoft PE/COFF spec),
 * just enough to reach IMAGE_DIRECTORY_ENTRY_BASERELOC's RVA/size. Read
 * directly out of this image's own loaded bytes, not parsed from a file
 * -- there is no filesystem this early, and none of this changes once
 * linked, so no more than this needs to exist. Packed: nothing here
 * relies on the host compiler's own struct layout/alignment, only on the
 * on-disk PE format's fixed byte offsets. */

struct dos_header {
    UWORD e_magic;          /* "MZ" */
    UBYTE e_reserved[0x3a]; /* everything between e_magic and e_lfanew */
    ULONG e_lfanew;         /* file offset of the PE (NT) headers */
} __attribute__((packed));

struct data_directory {
    ULONG virtual_address;
    ULONG size;
} __attribute__((packed));

struct file_header {
    UWORD machine;
    UWORD number_of_sections;
    ULONG time_date_stamp;
    ULONG pointer_to_symbol_table;
    ULONG number_of_symbols;
    UWORD size_of_optional_header;
    UWORD characteristics;
} __attribute__((packed));

#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES 16
#define IMAGE_DIRECTORY_ENTRY_BASERELOC 5

struct optional_header64 {
    UWORD magic;
    UBYTE major_linker_version;
    UBYTE minor_linker_version;
    ULONG size_of_code;
    ULONG size_of_initialized_data;
    ULONG size_of_uninitialized_data;
    ULONG address_of_entry_point;
    ULONG base_of_code;
    UQUAD image_base;
    ULONG section_alignment;
    ULONG file_alignment;
    UWORD major_os_version;
    UWORD minor_os_version;
    UWORD major_image_version;
    UWORD minor_image_version;
    UWORD major_subsystem_version;
    UWORD minor_subsystem_version;
    ULONG win32_version_value;
    ULONG size_of_image;
    ULONG size_of_headers;
    ULONG checksum;
    UWORD subsystem;
    UWORD dll_characteristics;
    UQUAD size_of_stack_reserve;
    UQUAD size_of_stack_commit;
    UQUAD size_of_heap_reserve;
    UQUAD size_of_heap_commit;
    ULONG loader_flags;
    ULONG number_of_rva_and_sizes;
    struct data_directory data_directory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
} __attribute__((packed));

struct nt_headers64 {
    ULONG signature;        /* "PE\0\0" */
    struct file_header file_header;
    struct optional_header64 optional_header;
} __attribute__((packed));

/* IMAGE_BASE_RELOCATION block header (Microsoft PE/COFF spec): each
 * block covers one 4 KiB page (virtual_address = that page's RVA) and is
 * followed by (size_of_block - sizeof(this header)) / 2 UWORD entries,
 * each packing a 4-bit type in the high nibble and a 12-bit in-page byte
 * offset in the low 12 bits. */
struct base_relocation_block {
    ULONG virtual_address;
    ULONG size_of_block;
} __attribute__((packed));

#define IMAGE_REL_BASED_ABSOLUTE 0  /* padding entry, skip */
#define IMAGE_REL_BASED_DIR64    10 /* the only real type x86-64 PE emits */

static NORETURN void panic_bad_reloc(void)
{
    earlycon_puts("panic: unexpected PE base relocation type -- image "
                  "layout does not match pe_reloc.c's assumptions\n");
    for (;;)
        ;
}

void x86_64_apply_higher_half_relocations(UQUAD image_base, UQUAD delta)
{
    const UBYTE *base = (const UBYTE *)(uintptr_t)image_base;
    const struct dos_header *dos = (const struct dos_header *)(uintptr_t)base;
    const struct nt_headers64 *nt =
        (const struct nt_headers64 *)(uintptr_t)(base + dos->e_lfanew);
    const struct data_directory *reloc_dir =
        &nt->optional_header.data_directory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    const UBYTE *pos = base + reloc_dir->virtual_address;
    const UBYTE *end = pos + reloc_dir->size;

    if (delta == 0 || reloc_dir->size == 0)
        return;

    while (pos < end) {
        const struct base_relocation_block *block =
            (const struct base_relocation_block *)(uintptr_t)pos;
        UQUAD page_base = image_base + block->virtual_address;
        ULONG entry_count = (block->size_of_block - sizeof(*block)) / sizeof(UWORD);
        const UWORD *entries = (const UWORD *)(uintptr_t)(pos + sizeof(*block));
        ULONG i;

        for (i = 0; i < entry_count; i++) {
            UWORD entry = entries[i];
            UWORD type = entry >> 12;
            UWORD offset = entry & 0xFFF;
            UQUAD *site;

            if (type == IMAGE_REL_BASED_ABSOLUTE)
                continue;
            if (type != IMAGE_REL_BASED_DIR64)
                panic_bad_reloc();

            site = (UQUAD *)(uintptr_t)(page_base + offset);
            *site += delta;
        }

        pos += block->size_of_block;
    }
}
