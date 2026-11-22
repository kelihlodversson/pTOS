/*
 * Initial ARM self relocation.  Every call is local and PC relative; no
 * globals, GOT pointers, libc or dynamically resolved functions are used.
 * The interpreter has R_ARM_RELATIVE records only (it is linked with hidden
 * symbols and -z now), in a DT_REL table: the addend is the word at the place.
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define BOOTSTRAP_ELF_DATA 1
#include "bootstrap_image.h"

#define R_ARM_RELATIVE 23

int dl_bootstrap(const uint32_t *stack)
{
    struct bootstrap_image view;
    const unsigned char *image, *ph, *d, *table;
    uint32_t i, n, base, count, dyn, ds, rel = 0, size = 0, ent = 0;
    uint32_t pass, at, info;
    if (bootstrap_image(stack, &view)) return 1;
    image = view.image; ph = view.ph;
    base = view.base; count = view.count;
    dyn = view.dyn; ds = view.ds;
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
    table = image + (size ? rel : 0);
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
