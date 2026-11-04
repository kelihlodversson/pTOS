/*
 * Initial ARM self relocation.  Every call is local and PC relative; no
 * globals, GOT pointers, libc or dynamically resolved functions are used.
 * The interpreter has R_ARM_RELATIVE records only (it is linked with hidden
 * symbols and -z now), in a DT_REL table: the addend is the word at the place.
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "runtime.h"

#define R_ARM_RELATIVE 23

static uint32_t word(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
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
int dl_bootstrap(const uint32_t *stack)
{
    const uint32_t *v;
    const unsigned char *image, *ph, *d, *table;
    uint32_t i, n, base = 0, count, off, dyn = 0, ds = 0, rel = 0, size = 0, ent = 0;
    uint32_t pass, at, info, argc = *stack;
    if (argc > 16384) return 1;
    v = stack + argc + 2;
    for (i = 0; i < 16384 && *v; i++, v++) ;
    if (i == 16384) return 1;
    for (v++, i = 0; i < 32 && v[0]; i++, v += 2)
        if (v[0] == 7) base = v[1];
    if (!base || base < 0xf000000 || base >= 0xfff0000 || i == 32) return 1;
    image = (const unsigned char *)(uintptr_t)base;
    if (word(image) != 0x464c457fUL || image[4] != 1 || image[5] != 1) return 1;
    off = word(image + 28);
    count = image[44] | ((uint32_t)image[45] << 8);
    if (!count || count > DL_MAX_PHDRS || off > 4096 || count * 32 > 4096 - off) return 1;
    ph = image + off;
    for (i = 0; i < count; i++)
        if (word(ph + i * 32) == 2) {
            dyn = word(ph + i * 32 + 8); ds = word(ph + i * 32 + 16);
        }
    if (!ds || ds % 8 || ds > 4096 || !range(ph, count, dyn, ds, 4)) return 1;
    d = image + dyn;
    for (i = 0; i < ds; i += 8) {
        uint32_t tag = word(d + i), value = word(d + i + 4);
        if (!tag) break;
        if (tag == 17) rel = value;
        if (tag == 18) size = value;
        if (tag == 19) ent = value;
        /* no RELA, PLT relocations, text relocations or RELR */
        if (tag == 7 || tag == 8 || tag == 9 || tag == 23 || tag == 2 || tag == 22 || tag == 36)
            return 1;
    }
    if (i == ds || (size && ent != 8) || size % 8 || size / 8 > DL_MAX_RELOCATIONS ||
        (size && !range(ph, count, rel, size, 4))) return 1;
    table = image + rel;
    /* Validate every write before applying any; metadata remains immutable. */
    for (pass = 0; pass < 2; pass++)
        for (n = 0; n < size; n += 8) {
            uint32_t value;
            unsigned char *target;
            at = word(table + n); info = word(table + n + 4);
            if (!info) continue;
            if (info != R_ARM_RELATIVE) return 1;
            if (!range(ph, count, at, 4, 6) ||
                ((uint64_t)at < (uint64_t)rel + size && (uint64_t)rel < (uint64_t)at + 4) ||
                ((uint64_t)at < (uint64_t)dyn + ds && (uint64_t)dyn < (uint64_t)at + 4)) return 1;
            for (i = 0; i < n; i += 8) {
                uint32_t prior = word(table + i);
                if (word(table + i + 4) && (uint64_t)at < (uint64_t)prior + 4 &&
                    (uint64_t)prior < (uint64_t)at + 4) return 1;
            }
            if ((uint64_t)base + (uint64_t)word(image + at) > UINT32_MAX) return 1;
            if (!pass) continue;
            target = (unsigned char *)(uintptr_t)(base + at);
            value = base + word(target);
            for (i = 0; i < 4; i++) target[i] = (unsigned char)(value >> (8 * i));
        }
    return 0;
}
