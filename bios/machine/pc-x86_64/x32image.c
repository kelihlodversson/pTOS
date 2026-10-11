/*
 * x32image.c - loader for the embedded x32 program image (x86-64)
 *
 * See include/x32image.h for the contract.
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_PC_X86_64
#error This file must only be compiled for the pc-x86_64 machine
#endif

#include "portab.h"
#include "string.h"
#include "procmem.h"
#include "x32image.h"
#include "pmem.h"

#define EHDR_SIZE       52
#define PHDR_SIZE       32
#define PT_LOAD         1
#define PF_X            1
#define PF_W            2
#define PF_R            4
#define EM_X86_64       62
#define MAX_SEGMENTS    8

/* little-endian field readers: the image is not necessarily aligned */
static ULONG rd16(const UBYTE *p) { return p[0] | (p[1] << 8); }
static ULONG rd32(const UBYTE *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) | ((ULONG)p[3] << 24);
}

/* Reads and validates the program headers of an ELF image into a layout. */
BOOL x86_64_x32image_layout(const X32_IMAGE *image, X32_LAYOUT *layout)
{
    const UBYTE *e = image->data;
    ULONG phoff, phnum, i, ent;

    if (!e || image->size < EHDR_SIZE)
        return FALSE;
    if (e[0] != 0x7f || e[1] != 'E' || e[2] != 'L' || e[3] != 'F' ||
        e[4] != 1 /* ELFCLASS32 */ || e[5] != 1 /* little endian */ ||
        rd16(e + 16) != 2 /* ET_EXEC */ || rd16(e + 18) != EM_X86_64 ||
        rd16(e + 42) != PHDR_SIZE)
        return FALSE;
    ent = rd32(e + 24);
    phoff = rd32(e + 28);
    phnum = rd16(e + 44);
    if (!phnum || phoff > image->size || phnum * PHDR_SIZE > image->size - phoff)
        return FALSE;

    layout->nseg = 0;
    layout->end = 0;
    for (i = 0; i < phnum; i++) {
        const UBYTE *ph = e + phoff + i * PHDR_SIZE;
        X32_SEGMENT *s;
        ULONG offset;

        if (rd32(ph) != PT_LOAD)
            continue;
        if (layout->nseg == X32_MAX_SEGMENTS)
            return FALSE;
        s = &layout->seg[layout->nseg++];
        offset = rd32(ph + 4);
        s->vaddr = rd32(ph + 8);
        s->filesz = rd32(ph + 16);
        s->memsz = rd32(ph + 20);
        s->flags = rd32(ph + 24);
        if (!s->memsz || s->filesz > s->memsz ||
            offset > image->size || s->filesz > image->size - offset)
            return FALSE;
        s->src = e + offset;
        if (s->vaddr + s->memsz > layout->end)
            layout->end = s->vaddr + s->memsz;
    }
    layout->entry = ent;
    return x86_64_x32_layout_valid(layout, TRUE);
}

BOOL x86_64_x32_layout_valid(const X32_LAYOUT *layout, BOOL strict)
{
    return x86_64_x32_layout_valid_in(layout, strict, X86_64_USER_IMAGE_BASE,
                                      X86_64_USER_IMAGE_BASE + X86_64_USER_IMAGE_SIZE);
}

BOOL x86_64_x32_layout_valid_in(const X32_LAYOUT *layout, BOOL strict, UQUAD window_lo, UQUAD window_hi)
{
    BOOL entry_ok = FALSE;
    ULONG i, k;

    if (!layout->nseg || layout->nseg > X32_MAX_SEGMENTS)
        return FALSE;
    for (i = 0; i < layout->nseg; i++) {
        const X32_SEGMENT *s = &layout->seg[i];
        UQUAD lo = s->vaddr, hi = s->vaddr + s->memsz;

        if (!s->memsz || s->filesz > s->memsz)
            return FALSE;
        if (lo < window_lo || hi > window_hi)
            return FALSE;
        if (strict && (s->flags & PF_W) && (s->flags & PF_X))
            return FALSE;               /* W^X */
        for (k = 0; k < i; k++) {       /* no two segments share a page */
            UQUAD olo = layout->seg[k].vaddr & ~(X86_64_PAGE_SIZE - 1);
            UQUAD ohi = (layout->seg[k].vaddr + layout->seg[k].memsz + X86_64_PAGE_SIZE - 1)
                        & ~(X86_64_PAGE_SIZE - 1);
            UQUAD nlo = lo & ~(X86_64_PAGE_SIZE - 1);
            UQUAD nhi = (hi + X86_64_PAGE_SIZE - 1) & ~(X86_64_PAGE_SIZE - 1);

            if (nlo < ohi && olo < nhi)
                return FALSE;
        }
        if ((s->flags & PF_X) && layout->entry >= lo && layout->entry < hi)
            entry_ok = TRUE;
    }
    return entry_ok;
}

BOOL x86_64_x32_layout_load(X86_64_ASPACE *as, const X32_LAYOUT *layout)
{
    ULONG i;

    for (i = 0; i < layout->nseg; i++) {
        const X32_SEGMENT *s = &layout->seg[i];
        UWORD prot = ASPACE_PROT_USER;

        if (s->flags & PF_W)
            prot |= ASPACE_PROT_WRITE;
        if (s->flags & PF_X)
            prot |= ASPACE_PROT_EXEC;
        if (!x86_64_aspace_load_private(as, s->vaddr, s->memsz, prot, s->src, s->filesz))
            return FALSE;       /* the caller destroys `as`, which frees what i mapped */
    }
    return TRUE;
}

BOOL x86_64_x32image_check(const X32_IMAGE *image, UQUAD *entry)
{
    X32_LAYOUT layout;

    if (!x86_64_x32image_layout(image, &layout))
        return FALSE;
    if (entry)
        *entry = layout.entry;
    return TRUE;
}

BOOL x86_64_x32image_load(X86_64_ASPACE *as, const X32_IMAGE *image, UQUAD *entry)
{
    X32_LAYOUT layout;

    if (!x86_64_x32image_layout(image, &layout) || !x86_64_x32_layout_load(as, &layout))
        return FALSE;
    if (entry)
        *entry = layout.entry;
    return TRUE;
}
