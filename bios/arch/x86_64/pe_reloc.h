/*
 * pe_reloc.h - re-apply this image's own PE base relocations for the
 *              higher-half bias
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef X86_64_PE_RELOC_H
#define X86_64_PE_RELOC_H

#include "portab.h"

/*
 * The image is linked with -pie specifically so the PE loader emits a
 * base relocation table (.reloc): the same table EFI's own loader
 * already walked once, adding (actual load address - link-time
 * ImageBase) to every entry, is still sitting in this image's own
 * memory, unchanged, and can be walked a second time.
 *
 * image_base is this image's actual (low) load address, exactly as
 * passed to x86_64_build_page_tables() (loaded_image->ImageBase in
 * startup.c). delta is added to every DIR64 relocation site: pass
 * (X86_64_KERNEL_VIRT_BASE - identity_low_base) to rebase every
 * compile-time-initialized absolute pointer in the image (function
 * pointer tables, string tables, jump tables, ...) from its low,
 * PE-loader-fixed-up value to its higher-half virtual alias, in one
 * pass, instead of translating each one individually wherever it is
 * later read (see #343). Must run before x86_64_drop_identity_map(),
 * while the low mapping this walks and writes through is still valid,
 * and before anything reads one of these pointers expecting the high
 * alias.
 *
 * Uses direct pointer accesses into the loaded image's own PE headers
 * to find the base relocation directory (IMAGE_DIRECTORY_ENTRY_BASERELOC,
 * index 5): this is boot-time-only code with no better source of PE
 * layout information available yet, and does not depend on the higher-
 * half mapping (or code that has already jumped there) to run.
 */
void x86_64_apply_higher_half_relocations(UQUAD image_base, UQUAD delta);

#endif /* X86_64_PE_RELOC_H */
