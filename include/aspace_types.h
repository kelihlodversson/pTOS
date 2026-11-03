/*
 * aspace_types.h - address types of the process address-space interface
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * The types include/procmem.h is written in, for a 32-bit MMU (ARM, m68k;
 * doc/mmu-address-space.txt).  An architecture with other widths has its
 * own aspace_types.h in include/arch/<arch>/, which the include path finds
 * first: include/arch/x86_64/aspace_types.h for the LP64 x86-64 kernel.
 */
#ifndef ASPACE_TYPES_H
#define ASPACE_TYPES_H

#include "portab.h"

typedef ULONG VADDR;        /* a user virtual address, or the length of a user range */
typedef ULONG PADDR;        /* a physical address (LPAE, #76, would make it wider) */
typedef ULONG ASPACE_ROOT;  /* the page-table root aspace_enter() hands back */

#define VADDR_C(x) x##UL    /* a VADDR constant */

/* The user half ends here (doc/mmu-address-space.txt): no user mapping may
 * reach or pass it. */
#define USER_VA_LIMIT VADDR_C(0x80000000)


#endif /* ASPACE_TYPES_H */
