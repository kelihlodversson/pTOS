/*
 * ARM (EABI, ARM state) relocations of the userspace runtime linker (#693).
 * The tables are DT_REL: the addend is the word at the place.
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "relocate.h"

#define R_ARM_NONE       0
#define R_ARM_ABS32      2
#define R_ARM_REL32      3
#define R_ARM_GLOB_DAT  21
#define R_ARM_JUMP_SLOT 22
#define R_ARM_RELATIVE  23

static int arm_kind(uint32_t type, DL_RELOC_KIND *out)
{
    DL_RELOC_KIND k = { 4, DL_RELOC_SYMBOL };

    switch (type) {
    case R_ARM_NONE: k.width = 0; k.flags = 0; break;
    case R_ARM_ABS32: case R_ARM_REL32: case R_ARM_GLOB_DAT: break;
    case R_ARM_JUMP_SLOT: k.flags |= DL_RELOC_PLT; break;
    case R_ARM_RELATIVE: k.flags = DL_RELOC_RELATIVE; break;
    default: return DL_UNSUPPORTED;     /* COPY, TLS, Thumb and the like */
    }
    *out = k;
    return DL_OK;
}

static int arm_value(uint32_t type, uint32_t bias, uint32_t symbol,
                     int32_t addend, uint32_t place, uint64_t *out)
{
    int64_t v;

    switch (type) {
    case R_ARM_NONE: *out = 0; return DL_OK;
    case R_ARM_GLOB_DAT: case R_ARM_JUMP_SLOT:
        *out = symbol;                  /* S: the slot holds a stub address to start with */
        return DL_OK;
    case R_ARM_ABS32:                   /* modulo 2**32, as the ABI has it: the addend may be negative */
        *out = (uint32_t)(symbol + (uint32_t)addend);
        return DL_OK;
    case R_ARM_RELATIVE:                /* B + A modulo 2**32, as ABS32: the addend may be negative */
        *out = (uint32_t)(bias + (uint32_t)addend);
        return DL_OK;
    case R_ARM_REL32:
        v = (int64_t)symbol + (int64_t)addend - (int64_t)place;
        if (v < INT32_MIN || v > INT32_MAX)
            return DL_OVERFLOW;
        *out = (uint32_t)v;
        return DL_OK;
    default: return DL_UNSUPPORTED;
    }
    if (v < 0 || v > UINT32_MAX)
        return DL_OVERFLOW;
    *out = (uint32_t)v;
    return DL_OK;
}

const DL_RELOC_BACKEND dl_reloc_backend = { arm_kind, arm_value, 1 };
