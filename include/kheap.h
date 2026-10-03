/*
 * kheap.h - growable kernel-private heap (x86-64)
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef KHEAP_H
#define KHEAP_H

#include "portab.h"

#ifdef __x86_64__

/*
 * Memory for kernel-only objects: KPROC records, address-space and
 * page-table metadata, and other descriptors no user program may name.
 *
 * Ownership: every block belongs to the kernel and to nobody else.  It is
 * never entered in a GEMDOS memory descriptor list (bdos/umem.c) and
 * never carries a process owner, so Mfree()/Pterm() cannot release it; in
 * the other direction, memory handed out by the process allocator
 * (procmem.h) is never a kheap block, so kfree() rejects it.
 *
 * The heap grows a page at a time from the physical page allocator, so --
 * unlike the fixed pool behind xmgetblk() -- it has no system-wide object
 * limit; the only limit is physical memory.  Pages are reached through
 * the physical-memory direct map, so kheap pointers are 64-bit higher-half
 * addresses and must never be stored in a 32-bit ABI field.
 *
 * kalloc() returns zeroed memory, aligned to 16 bytes, or NULL when size
 * is 0, absurdly large, or physical memory is exhausted.  A failed call
 * has no side effect: callers unwind and report the error (ENSMEM) to
 * their own caller.
 *
 * kfree(NULL) is a no-op.  Freeing a pointer that is not a live kheap
 * block (a double free, a foreign pointer) is detected on a best-effort
 * basis, counted in kheap_stats().bad_frees and otherwise ignored.
 *
 * Not interrupt-safe: call from process context only, which is the case
 * for every process-creation and teardown path.
 */
void *kalloc(ULONG size);
void kfree(void *p);

typedef struct {
    ULONG live_blocks;      /* kalloc'd blocks not yet kfree'd */
    ULONG pages;            /* physical pages currently held by the heap */
    ULONG bad_frees;        /* rejected kfree() calls */
} KHEAP_STATS;

void kheap_stats(KHEAP_STATS *stats);

#endif /* __x86_64__ */

#endif /* KHEAP_H */
