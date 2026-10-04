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
#include "x32image.h"
#endif

#ifdef __x86_64__
#define KPROC_BORROWS 8
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
    UQUAD env_gen;              /* identities of the two blocks recorded, so */
    UQUAD tpa_gen;              /* a freed-and-reused address is not trusted */
    UBYTE *user_start;
    UBYTE *user_end;
    X86_64_ASPACE *aspace;      /* ring-3 page tables, NULL until prepared */
    BOOL started;               /* proc_go() has launched it */
    PD *parent;                 /* who launched it: the trusted copy of p_parent */
    const X32_IMAGE *image;     /* built-in program to map private, or NULL */
    UQUAD entry;                /* its entry point once loaded, else 0 */
    UQUAD stack_top;            /* its private stack's top once loaded, else 0 */
    UQUAD kstack_phys;          /* its kernel stack (system calls), 0 if none */
    UQUAD kstack_top;           /* initial stack pointer of that stack */
    struct {                    /* blocks of a child being launched from this */
        UQUAD va, bytes;        /* process, mapped supervisor-only into its */
    } borrowed[KPROC_BORROWS];  /* address space until they are freed */
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
    kproc->env_gen = x86_64_procmem_gen(kproc->env_start);
    kproc->tpa_gen = x86_64_procmem_gen(pd);
    kproc->user_start = (UBYTE *)pd;
    kproc->user_end = USERPTR_TO_PTR(pd->p_hitpa);
    /* The basepage is writable by whoever holds it until launch, so the
     * snapshot is the only trusted bound: the TPA must lie within the one
     * allocation the basepage sits at the start of, whatever p_hitpa says. */
    {
        UBYTE *alloc_end = kproc->user_start +
                           x86_64_procmem_size(kproc->user_start);

        if (kproc->user_end > alloc_end || kproc->user_end < kproc->user_start)
            kproc->user_end = alloc_end;
    }
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
            x86_64_kstack_free(kproc->kstack_phys);
#endif
            KPROC_FREE(kproc);
            return;
        }
}

#ifdef __x86_64__
void kproc_set_parent(PD *pd, PD *parent)
{
    KPROC *kproc = kproc_find(pd);

    if (!kproc) {
        KINFO(("Missing kernel process record for %p\n", pd));
        halt();
    }
    kproc->parent = parent;
}

PD *kproc_get_parent(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    /* pd->p_parent lives in the basepage, which the owning process can
     * write: Pterm() must never steer where the kernel writes the exit
     * code (and then continues running) by it.  The launcher recorded here
     * is the only trusted value. */
    if (!kproc || !kproc->parent) {
        KINFO(("Missing parent for process record %p\n", pd));
        halt();
    }
    return kproc->parent;
}

void kproc_mark_started(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    if (kproc)
        kproc->started = TRUE;
}

BOOL kproc_discard(PD *pd)
{
    KPROC *kproc = kproc_find(pd);
    BOOL unstarted = kproc && !kproc->started;

    kproc_destroy(pd);
    return unstarted;
}
#endif

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
    UQUAD env, tpa, hitpa;
    ULONG envbytes;

    if (!kproc)
        return FALSE;
    if (kproc->aspace) {
        /* Already prepared -- but a process that has been launched has handed
         * its kernel stack to gouser(), and cannot be launched a second time
         * (Pexec(PE_GO) of its own basepage, from ring 3): refuse, where the
         * launch can still fail cleanly. */
        return kproc->kstack_phys != 0;
    }
    env = (UQUAD)(uintptr_t)kproc->env_start;
    tpa = (UQUAD)(uintptr_t)kproc->user_start;
    hitpa = (UQUAD)(uintptr_t)kproc->user_end;
    envbytes = (ULONG)(kproc->env_end - kproc->env_start);
    /* Map only what was recorded when the basepage was made.  The public
     * fields may have been rewritten since (PE_BASEPAGE/PE_LOAD hand the
     * caller a writable basepage), so they must still agree with it:
     * otherwise a launch could map a neighbouring allocation. */
    if (!envbytes || hitpa <= tpa ||
        !kproc->env_gen || !kproc->tpa_gen ||
        x86_64_procmem_gen(kproc->env_start) != kproc->env_gen ||
        x86_64_procmem_gen(kproc->user_start) != kproc->tpa_gen ||
        (UQUAD)pd->p_env != env || (UQUAD)pd->p_hitpa > hitpa ||
        (UQUAD)pd->p_hitpa <= tpa)
        return FALSE;

    as = x86_64_aspace_create();
    if (!as)
        return FALSE;
    kproc->kstack_phys = x86_64_kstack_alloc(&kproc->kstack_top);
    if (!kproc->kstack_phys) {
        x86_64_aspace_destroy(as);
        return FALSE;
    }

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
        x86_64_kstack_free(kproc->kstack_phys);
        kproc->kstack_phys = 0;
        return FALSE;
    }
    /*
     * A built-in image gets its segments and a stack of its own, as private
     * pages at the fixed addresses of include/procmem.h's layout.  (A bare
     * basepage's TPA is no stack: it is the basepage and little else.)
     */
    if (kproc->image &&
        (!x86_64_x32image_load(as, kproc->image, &kproc->entry) ||
         !x86_64_aspace_map_private(as, X86_64_USER_STACK_TOP - X86_64_USER_STACK_SIZE,
                                    X86_64_USER_STACK_SIZE,
                                    ASPACE_PROT_WRITE | ASPACE_PROT_USER))) {
        kproc->entry = 0;
        x86_64_aspace_destroy(as);
        x86_64_kstack_free(kproc->kstack_phys);
        kproc->kstack_phys = 0;
        return FALSE;
    }
    if (kproc->image)
        kproc->stack_top = X86_64_USER_STACK_TOP - 8;   /* RSP + 8 divisible by 16 */
    kproc->aspace = as;
    return TRUE;
}

