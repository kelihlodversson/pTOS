/*
 * userptr.h - shared native-user pointer representation
 *
 * Copyright (C) 2026 The pTOS development team.
 * This file is distributed under the GPL, version 2 or any later version.
 */
#ifndef USERPTR_H
#define USERPTR_H

#include "portab.h"

#ifdef __LP64__
#define USERPTR_T(type) ULONG
/* Widen the ABI address only. The caller must validate ownership,
 * mapping and permissions before using it in the current address space. */
#define USERPTR_TO_PTR(up) ((void *)(uintptr_t)(ULONG)(up))
/* Do not silently truncate an address that cannot fit the user ABI. */
static inline ULONG ptr_to_userptr(const void *p)
{
    UQUAD addr = (UQUAD)(uintptr_t)p;

    if (addr > 0xFFFFFFFFULL)
        __builtin_trap();
    return (ULONG)addr;
}
#define PTR_TO_USERPTR(p) ptr_to_userptr(p)
/* Legacy bootstrap fields also store truncated supervisor addresses.
 * Those fields must not be treated as dereferenceable user pointers. */
#define PTR_TO_USERPTR_UNCHECKED(p) ((ULONG)(uintptr_t)(p))
#else
#define USERPTR_T(type) type *
#define USERPTR_TO_PTR(up) ((void *)(up))
#define PTR_TO_USERPTR(p) (p)
#define PTR_TO_USERPTR_UNCHECKED(p) (p)
#endif

#endif /* USERPTR_H */
