/*
 * m68k relocations of the userspace runtime linker (#694).  The tables are
 * DT_RELA, as every m68k linker makes them.
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "relocate.h"

#define R_68K_NONE       0
#define R_68K_32         1
#define R_68K_GLOB_DAT  20
#define R_68K_JMP_SLOT  21
#define R_68K_RELATIVE  22

static int m68k_kind(uint32_t type, DL_RELOC_KIND *out)
{
    DL_RELOC_KIND k = { 4, DL_RELOC_SYMBOL };

    switch (type) {
    case R_68K_NONE: k.width = 0; k.flags = 0; break;
    case R_68K_32: case R_68K_GLOB_DAT: break;
    case R_68K_JMP_SLOT: k.flags |= DL_RELOC_PLT; break;
    case R_68K_RELATIVE: k.flags = DL_RELOC_RELATIVE; break;
    default: return DL_UNSUPPORTED;     /* the PC-relative ones (they are text relocations), TLS */
    }
    *out = k;
    return DL_OK;
}

static int m68k_value(uint32_t type, uint32_t bias, uint32_t symbol,
                      int32_t addend, uint32_t place, uint64_t *out)
{
    int64_t v;

    (void)place;
    switch (type) {
    case R_68K_NONE: *out = 0; return DL_OK;
    case R_68K_32: case R_68K_GLOB_DAT: case R_68K_JMP_SLOT:
        /* S + A, modulo 2**32.  (The linkers write 0 as the addend of a GOT or PLT
         * entry; one that does not means what the relocation says.) */
        *out = (uint32_t)(symbol + (uint32_t)addend);
        return DL_OK;
    case R_68K_RELATIVE: v = (int64_t)bias + (int64_t)addend; break;
    default: return DL_UNSUPPORTED;
    }
    if (v < 0 || v > UINT32_MAX)
        return DL_OVERFLOW;
    *out = (uint32_t)v;
    return DL_OK;
}

const DL_RELOC_BACKEND dl_reloc_backend = { m68k_kind, m68k_value, 0 };
