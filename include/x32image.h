/*
 * x32image.h - built-in x32 program images (x86-64)
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef X32IMAGE_H
#define X32IMAGE_H

#include "portab.h"
#include "procmem.h"

#ifdef __x86_64__

/*
 * A statically linked x32 ELF executable (ELFCLASS32, EM_X86_64, ET_EXEC)
 * embedded in the kernel image: the built-in EmuCON.  This is not a general
 * ELF loader.  Every PT_LOAD segment must lie inside the user image window
 * (X86_64_USER_IMAGE_BASE, X86_64_USER_IMAGE_SIZE), segments may not
 * overlap, and the entry point must lie in an executable one; anything else
 * is refused before a page is mapped.
 */
typedef struct {
    const UBYTE *data;
    ULONG size;
} X32_IMAGE;

/* TRUE iff the image satisfies the rules above; *entry receives its entry
 * point.  Maps nothing. */
BOOL x86_64_x32image_check(const X32_IMAGE *image, UQUAD *entry);

/* Maps and fills every segment of a checked image as private memory of `as`
 * (text read+execute, rodata read-only, data and bss read+write; all
 * user-accessible, none writable and executable at once).  FALSE if the
 * image is invalid or memory ran out; segments already mapped stay in `as`
 * and are freed with it, so the caller destroys the address space. */
BOOL x86_64_x32image_load(X86_64_ASPACE *as, const X32_IMAGE *image, UQUAD *entry);

/* The embedded EmuCON (cli/arch/x86_64/emucon_image.c). */
const X32_IMAGE *x86_64_emucon_image(void);
/* The ring-3 probe program of the boot self-test (tests/x32_probe/). */
const X32_IMAGE *x86_64_x32probe_image(void);

#endif /* __x86_64__ */

#endif /* X32IMAGE_H */
