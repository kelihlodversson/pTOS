/*
 * progimage.c - the layout of a program in its address space (32-bit MMU
 *               configurations)
 *
 * See include/progimage.h for the contract.  The ELF loader (bdos/elfld.c)
 * builds a layout from a file, relocated into the image window, and
 * kproc_set_loaded_image() maps it with layout_load(); the rules it must
 * satisfy are checked here.  There is no program embedded in the kernel on
 * these configurations (EmuCON is a program, CONF_EXTERNAL_CLI), so
 * image_layout() and the functions built on it refuse everything.
 *
 * (The x86-64 kernel has its own, bios/machine/pc-x86_64/x32image.c: found
 * first through vpath.)
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#if CONF_WITH_USER_ASPACE && !defined(__x86_64__)

#include "portab.h"
#include "string.h"
#include "procmem.h"
#include "progimage.h"

#define PAGE            USER_PAGE_SIZE
#define PF_X            1
#define PF_W            2

BOOL image_layout(const PROG_IMAGE *image, PROG_LAYOUT *layout)
{
    (void)image;
    (void)layout;
    return FALSE;               /* no program is embedded */
}

BOOL layout_valid(const PROG_LAYOUT *layout, BOOL strict)
{
    return layout_valid_in(layout, strict, USER_IMAGE_BASE,
                                      USER_IMAGE_BASE + USER_IMAGE_SIZE);
}

BOOL layout_valid_in(const PROG_LAYOUT *layout, BOOL strict, VADDR window_lo, VADDR window_hi)
{
    BOOL entry_ok = FALSE;
    ULONG i, k;

    if (!layout->nseg || layout->nseg > PROG_MAX_SEGMENTS)
        return FALSE;
    for (i = 0; i < layout->nseg; i++) {
        const PROG_SEGMENT *s = &layout->seg[i];
        VADDR lo = s->vaddr, hi = s->vaddr + s->memsz;

        if (!s->memsz || s->filesz > s->memsz || hi < lo)
            return FALSE;
        if (lo < window_lo || hi > window_hi)
            return FALSE;
        if (strict && (s->flags & PF_W) && (s->flags & PF_X))
            return FALSE;               /* W^X */
        for (k = 0; k < i; k++) {       /* no two segments share a page */
            VADDR olo = layout->seg[k].vaddr & ~(PAGE - 1);
            VADDR ohi = (layout->seg[k].vaddr + layout->seg[k].memsz + PAGE - 1) & ~(PAGE - 1);
            VADDR nlo = lo & ~(PAGE - 1);
            VADDR nhi = (hi + PAGE - 1) & ~(PAGE - 1);

            if (nlo < ohi && olo < nhi)
                return FALSE;
        }
        if ((s->flags & PF_X) && layout->entry >= lo && layout->entry < hi)
            entry_ok = TRUE;
    }
    return entry_ok;
}

BOOL layout_load(ASPACE *as, const PROG_LAYOUT *layout)
{
    ULONG i;

    for (i = 0; i < layout->nseg; i++) {
        const PROG_SEGMENT *s = &layout->seg[i];
        UWORD prot = ASPACE_PROT_USER;

        if (s->flags & PF_W)
            prot |= ASPACE_PROT_WRITE;
        if (s->flags & PF_X)
            prot |= ASPACE_PROT_EXEC;
        if (!aspace_load_private(as, s->vaddr, s->memsz, prot, s->src, s->filesz))
            return FALSE;       /* the caller destroys `as`, which frees what i mapped */
    }
    return TRUE;
}

BOOL image_check(const PROG_IMAGE *image, VADDR *entry)
{
    PROG_LAYOUT layout;

    if (!image_layout(image, &layout))
        return FALSE;
    if (entry)
        *entry = layout.entry;
    return TRUE;
}

BOOL image_load(ASPACE *as, const PROG_IMAGE *image, VADDR *entry)
{
    PROG_LAYOUT layout;

    if (!image_layout(image, &layout) || !layout_load(as, &layout))
        return FALSE;
    if (entry)
        *entry = layout.entry;
    return TRUE;
}

#endif /* CONF_WITH_USER_ASPACE */
