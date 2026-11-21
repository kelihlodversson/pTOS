/*
 * ELF32 bootstrap validation shared by the interpreter entry points.
 * This code runs before self-relocation and uses only local calls and data.
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef LDSO_BOOTSTRAP_IMAGE_H
#define LDSO_BOOTSTRAP_IMAGE_H

#include "runtime.h"

static uint32_t word(const unsigned char *p)
{
#if BOOTSTRAP_ELF_DATA == 2
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
#else
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
#endif
}

static int range(const unsigned char *ph, uint32_t count, uint32_t va, uint32_t size, uint32_t flags)
{
    uint32_t i;
    for (i = 0; i < count; i++, ph += 32)
        if (word(ph) == 1 && (word(ph + 24) & flags) == flags && va >= word(ph + 8) &&
            va - word(ph + 8) <= word(ph + 20) && size <= word(ph + 20) - (va - word(ph + 8)))
            return 1;
    return 0;
}

struct bootstrap_image {
    const unsigned char *image, *ph;
    uint32_t base, count, dyn, ds;
};

static int bootstrap_image(const uint32_t *stack, struct bootstrap_image *view)
{
    const uint32_t *v;
    const unsigned char *image, *ph;
    uint32_t i, base = 0, count, off, dyn = 0, ds = 0, argc = *stack;

    if (argc > 16384) return 1;
    v = stack + argc + 2;
    for (i = 0; i < 16384 && *v; i++, v++) ;
    if (i == 16384) return 1;
    for (v++, i = 0; i < 32 && v[0]; i++, v += 2)
        if (v[0] == 7) base = v[1];
    /* Shared-VM loading may rebase the interpreter outside its old window.
     * Keep the complete initial header/PHDR page inside positive user VA. */
    if (!base || (base & 4095U) || base > 0x80000000U - 4096U || i == 32) return 1;
    image = (const unsigned char *)(uintptr_t)base;
    if (image[0] != 0x7f || image[1] != 'E' || image[2] != 'L' || image[3] != 'F' ||
        image[4] != 1 || image[5] != BOOTSTRAP_ELF_DATA) return 1;
    off = word(image + 28);
#if BOOTSTRAP_ELF_DATA == 2
    count = ((uint32_t)image[44] << 8) | image[45];
#else
    count = image[44] | ((uint32_t)image[45] << 8);
#endif
    if (!count || count > DL_MAX_PHDRS || off > 4096 || count * 32 > 4096 - off) return 1;
    ph = image + off;
    for (i = 0; i < count; i++) {
        const unsigned char *h = ph + i * 32;
        /* Validate every load before forming any dynamic/table/target pointer.
         * Widen before addition; a wrapped 32-bit VA cannot regain validity. */
        if (word(h) == 1 &&
            ((uint64_t)base + word(h + 8) >= 0x80000000U ||
             (uint64_t)base + word(h + 8) + word(h + 20) > 0x80000000U)) return 1;
        if (word(h) == 2) {
            dyn = word(h + 8); ds = word(h + 16);
        }
    }
    if (!ds || ds % 8 || ds > 4096 || !range(ph, count, dyn, ds, 4)) return 1;
    view->image = image;
    view->ph = ph;
    view->base = base;
    view->count = count;
    view->dyn = dyn;
    view->ds = ds;
    return 0;
}

#endif
