/*
 * Initial m68k self relocation.  Every call is local and PC relative; no
 * globals, GOT pointers, libc or dynamically resolved functions are used.
 * The interpreter has R_68K_RELATIVE records only (it is linked with hidden
 * symbols and -z now), in a DT_RELA table.  The object is big endian, so the
 * words are put together byte by byte.
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define BOOTSTRAP_ELF_DATA 2
#include "bootstrap_image.h"

#define R_68K_RELATIVE 22

int dl_bootstrap(const uint32_t *stack)
{
    struct bootstrap_image view;
    const unsigned char *image, *ph, *d, *table;
    uint32_t i, n, base, count, dyn, ds, rela = 0, size = 0, ent = 0;
    uint32_t pass, at, info;
    if (bootstrap_image(stack, &view)) return 1;
    image = view.image; ph = view.ph;
    base = view.base; count = view.count;
    dyn = view.dyn; ds = view.ds;
    d = image + dyn;
    for (i = 0; i < ds; i += 8) {
        uint32_t tag = word(d + i), value = word(d + i + 4);
        if (!tag) break;
        if (tag == 7) rela = value;
        if (tag == 8) size = value;
        if (tag == 9) ent = value;
        /* no REL, PLT relocations, text relocations or RELR */
        if (tag == 17 || tag == 18 || tag == 19 || tag == 23 || tag == 2 || tag == 22 || tag == 36)
            return 1;
    }
    if (i == ds || (size && ent != 12) || size % 12 || size / 12 > DL_MAX_RELOCATIONS ||
        (size && !range(ph, count, rela, size, 4))) return 1;
    table = image + (size ? rela : 0);
    /* Validate every write before applying any; metadata remains immutable. */
    for (pass = 0; pass < 2; pass++)
        for (n = 0; n < size; n += 12) {
            uint32_t value;
            unsigned char *target;
            at = word(table + n); info = word(table + n + 4);
            if (!info) continue;
            if (info != R_68K_RELATIVE) return 1;
            if (!range(ph, count, at, 4, 6) ||
                ((uint64_t)at < (uint64_t)rela + size && (uint64_t)rela < (uint64_t)at + 4) ||
                ((uint64_t)at < (uint64_t)dyn + ds && (uint64_t)dyn < (uint64_t)at + 4)) return 1;
            for (i = 0; i < n; i += 12) {
                uint32_t prior = word(table + i);
                if (word(table + i + 4) && (uint64_t)at < (uint64_t)prior + 4 &&
                    (uint64_t)prior < (uint64_t)at + 4) return 1;
            }
            if ((uint64_t)base + (uint64_t)word(table + n + 8) > UINT32_MAX) return 1;
            if (!pass) continue;
            target = (unsigned char *)(uintptr_t)(base + at);
            value = base + word(table + n + 8);
            for (i = 0; i < 4; i++) target[i] = (unsigned char)(value >> (24 - 8 * i));
        }
    return 0;
}
