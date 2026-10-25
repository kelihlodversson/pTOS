/*
 * x32 relocation widths and psABI arithmetic, not LP64 ELF64 rules (#660).
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "relocate.h"

#define R_NONE        0
#define R_64          1
#define R_PC32        2
#define R_GLOB_DAT    6
#define R_JUMP_SLOT   7
#define R_RELATIVE    8
#define R_32         10
#define R_32S        11
#define R_RELATIVE64 38

static int x32_kind(uint32_t type, DL_RELOC_KIND *out)
{
    DL_RELOC_KIND k = { 4, DL_RELOC_SYMBOL };

    switch (type) {
    case R_NONE: k.width = 0; k.flags = 0; break;
    case R_64: k.width = 8; break;
    case R_PC32: case R_32: case R_32S: case R_GLOB_DAT: break;
    case R_JUMP_SLOT: k.flags |= DL_RELOC_PLT; break;
    case R_RELATIVE: k.flags = DL_RELOC_RELATIVE; break;
    case R_RELATIVE64: k.width = 8; k.flags = DL_RELOC_RELATIVE; break;
    default: return DL_UNSUPPORTED;
    }
    *out = k;
    return DL_OK;
}

static int x32_value(uint32_t type, uint32_t bias, uint32_t symbol,
                     int32_t addend, uint32_t place, uint64_t *out)
{
    int64_t v;

    switch (type) {
    case R_NONE: *out = 0; return DL_OK;
    case R_GLOB_DAT: case R_JUMP_SLOT:
        *out = symbol;                 /* psABI S, not S+A */
        return DL_OK;
    case R_64:
        *out = (uint64_t)symbol + (uint64_t)(int64_t)addend;
        return DL_OK;
    case R_RELATIVE64:
        *out = (uint64_t)bias + (uint64_t)(int64_t)addend;
        return DL_OK;
    case R_RELATIVE: v = (int64_t)bias + addend; break;
    case R_32: case R_32S: v = (int64_t)symbol + addend; break;
    case R_PC32: v = (int64_t)symbol + addend - place; break;
    default: return DL_UNSUPPORTED;
    }
    if (type == R_32S || type == R_PC32) {
        if (v < INT32_MIN || v > INT32_MAX)
            return DL_OVERFLOW;
    } else if (v < 0 || v > UINT32_MAX) {
        return DL_OVERFLOW;
    }
    *out = (uint32_t)v;
    return DL_OK;
}

const DL_RELOC_BACKEND dl_x32_reloc_backend = { x32_kind, x32_value };
