/*
 * kproc.c - kernel-private process state
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "string.h"
#include "fs.h"
#include "kproc.h"
#include "mem.h"
#include "bdosstub.h"
#include "biosext.h"
#ifdef __x86_64__
#include "kheap.h"
#include "procmem.h"
#endif

/* The option gates only the record itself: the #else stubs stay in every
 * link so the public p_xdta field keeps working verbatim. */

#if CONF_WITH_KPROC

typedef struct kproc KPROC;
/* PD is the public GEMDOS basepage and is writable by its process. Keep
 * native pointers and the user-range snapshot in this kernel-only record. */
struct kproc {
    PD *pd;
    DTAINFO *dta;
#ifdef __x86_64__
    /* The two separate allocations a process owns: its environment block
     * and its basepage/TPA/stack.  The window does not keep them adjacent,
     * and whatever lies between them is somebody else's. */
    UBYTE *env_start;
    UBYTE *env_end;
    UBYTE *user_start;
    UBYTE *user_end;
    X86_64_ASPACE *aspace;      /* ring-3 page tables, NULL until prepared */
#endif
    KPROC *next;
};

static KPROC *kproc_list;

/* KPROC records are kernel-only objects.  On x86-64 they live in the
 * growable kernel heap, so the number of processes is bounded by memory
 * and not by the fixed xmgetblk() pool; they are never GEMDOS memory. */
#ifdef __x86_64__
#define KPROC_ALLOC()   ((KPROC *)kalloc(sizeof(KPROC)))
#define KPROC_FREE(k)   kfree(k)
#else
#define KPROC_ALLOC()   MGET(KPROC)
#define KPROC_FREE(k)   xmfreblk(k)
#endif

static KPROC *kproc_find(PD *pd)
{
    KPROC *kproc;

    for (kproc = kproc_list; kproc; kproc = kproc->next)
        if (kproc->pd == pd)
            return kproc;
    return NULL;
}

BOOL kproc_create(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    if (kproc)
        return TRUE;
    kproc = KPROC_ALLOC();
    if (!kproc)
        return FALSE;
    kproc->pd = pd;
    kproc->dta = (DTAINFO *)pd->p_cmdlin;
#ifdef __x86_64__
    /* Snapshot bounds before ring 3 can modify the public basepage. */
    kproc->env_start = USERPTR_TO_PTR(pd->p_env);
    kproc->env_end = kproc->env_start +
                     x86_64_procmem_size(kproc->env_start);
    kproc->user_start = (UBYTE *)pd;
    kproc->user_end = USERPTR_TO_PTR(pd->p_hitpa);
#endif
    kproc->next = kproc_list;
    kproc_list = kproc;
    return TRUE;
}

void kproc_destroy(PD *pd)
{
    KPROC **link;

    for (link = &kproc_list; *link; link = &(*link)->next)
        if ((*link)->pd == pd) {
            KPROC *kproc = *link;
            *link = kproc->next;
#ifdef __x86_64__
            /* Unlinked first, so the record is gone before its address
             * space is torn down: a second kproc_destroy() for the same
             * PD finds nothing and cannot free either twice. */
            x86_64_aspace_destroy(kproc->aspace);
#endif
            KPROC_FREE(kproc);
            return;
        }
}

void kproc_set_dta(PD *pd, DTAINFO *dta)
{
    KPROC *kproc = kproc_find(pd);

    if (!kproc) {
#ifdef __x86_64__
        KINFO(("Missing kernel process record for %p\n", pd));
        halt();
#else
        pd->p_xdta = PTR_TO_USERPTR_UNCHECKED(dta);
        return;
#endif
    }
    kproc->dta = dta;
    pd->p_xdta = PTR_TO_USERPTR_UNCHECKED(dta);
}

DTAINFO *kproc_get_dta(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

#ifdef __x86_64__
    /* pd->p_xdta lives in the basepage, which is writable by the owning
     * process, so it must never be trusted as a DTA pointer: a ring-3
     * caller could store any address there and then have Fsfirst()/
     * Fsnext() write through it at CPL0, bypassing the Fsetdta() check
     * in kproc_validate_user_dta() below. The kernel-private value is
     * authoritative, and is only ever set from a validated source. */
    if (!kproc) {
        KINFO(("Missing kernel process record for %p\n", pd));
        halt();
    }
    return kproc->dta;
#else
    /* No memory protection to defeat here: p_xdta is itself the full
     * native pointer, so honour a direct write to it as TOS always has. */
    if (kproc && PTR_TO_USERPTR_UNCHECKED(kproc->dta) == pd->p_xdta)
        return kproc->dta;
    return (DTAINFO *)USERPTR_TO_PTR(pd->p_xdta);
#endif
}

