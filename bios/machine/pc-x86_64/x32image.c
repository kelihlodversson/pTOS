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

typedef struct {
    ULONG offset, vaddr, filesz, memsz, flags;
} SEG;

/* Reads and validates the program headers; returns the number of loadable
 * segments, or -1.  seg[] receives them. */
static int parse(const X32_IMAGE *image, SEG *seg, UQUAD *entry)
{
    const UBYTE *e = image->data;
    ULONG phoff, phnum, i, n = 0, ent;
    BOOL entry_ok = FALSE;

    if (!e || image->size < EHDR_SIZE)
        return -1;
    if (e[0] != 0x7f || e[1] != 'E' || e[2] != 'L' || e[3] != 'F' ||
        e[4] != 1 /* ELFCLASS32 */ || e[5] != 1 /* little endian */ ||
        rd16(e + 16) != 2 /* ET_EXEC */ || rd16(e + 18) != EM_X86_64 ||
        rd16(e + 42) != PHDR_SIZE)
        return -1;
    ent = rd32(e + 24);
    phoff = rd32(e + 28);
    phnum = rd16(e + 44);
    if (!phnum || phoff > image->size || phnum * PHDR_SIZE > image->size - phoff)
        return -1;

    for (i = 0; i < phnum; i++) {
        const UBYTE *ph = e + phoff + i * PHDR_SIZE;
        SEG *s;
        UQUAD lo, hi, k;

        if (rd32(ph) != PT_LOAD)
            continue;
        if (n == MAX_SEGMENTS)
            return -1;
        s = &seg[n];
        s->offset = rd32(ph + 4);
        s->vaddr = rd32(ph + 8);
        s->filesz = rd32(ph + 16);
        s->memsz = rd32(ph + 20);
        s->flags = rd32(ph + 24);
        if (!s->memsz || s->filesz > s->memsz ||
            s->offset > image->size || s->filesz > image->size - s->offset)
            return -1;
        lo = s->vaddr;
        hi = lo + s->memsz;
        if (lo < X86_64_USER_IMAGE_BASE ||
            hi > X86_64_USER_IMAGE_BASE + X86_64_USER_IMAGE_SIZE)
            return -1;
        if ((s->flags & PF_W) && (s->flags & PF_X))
            return -1;                  /* W^X */
        for (k = 0; k < n; k++) {       /* no two segments share a page */
            UQUAD olo = seg[k].vaddr & ~(X86_64_PAGE_SIZE - 1);
            UQUAD ohi = ((UQUAD)seg[k].vaddr + seg[k].memsz + X86_64_PAGE_SIZE - 1)
                        & ~(X86_64_PAGE_SIZE - 1);
            UQUAD nlo = lo & ~(X86_64_PAGE_SIZE - 1);
            UQUAD nhi = (hi + X86_64_PAGE_SIZE - 1) & ~(X86_64_PAGE_SIZE - 1);

            if (nlo < ohi && olo < nhi)
                return -1;
        }
        if ((s->flags & PF_X) && ent >= lo && ent < hi)
            entry_ok = TRUE;
        n++;
    }
    if (!n || !entry_ok)
        return -1;
    *entry = ent;
    return (int)n;
}

BOOL x86_64_x32image_check(const X32_IMAGE *image, UQUAD *entry)
{
    SEG seg[MAX_SEGMENTS];
    UQUAD e;

    if (parse(image, seg, &e) < 0)
        return FALSE;
    if (entry)
        *entry = e;
    return TRUE;
}

BOOL x86_64_x32image_load(X86_64_ASPACE *as, const X32_IMAGE *image, UQUAD *entry)
{
    SEG seg[MAX_SEGMENTS];
    UQUAD e;
    int n, i;

    n = parse(image, seg, &e);
    if (n < 0)
        return FALSE;
    for (i = 0; i < n; i++) {
        UWORD prot = ASPACE_PROT_USER;

        if (seg[i].flags & PF_W)
            prot |= ASPACE_PROT_WRITE;
        if (seg[i].flags & PF_X)
            prot |= ASPACE_PROT_EXEC;
        if (!x86_64_aspace_load_private(as, seg[i].vaddr, seg[i].memsz, prot,
                                        image->data + seg[i].offset, seg[i].filesz))
            return FALSE;       /* the caller destroys `as`, which frees what i mapped */
    }
    if (entry)
        *entry = e;
    return TRUE;
}
