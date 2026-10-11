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
 * A program image: where the segments of an x32 program go in its private
 * address space, and the bytes to fill them with.  Two things make one: a
 * statically linked x32 ELF executable (ELFCLASS32, EM_X86_64, ET_EXEC) --
 * the built-in EmuCON, embedded in the kernel image -- or a layout the ELF
 * loader (bdos/elfld.c) built from a file, relocated into the image window.
 * This is not a general ELF loader.  Every PT_LOAD segment must lie inside the
 * user image window (X86_64_USER_IMAGE_BASE, X86_64_USER_IMAGE_SIZE),
 * segments may not share a page, and the entry point must lie in an
 * executable one; anything else is refused before a page is mapped.
 */
typedef struct {
    const UBYTE *data;
    ULONG size;
} X32_IMAGE;

#define X32_MAX_SEGMENTS 8

typedef struct {
    UQUAD vaddr;                /* where it goes (need not be page-aligned) */
    ULONG filesz;               /* bytes taken from src, the rest to memsz is zero */
    ULONG memsz;
    ULONG flags;                /* ELF PF_R/PF_W/PF_X */
    const UBYTE *src;
} X32_SEGMENT;

typedef struct {
    X32_SEGMENT seg[X32_MAX_SEGMENTS];
    ULONG nseg;
    UQUAD entry;
    UQUAD end;                  /* just past the highest byte of any segment */
} X32_LAYOUT;

/* TRUE iff the ELF bytes of an image form a valid layout; *entry receives
 * its entry point.  Maps nothing. */
BOOL x86_64_x32image_check(const X32_IMAGE *image, UQUAD *entry);

/* x86_64_x32image_layout() and x86_64_x32_layout_load() in one step. */
BOOL x86_64_x32image_load(X86_64_ASPACE *as, const X32_IMAGE *image, UQUAD *entry);

/* Fills *layout from the ELF bytes of an image (src points into them).  FALSE
 * if they are not an ET_EXEC x32 executable or the layout is not valid. */
BOOL x86_64_x32image_layout(const X32_IMAGE *image, X32_LAYOUT *layout);

/* Whether a layout satisfies the rules above.  `strict` also forbids a segment
 * that is both writable and executable.  The built-in programs are checked
 * strict (x86_64_x32image_layout(), so kproc_set_image()), and are refused
 * with such a segment; a program loaded from a file is not
 * (kproc_set_loaded_image()), and may be a single flat read+write+execute
 * segment. */
BOOL x86_64_x32_layout_valid(const X32_LAYOUT *layout, BOOL strict);

/* The same with another window for the segments than the image window: the
 * memory a program loaded for its caller (Pexec(PE_LOAD)) is put in. */
BOOL x86_64_x32_layout_valid_in(const X32_LAYOUT *layout, BOOL strict,
                                UQUAD window_lo, UQUAD window_hi);

/* Maps and fills every segment as private memory of `as` (read+write+execute
 * as the segment's flags say, all user-accessible).  FALSE if memory ran out;
 * segments already mapped stay in `as` and are freed with it, so the caller
 * destroys the address space. */
BOOL x86_64_x32_layout_load(X86_64_ASPACE *as, const X32_LAYOUT *layout);

/* The embedded EmuCON (cli/arch/x86_64/emucon_image.c). */
const X32_IMAGE *x86_64_emucon_image(void);
/* The ring-3 probe program of the boot self-test (tests/x32_probe/). */
const X32_IMAGE *x86_64_x32probe_image(void);

#endif /* __x86_64__ */

#endif /* X32IMAGE_H */