#ifdef __x86_64__
BOOL kproc_prepare_user(PD *pd, PD *parent)
{
    KPROC *kproc = kproc_find(pd);
    X86_64_ASPACE *as;
    UQUAD env = (UQUAD)pd->p_env;
    UQUAD tpa = (UQUAD)(uintptr_t)pd;
    UQUAD hitpa = (UQUAD)pd->p_hitpa;
    ULONG envbytes = x86_64_procmem_size((void *)(uintptr_t)env);

    if (!kproc)
        return FALSE;
    if (kproc->aspace)
        return TRUE;                /* already prepared */
    if (!envbytes || hitpa <= tpa)
        return FALSE;               /* not backed by process allocations */

    as = x86_64_aspace_create();
    if (!as)
        return FALSE;

    /*
     * The process's own environment block and its basepage + TPA + stack
     * (the PD is the first thing in the TPA allocation, p_hitpa its end):
     * two separate allocations, wherever the window put them.  They are
     * user read/write/execute because the flat x32 image has a single RWX
     * segment.  Then its parent's basepage, kernel-only: xterm() writes
     * the exit code through it from ring 0 while this address space is
     * still loaded.  Any failure unwinds the whole address space.
     */
    if (!x86_64_aspace_map_procmem(as, env, envbytes,
                                   ASPACE_PROT_WRITE | ASPACE_PROT_USER) ||
        !x86_64_aspace_map_procmem(as, tpa, hitpa - tpa,
                                   ASPACE_PROT_WRITE | ASPACE_PROT_EXEC | ASPACE_PROT_USER) ||
        !x86_64_aspace_map_procmem(as, (UQUAD)(uintptr_t)parent, sizeof(PD),
                                   ASPACE_PROT_WRITE)) {
        x86_64_aspace_destroy(as);
        return FALSE;
    }
    kproc->aspace = as;
    return TRUE;
}

UQUAD kproc_user_pml4(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    return (kproc && kproc->aspace) ? x86_64_aspace_pml4(kproc->aspace) : 0;
}

ULONG kproc_count(void)
{
    ULONG n = 0;
    KPROC *kproc;

    for (kproc = kproc_list; kproc; kproc = kproc->next)
        n++;
    return n;
}

BOOL kproc_validate_user_dta(UQUAD address)
{
    return kproc_validate_user_range(address, sizeof(DTAINFO));
}

/* True iff [address, address + size) lies inside [start, end). */
static BOOL range_within(UQUAD address, ULONG size, const UBYTE *start_p, const UBYTE *end_p)
{
    UQUAD start = (UQUAD)(uintptr_t)start_p;
    UQUAD end = (UQUAD)(uintptr_t)end_p;

    if (!start || end > 0x100000000ULL || end < start || end - start < size)
        return FALSE;
    return address >= start && address <= end - size;
}

BOOL kproc_validate_user_range(UQUAD address, ULONG size)
{
    KPROC *kproc = kproc_find(run);

    /* The x32 ABI only carries 32-bit addresses.  Test the subtraction,
     * rather than address + size, so an attacker cannot wrap the range.
     * The range must lie wholly inside the process's environment block or
     * wholly inside its basepage/TPA/stack, never across the gap between
     * them. */
    if (!kproc || !size || address > 0xffffffffULL)
        return FALSE;
    return range_within(address, size, kproc->user_start, kproc->user_end) ||
           range_within(address, size, kproc->env_start, kproc->env_end);
}

BOOL kproc_copy_from_user(void *dst, UQUAD address, ULONG size)
{
    if (!kproc_validate_user_range(address, size))
        return FALSE;
    memcpy(dst, (const void *)(uintptr_t)address, size);
    return TRUE;
}

BOOL kproc_copy_to_user(UQUAD address, const void *src, ULONG size)
{
    if (!kproc_validate_user_range(address, size))
        return FALSE;
    memcpy((void *)(uintptr_t)address, src, size);
    return TRUE;
}
#endif

#else /* CONF_WITH_KPROC */

/* No kernel-private record: honour the writable public p_xdta field directly,
 * exactly as TOS always has on targets with no memory protection to defeat.
 * The record exists solely to carry native pointers and trust state that a
 * user-writable 32-bit basepage field cannot represent. */
BOOL kproc_create(PD *pd)
{
    (void)pd;
    return TRUE;
}

void kproc_destroy(PD *pd)
{
    (void)pd;
}

void kproc_set_dta(PD *pd, DTAINFO *dta)
{
    pd->p_xdta = PTR_TO_USERPTR_UNCHECKED(dta);
}

DTAINFO *kproc_get_dta(PD *pd)
{
    return (DTAINFO *)USERPTR_TO_PTR(pd->p_xdta);
}

#endif /* CONF_WITH_KPROC */
