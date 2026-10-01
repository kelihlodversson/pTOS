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

typedef struct kproc KPROC;
/* PD is the public GEMDOS basepage and is writable by its process. Keep
 * native pointers and the user-range snapshot in this kernel-only record. */
struct kproc {
    PD *pd;
    DTAINFO *dta;
#ifdef __x86_64__
    UBYTE *user_start;
    UBYTE *user_end;
#endif
    KPROC *next;
};

static KPROC *kproc_list;

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
    kproc = MGET(KPROC);
    if (!kproc)
        return FALSE;
    kproc->pd = pd;
    kproc->dta = (DTAINFO *)pd->p_cmdlin;
#ifdef __x86_64__
    /* Snapshot bounds before ring 3 can modify the public basepage. */
    kproc->user_start = USERPTR_TO_PTR(pd->p_env);
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
            xmfreblk(kproc);
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
BOOL kproc_validate_user_dta(UQUAD address)
{
    return kproc_validate_user_range(address, sizeof(DTAINFO));
}

BOOL kproc_validate_user_range(UQUAD address, ULONG size)
{
    KPROC *kproc = kproc_find(run);
    UQUAD start, end;

    /* The x32 ABI only carries 32-bit addresses.  Test the subtraction,
     * rather than address + size, so an attacker cannot wrap the range. */
    if (!kproc || !size || address > 0xffffffffULL)
        return FALSE;
    start = (UQUAD)(uintptr_t)kproc->user_start;
    end = (UQUAD)(uintptr_t)kproc->user_end;
    if (!start || end > 0x100000000ULL || end < start || end - start < size)
        return FALSE;
    return address >= start && address <= end - size;
}

BOOL kproc_copy_from_user(void *dst, UQUAD address, ULONG size)
{
    if (!kproc_validate_user_range(address, size))
        return FALSE;
    memcpy(dst, (const void *)(uintptr_t)address, size);
    return TRUE;
}
#endif
