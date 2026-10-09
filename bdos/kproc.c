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
#include "gemerror.h"
#include "kproc.h"
#include "mem.h"
#include "bdosstub.h"
#include "biosext.h"
#ifdef __x86_64__
#include "kheap.h"
#endif
#if CONF_WITH_USER_ASPACE
#include "procmem.h"
#include "x32image.h"

#define KPROC_BORROWS 16       /* two per child a ring-3 process holds */
#define KPROC_DTAS    8        /* directory searches it can have going at once */
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
    BOOL started;               /* proc_go() has launched it */
    PD *parent;                 /* who launched it: the trusted copy of p_parent */
    SBYTE uft[NUMSTD];          /* its standard-handle map and current directories: */
    UBYTE curdir[NUMCURDIR];    /* the authoritative ones, see PD_UFT() */
#if CONF_WITH_USER_ASPACE
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
    PD *creator;                /* who made the basepage (Pexec() modes 3, 5, 7) */
    const X32_IMAGE *image;     /* built-in program to map private, or NULL */
    UQUAD entry;                /* its entry point once loaded, else 0 */
    UQUAD stack_top;            /* its private stack's top once loaded, else 0 */
    ULONG ancestors;            /* basepage copies on its read-only ancestors page */
    UQUAD kstack_phys;          /* its kernel stack (system calls), 0 if none */
    UQUAD kstack_top;           /* initial stack pointer of that stack */
    struct {                    /* the search state Fsfirst()/Fsnext() keep in */
        UQUAD va;               /* the DTAs of this process, as the kernel */
        ULONG stamp;            /* left them, and when (see below) */
        UBYTE state[offsetof(DTAINFO, dt_fattr)];
    } dtas[KPROC_DTAS];
    ULONG dta_clock;            /* counts the searches, to find the oldest */
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
    bzero(kproc, sizeof *kproc);
    kproc->pd = pd;
    kproc->dta = (DTAINFO *)pd->p_cmdlin;
    /* Take over the tables the basepage may already hold; from now on the
     * basepage's own copies are unused, and cleared so no stale value is
     * mistaken for state. */
    memcpy(kproc->uft, pd->p_uft, sizeof kproc->uft);
    memcpy(kproc->curdir, pd->p_curdir, sizeof kproc->curdir);
    bzero(pd->p_uft, sizeof pd->p_uft);
    bzero(pd->p_curdir, sizeof pd->p_curdir);
#if CONF_WITH_USER_ASPACE
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
#if CONF_WITH_USER_ASPACE
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

/* The tables of a process with a record are in it; the few without one (the
 * boot-time basepages, which only ring 0 ever touches) keep them in the PD. */
SBYTE *kproc_uft(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    return kproc ? kproc->uft : pd->p_uft;
}

UBYTE *kproc_curdir(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    return kproc ? kproc->curdir : pd->p_curdir;
}

/* TRUE for a record whose process was never launched: it still holds the
 * references init_pd_files() took, which nothing else will release. */
BOOL kproc_unlaunched(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    return kproc && !kproc->started;
}

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
#if CONF_WITH_USER_ASPACE
        KINFO(("Missing parent for process record %p\n", pd));
        halt();
#else
        /* no memory protection to defeat: p_parent is as good as it ever was */
        return (PD *)USERPTR_TO_PTR(pd->p_parent);
#endif
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

