/*
 * aspace_types.h - address types of the process address-space interface
 *                  (x86-64)
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * The x86-64 version of include/aspace_types.h: the kernel is LP64, so a
 * user address of an x32 process is computed in 64 bits (it is checked
 * against the 32-bit ABI limit, not truncated to it), physical addresses
 * reach above 4 GiB, and the root is a CR3 value.
 */
#ifndef ASPACE_TYPES_H
#define ASPACE_TYPES_H

#include "portab.h"

typedef UQUAD VADDR;        /* a user virtual address, or the length of a user range */
typedef UQUAD PADDR;        /* a physical address */
typedef UQUAD ASPACE_ROOT;  /* a CR3 value */

#define VADDR_C(x) x##ULL   /* a VADDR constant */

/* The 32-bit ABI limit: no user mapping may reach or pass this address. */
#define USER_VA_LIMIT VADDR_C(0x100000000)


#endif /* ASPACE_TYPES_H */
