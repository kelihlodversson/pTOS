/*
 * Initial x32 self relocation. Every call is local and PC relative; no
 * globals, GOT pointers, libc or dynamically resolved functions are used.
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define BOOTSTRAP_ELF_DATA 1
#include "bootstrap_image.h"

int dl_bootstrap(const uint32_t *stack)
{
    struct bootstrap_image view;
    const unsigned char *image, *ph, *d, *table;
    uint32_t i, n, base, count, dyn, ds, rela = 0, size = 0, ent = 0;
    uint32_t pass, at, info, width;
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
        if (tag == 17 || tag == 18 || tag == 23 || tag == 22 || tag == 36) return 1;
    }
    if (i == ds || (size && ent != 12) || size % 12 || size / 12 > DL_MAX_RELOCATIONS ||
        (size && !range(ph, count, rela, size, 4))) return 1;
    table = image + (size ? rela : 0);
    /* Validate every write before applying any; metadata remains immutable. */
    for (pass = 0; pass < 2; pass++)
        for (n = 0; n < size; n += 12) {
            int32_t addend = (int32_t)word(table + n + 8);
            uint64_t value;
            unsigned char *target;
            at = word(table + n); info = word(table + n + 4);
            if (!info) continue;
            if (info != 8 && info != 38) return 1;
            width = info == 8 ? 4 : 8;
            if (!range(ph, count, at, width, 6) ||
                ((uint64_t)at < (uint64_t)rela + size && (uint64_t)rela < (uint64_t)at + width) ||
                ((uint64_t)at < (uint64_t)dyn + ds && (uint64_t)dyn < (uint64_t)at + width)) return 1;
            for (i = 0; i < n; i += 12) {
                uint32_t prior = word(table + i), kind = word(table + i + 4);
                uint32_t bytes = kind == 8 ? 4 : 8;
                if (kind && (uint64_t)at < (uint64_t)prior + bytes &&
                    (uint64_t)prior < (uint64_t)at + width) return 1;
            }
            value = (uint64_t)base + (uint64_t)(int64_t)addend;
            if (width == 4 && value > UINT32_MAX) return 1;
            if (!pass) continue;
            target = (unsigned char *)(uintptr_t)(base + at);
            for (i = 0; i < width; i++) target[i] = ((unsigned char *)&value)[i];
        }
    return 0;
}