void kproc_set_dta(PD *pd, DTAINFO *dta)
{
    KPROC *kproc = kproc_find(pd);

    if (!kproc) {
#if CONF_WITH_USER_ASPACE
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

#if CONF_WITH_USER_ASPACE
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

#if CONF_WITH_USER_ASPACE
/*
 * The ancestors page (include/procmem.h): `parent`'s basepage, then the copies
 * its own page holds, each scrubbed of what must not leak or be believed --
 * the file and directory tables and DTA pointer (kernel-owned state), the saved
 * registers -- with p_env redirected to the process's own environment and
 * p_parent chained to the next copy.  FALSE only if memory ran out.
 */
static BOOL build_ancestors(KPROC *kproc, X86_64_ASPACE *as, PD *parent)
{
    static UBYTE page[X86_64_USER_ANCESTORS * sizeof(PD)];
    PD *copy = (PD *)page;
    KPROC *pk = kproc_find(parent);
    ULONG n = 1, i;

    memcpy(&copy[0], parent, sizeof(PD));
    if (pk && pk->aspace && pk->ancestors) {
        ULONG take = pk->ancestors;

        if (take > X86_64_USER_ANCESTORS - 1)
            take = X86_64_USER_ANCESTORS - 1;
        if (!x86_64_aspace_copy_from_user(pk->aspace, &copy[1],
                                          X86_64_USER_ANCESTORS_VA,
                                          take * sizeof(PD)))
            return FALSE;
        n += take;
    }
    for (i = 0; i < n; i++) {
        bzero(copy[i].p_uft, sizeof copy[i].p_uft);
        bzero(copy[i].p_curdir, sizeof copy[i].p_curdir);
        copy[i].p_xdta = 0;
        bzero(copy[i].p_1fill, sizeof copy[i].p_1fill);
        bzero(copy[i].p_2fill, sizeof copy[i].p_2fill);
        bzero(copy[i].p_3fill, sizeof copy[i].p_3fill);
        bzero(copy[i].p_dreg, sizeof copy[i].p_dreg);
        bzero(copy[i].p_areg, sizeof copy[i].p_areg);
        copy[i].p_env = PTR_TO_USERPTR(kproc->env_start);
        copy[i].p_parent = (i + 1 < n) ?
            (ULONG)(X86_64_USER_ANCESTORS_VA + (i + 1) * sizeof(PD)) : 0;
    }
    if (!x86_64_aspace_load_private(as, X86_64_USER_ANCESTORS_VA,
                                    4096, ASPACE_PROT_USER,
                                    page, n * sizeof(PD)))
        return FALSE;
    kproc->ancestors = n;
    return TRUE;
}

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
    if (!build_ancestors(kproc, as, parent)) {
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

/*
 * The private part of a DTA (the pattern, drive, directory position) lives in
 * memory the process can write, and Fsnext() acts on it: a drive that is not
 * mounted, a cluster that is not in the file system.  So the kernel keeps its
 * own copy of what it last left in each DTA of the process, and puts that back
 * before every Fsnext().  A DTA the process has not searched with (or one it
 * has not searched with for a while: the table is small) has none, and the
 * search is over.
 */
BOOL kproc_dta_restore(PD *pd, DTAINFO *dta)
{
    KPROC *kproc = kproc_find(pd);
    UQUAD va = (UQUAD)(uintptr_t)dta;
    int i;

    if (!kproc || !kproc->aspace)
        return TRUE;                /* a kernel process's DTA is the kernel's */
    for (i = 0; i < KPROC_DTAS; i++)
        if (kproc->dtas[i].va == va) {
            memcpy(dta, kproc->dtas[i].state, sizeof kproc->dtas[i].state);
            return TRUE;
        }
    return FALSE;
}

/* a new Fsfirst() starts over: whatever search this DTA held is gone, found
 * anything or not */
void kproc_dta_forget(PD *pd, const DTAINFO *dta)
{
    KPROC *kproc = kproc_find(pd);
    UQUAD va = (UQUAD)(uintptr_t)dta;
    int i;

    if (!kproc || !kproc->aspace)
        return;
    for (i = 0; i < KPROC_DTAS; i++)
        if (kproc->dtas[i].va == va)
            kproc->dtas[i].va = 0;
}

void kproc_dta_save(PD *pd, const DTAINFO *dta)
{
    KPROC *kproc = kproc_find(pd);
    UQUAD va = (UQUAD)(uintptr_t)dta;
    int i;

    if (!kproc || !kproc->aspace)
        return;
    for (i = 0; i < KPROC_DTAS; i++)
        if (kproc->dtas[i].va == va)
            break;
    if (i == KPROC_DTAS) {
        int j;

        /* a free slot, else the one whose search is the oldest */
        for (i = 0, j = 0; j < KPROC_DTAS; j++) {
            if (!kproc->dtas[j].va) {
                i = j;
                break;
            }
            if ((LONG)(kproc->dtas[j].stamp - kproc->dtas[i].stamp) < 0)
                i = j;
        }
        kproc->dtas[i].va = va;
    }
    kproc->dtas[i].stamp = ++kproc->dta_clock;
    memcpy(kproc->dtas[i].state, dta, sizeof kproc->dtas[i].state);
}

UQUAD kproc_ancestors_va(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    return kproc && kproc->aspace && kproc->ancestors ? X86_64_USER_ANCESTORS_VA : 0;
}

BOOL kproc_set_image(PD *pd, const X32_IMAGE *image)
{
    KPROC *kproc = kproc_find(pd);

    if (!kproc || kproc->aspace || !x86_64_x32image_check(image, NULL))
        return FALSE;
    kproc->image = image;
    return TRUE;
}

void kproc_set_creator(PD *pd, PD *creator)
{
    KPROC *kproc = kproc_find(pd);

    if (kproc)
        kproc->creator = creator;
}

/*
 * Pexec(PE_BASEPAGE*, PE_LOAD) from ring 3: the basepage and its blocks,
 * borrowed supervisor-only while they were built, are handed to the caller
 * (which owns them) as ordinary user memory.  They go back, as every borrow
 * does, when they are freed.
 */
void kproc_hand_over(PD *caller, PD *child)
{
    KPROC *launcher = kproc_find(caller);
    KPROC *kproc = kproc_find(child);

    if (!launcher || !launcher->aspace || !kproc)
        return;
    /* the caller may grow the TPA (p_hitpa) up to the end of its block */
    kproc->user_end = kproc->user_start + x86_64_procmem_size(kproc->user_start);
    x86_64_aspace_regrant(launcher->aspace, (UQUAD)(uintptr_t)kproc->env_start,
                          kproc->env_end - kproc->env_start,
                          ASPACE_PROT_WRITE | ASPACE_PROT_USER);
    x86_64_aspace_regrant(launcher->aspace, (UQUAD)(uintptr_t)kproc->user_start,
                          x86_64_procmem_size(kproc->user_start),
                          ASPACE_PROT_WRITE | ASPACE_PROT_EXEC | ASPACE_PROT_USER);
}

/*
 * Pexec(PE_GO, PE_GOTHENFREE) from ring 3.  The basepage the caller passes is
 * only a request: it must be one the kernel made for this caller and has not
 * launched, and its public fields, which the caller may have rewritten, must
 * still describe memory that basepage owns.  Returns E_OK, EIMBA for a
 * basepage that is not (or no longer) the caller's, EPLFMT for fields out of
 * bounds.  See doc/x86_64-address-space.txt, "Basepages in ring 3".
 */
LONG kproc_check_launch(PD *pd, PD *caller)
{
    KPROC *kproc = kproc_find(pd);
    UQUAD lowtpa, hitpa, tbase, first;
    UBYTE *alloc_end;

    if (!kproc || kproc->started || kproc->creator != caller || kproc->aspace)
        return EIMBA;
    /* the blocks must still be the ones recorded: freed and reused ones are not */
    if (x86_64_procmem_gen(kproc->user_start) != kproc->tpa_gen ||
        x86_64_procmem_gen(kproc->env_start) != kproc->env_gen)
        return EIMBA;
    /* each field is read once into a local: that is the value validated */
    lowtpa = pd->p_lowtpa;
    hitpa = ((pd->p_hitpa + 8UL) & ~15UL) - 8;      /* RSP + 8 divisible by 16 */
    tbase = pd->p_tbase;
    first = (UQUAD)(uintptr_t)kproc->user_start + sizeof(PD);
    /* the block's own end, not user_end: that is the bound of the launch
     * last validated, and a launch that failed for lack of memory may be
     * retried with different fields */
    alloc_end = (UBYTE *)kproc->user_start + x86_64_procmem_size(kproc->user_start);
    if (lowtpa != (UQUAD)(uintptr_t)kproc->user_start ||
        hitpa < first || hitpa > (UQUAD)(uintptr_t)alloc_end ||
        tbase < first || tbase >= hitpa ||
        pd->p_env != PTR_TO_USERPTR(kproc->env_start))
        return EPLFMT;
    pd->p_hitpa = (ULONG)hitpa;
    kproc->user_end = (UBYTE *)(uintptr_t)hitpa;    /* the trusted bound */
    return E_OK;
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

/* How many times `launcher` holds the block borrowed: pins that are its to
 * give back, as opposed to somebody else's loan or a live process's own
 * mapping. */
ULONG kproc_borrow_count(PD *launcher, void *block)
{
    UQUAD va = (UQUAD)(uintptr_t)block;
    KPROC *kproc;
    ULONG n = 0;
    int i;

    kproc = kproc_find(launcher);
    if (kproc && kproc->aspace)
        for (i = 0; i < KPROC_BORROWS; i++)
            if (kproc->borrowed[i].bytes && kproc->borrowed[i].va == va)
                n++;
    return n;
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
