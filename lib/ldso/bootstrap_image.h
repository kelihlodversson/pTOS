/*
 * ELF32 bootstrap validation shared by the interpreter entry points.
 * This code runs before self-relocation and uses only local calls and data.
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef LDSO_BOOTSTRAP_IMAGE_H
#define LDSO_BOOTSTRAP_IMAGE_H

#include "runtime.h"

#define ELF_DATA_LSB 1
#define ELF_DATA_MSB 2
#if !defined(BOOTSTRAP_ELF_DATA)
#error Define BOOTSTRAP_ELF_DATA for the interpreter target
#elif BOOTSTRAP_ELF_DATA != ELF_DATA_LSB && BOOTSTRAP_ELF_DATA != ELF_DATA_MSB
#error BOOTSTRAP_ELF_DATA must select ELF little or big endian
#endif

#define ELF_CLASS_32 1
#define ELF32_EHDR_SIZE 52U
#define ELF32_PHDR_SIZE 32U
#define ELF32_DYN_SIZE 8U
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define AT_BASE 7
#define BOOTSTRAP_MAX_START_ENTRIES 16384U
#define BOOTSTRAP_MAX_AUX_ENTRIES 32U
#define BOOTSTRAP_PAGE_SIZE 4096U
#define BOOTSTRAP_USER_LIMIT 0x80000000U

static uint32_t halfword(const unsigned char *p)
{
#if BOOTSTRAP_ELF_DATA == ELF_DATA_MSB
    return ((uint32_t)p[0] << 8) | p[1];
#else
    return p[0] | ((uint32_t)p[1] << 8);
#endif
}

static uint32_t word(const unsigned char *p)
{
#if BOOTSTRAP_ELF_DATA == ELF_DATA_MSB
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
    for (i = 0; i < count; i++, ph += ELF32_PHDR_SIZE)
        if (word(ph) == PT_LOAD && (word(ph + 24) & flags) == flags && va >= word(ph + 8) &&
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

    if (argc > BOOTSTRAP_MAX_START_ENTRIES) return 1;
    v = stack + argc + 2;
    for (i = 0; i < BOOTSTRAP_MAX_START_ENTRIES && *v; i++, v++) ;
    if (i == BOOTSTRAP_MAX_START_ENTRIES) return 1;
    for (v++, i = 0; i < BOOTSTRAP_MAX_AUX_ENTRIES && v[0]; i++, v += 2)
        if (v[0] == AT_BASE) base = v[1];
    /* Shared-VM loading may rebase the interpreter outside its old window.
     * Keep the complete initial header/PHDR page inside positive user VA. */
    if (!base || (base & (BOOTSTRAP_PAGE_SIZE - 1)) ||
        base > BOOTSTRAP_USER_LIMIT - BOOTSTRAP_PAGE_SIZE ||
        i == BOOTSTRAP_MAX_AUX_ENTRIES) return 1;
    image = (const unsigned char *)(uintptr_t)base;
    if (image[0] != 0x7f || image[1] != 'E' || image[2] != 'L' || image[3] != 'F' ||
        image[4] != ELF_CLASS_32 || image[5] != BOOTSTRAP_ELF_DATA ||
        halfword(image + 40) != ELF32_EHDR_SIZE || halfword(image + 42) != ELF32_PHDR_SIZE) return 1;
    off = word(image + 28);
    count = halfword(image + 44);
    if (!count || count > DL_MAX_PHDRS || off > BOOTSTRAP_PAGE_SIZE ||
        count * ELF32_PHDR_SIZE > BOOTSTRAP_PAGE_SIZE - off) return 1;
    ph = image + off;
    for (i = 0; i < count; i++) {
        const unsigned char *h = ph + i * ELF32_PHDR_SIZE;
        /* Validate every load before forming any dynamic/table/target pointer.
         * Widen before addition; a wrapped 32-bit VA cannot regain validity. */
        if (word(h) == PT_LOAD &&
            ((uint64_t)base + word(h + 8) >= BOOTSTRAP_USER_LIMIT ||
             (uint64_t)base + word(h + 8) + word(h + 20) > BOOTSTRAP_USER_LIMIT)) return 1;
        if (word(h) == PT_DYNAMIC) {
            dyn = word(h + 8); ds = word(h + 16);
        }
    }
    if (!ds || ds % ELF32_DYN_SIZE || ds > BOOTSTRAP_PAGE_SIZE ||
        !range(ph, count, dyn, ds, DL_PF_R)) return 1;
    view->image = image;
    view->ph = ph;
    view->base = base;
    view->count = count;
    view->dyn = dyn;
    view->ds = ds;
    return 0;
}

#endif
