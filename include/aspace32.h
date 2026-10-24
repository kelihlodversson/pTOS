/*
 * aspace32.h - address spaces on a 32-bit MMU: the generic core and the
 *              page-table backend it calls
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef ASPACE32_H
#define ASPACE32_H

#include "portab.h"
#include "config.h"
#include "procmem.h"
#include "pmem32.h"

#if CONF_WITH_USER_ASPACE && !defined(__x86_64__)

/* The process window of include/procmem.h: where procmem blocks (basepage,
 * environment and TPA) are, in every address space that maps them. */
#define PROCMEM_WINDOW_BASE     VADDR_C(0x00200000)
#define PROCMEM_WINDOW_BYTES    VADDR_C(0x00200000)

/* what the page-table backend keeps beside the hardware bits of a leaf */
#define ASPACE_LEAF_OWNED   0x1     /* a private page: freed with the address space */

typedef PADDR (*ASPACE_TABLE_ALLOC)(void *ctx);   /* a zeroed page, PMEM32_NONE if none */

/*
 * The backend of the architecture (bios/arch/<arch>/aspace_hw.c): the format
 * of the page tables of the user half, nothing else.  The generic core
 * (bios/aspace.c) owns what has to be freed, and records every table page
 * that the backend asks for through the allocator it is given.
 */
ULONG aspace_hw_root_pages(void);               /* size of a root, in pages */
ULONG aspace_hw_root_align(void);               /* its alignment, in pages */
/* once, before the first root: what is shared by every root (the kernel half of an m68k tree) */
BOOL aspace_hw_init(void);
/* a new root, zeroed: the part that is the same in every address space */
void aspace_hw_init_root(PADDR root);

/* one 4 KiB leaf; FALSE if a table page could not be had (nothing mapped) */
BOOL aspace_hw_map(PADDR root, VADDR va, PADDR phys, UWORD prot, UWORD flags,
                   ASPACE_TABLE_ALLOC alloc, void *ctx);
/* the leaf at va: its page, effective permissions and flags; FALSE if none */
BOOL aspace_hw_lookup(PADDR root, VADDR va, PADDR *phys, UWORD *prot, UWORD *flags);
/* removes the leaf at va; TRUE if there was one */
BOOL aspace_hw_unmap(PADDR root, VADDR va);
/* changes the permissions and flags of the leaf at va, keeping its page */
BOOL aspace_hw_update(PADDR root, VADDR va, UWORD prot, UWORD flags);
/* calls back for every present leaf of the user half */
void aspace_hw_walk(PADDR root,
                    void (*fn)(VADDR va, PADDR phys, UWORD flags, void *ctx), void *ctx);
/* cuts the table pages that are in pages[] off the root (a failed mapping) */
void aspace_hw_drop_tables(PADDR root, const PADDR *pages, ULONG n);

/* loads root as the address space of user mode (and of the supervisor, for
 * what it reaches below USER_VA_LIMIT) and drops the translations of the
 * previous one */
void aspace_hw_switch(PADDR root);
PADDR aspace_hw_current(void);
/* the translations of the address space in use are stale (a leaf changed) */
void aspace_hw_flush(void);
/* code was written through the data side (a program loaded into new pages): make
 * it visible to instruction fetch before the process runs */
void aspace_hw_code_changed(void);

/* the generic core */
BOOL aspace_kernel_init(void);          /* the kernel's own address space; after pmem32_init() */
PADDR aspace_kernel_root(void);
BOOL procmem_window_init(PADDR *phys);  /* bios/procmem.c */

#endif /* CONF_WITH_USER_ASPACE */

#endif /* ASPACE32_H */
