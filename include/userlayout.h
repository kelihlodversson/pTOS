/*
 * userlayout.h - allocatable user VA ranges, shared by ELF and VM admission
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef USERLAYOUT_H
#define USERLAYOUT_H

#include "procmem.h"

static __inline__ BOOL user_layout_range(VADDR va, VADDR bytes, VADDR limit)
{
    VADDR end;

    if (!bytes || va < USER_ALLOC_MIN || va >= limit ||
        limit > USER_VA_LIMIT || bytes > limit - va)
        return FALSE;
    end = va + bytes;
    if (va < USER_SYSTEM_LIMIT && USER_SYSTEM_BASE < end)
        return FALSE;
    return TRUE;
}

#endif