BOOL kproc_set_image(PD *pd, const X32_IMAGE *image)
{
    KPROC *kproc = kproc_find(pd);

    if (!kproc || kproc->aspace || !x86_64_x32image_check(image, NULL))
        return FALSE;
    kproc->image = image;
    return TRUE;
}

BOOL kproc_borrow(PD *launcher, void *block)
{
    KPROC *kproc = kproc_find(launcher);
    UQUAD va = (UQUAD)(uintptr_t)block;
    UQUAD bytes = x86_64_procmem_size(block);
    int i;

    if (!kproc || !kproc->aspace)
        return TRUE;                /* a ring-0 launcher reaches all of it */
    if (!bytes)
        return FALSE;
    for (i = 0; i < KPROC_BORROWS; i++)
        if (!kproc->borrowed[i].bytes) {
            if (!x86_64_aspace_borrow(kproc->aspace, va, bytes))
                return FALSE;
            kproc->borrowed[i].va = va;
            kproc->borrowed[i].bytes = bytes;
            return TRUE;
        }
    return FALSE;
}

void kproc_unborrow(void *block)
{
    UQUAD va = (UQUAD)(uintptr_t)block;
    KPROC *kproc;
    int i;

    for (kproc = kproc_list; kproc; kproc = kproc->next)
        for (i = 0; i < KPROC_BORROWS; i++)
            if (kproc->borrowed[i].bytes && kproc->borrowed[i].va == va) {
                if (kproc->aspace)
                    x86_64_aspace_unborrow(kproc->aspace, va, kproc->borrowed[i].bytes);
                kproc->borrowed[i].bytes = 0;
                return;
            }
}

UQUAD kproc_take_kernel_stack(PD *pd, UQUAD *top)
{
    KPROC *kproc = kproc_find(pd);
    UQUAD phys;

    if (!kproc || !kproc->kstack_phys)
        return 0;
    phys = kproc->kstack_phys;
    *top = kproc->kstack_top;
    kproc->kstack_phys = 0;         /* the launcher frees it, after the exit */
    return phys;
}

UQUAD kproc_user_stack(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    return (kproc && kproc->aspace) ? kproc->stack_top : 0;
}

UQUAD kproc_user_entry(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    return (kproc && kproc->aspace) ? kproc->entry : 0;
}

X86_64_ASPACE *kproc_user_aspace(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    return kproc ? kproc->aspace : NULL;
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
    /* the filesystem writes the DTA through this pointer later */
    return kproc_validate_user_write(address, sizeof(DTAINFO));
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

static BOOL validate_user(UQUAD address, ULONG size, BOOL write)
{
    KPROC *kproc = kproc_find(run);

    /* The x32 ABI only carries 32-bit addresses.  Test the subtraction,
     * rather than address + size, so an attacker cannot wrap the range. */
    if (!kproc || !size || address > 0xffffffffULL)
        return FALSE;

    /* A ring-3 process: valid means mapped, user-accessible (and writable
     * for a write) in THIS process's own page tables -- not merely a number
     * that falls in some range. */
    if (kproc->aspace)
        return x86_64_aspace_user_range_ok(kproc->aspace, address, size, write);

    /* No address space (a kernel-code process, which runs in ring 0 on the
     * kernel's own tables): the ranges recorded when it was created, wholly
     * inside its environment block or wholly inside its basepage/TPA. */
    return range_within(address, size, kproc->user_start, kproc->user_end) ||
           range_within(address, size, kproc->env_start, kproc->env_end);
}

BOOL kproc_validate_user_range(UQUAD address, ULONG size)
{
    return validate_user(address, size, FALSE);
}

BOOL kproc_validate_user_write(UQUAD address, ULONG size)
{
    return validate_user(address, size, TRUE);
}

BOOL kproc_copy_from_user(void *dst, UQUAD address, ULONG size)
{
    KPROC *kproc = kproc_find(run);

    if (!validate_user(address, size, FALSE))
        return FALSE;
    if (kproc->aspace)
        return x86_64_aspace_copy_from_user(kproc->aspace, dst, address, size);
    memcpy(dst, (const void *)(uintptr_t)address, size);
    return TRUE;
}

BOOL kproc_copy_to_user(UQUAD address, const void *src, ULONG size)
{
    KPROC *kproc = kproc_find(run);

    if (!validate_user(address, size, TRUE))
        return FALSE;
    if (kproc->aspace)
        return x86_64_aspace_copy_to_user(kproc->aspace, address, src, size);
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
