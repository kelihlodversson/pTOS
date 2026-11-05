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
#if CONF_WITH_NATIVE_SCHED
#include "ksched.h"
#endif
#if CONF_WITH_CUIS
#include "../aes/cuis.h"
#endif
#include "mem.h"
#include "bdosstub.h"
#include "biosext.h"
#include "tosvars.h"
#if CONF_WITH_X86_64_MEMTEST && CONF_WITH_USER_CALLBACKS
#include "../bios/machine/pc-x86_64/pc_x86_64_memtest.h"
#endif
#if CONF_WITH_X32_VDI
#include "vdi_service.h"
#include "x32vdi_image.h"
#endif
#if CONF_WITH_KHEAP
#include "kheap.h"
#include "kprint.h"
#include "proc.h"
#endif
#if CONF_WITH_USER_ASPACE
#include "procmem.h"
#include "kaspace.h"
#include "kthread.h"
#include "progimage.h"
#if CONF_WITH_ELF_SHARED
#include "../lib/ldso/startup.h"
#endif

#define KPROC_BORROWS 16       /* two per child a ring-3 process holds */
#define KPROC_DTAS    8        /* directory searches it can have going at once */

enum { UH_MALLOC = 0,           /* a block Malloc() gave out */
       UH_LOADED = 1,           /* a program Pexec(PE_LOAD) loaded for a child, until PE_GO */
       UH_IMAGE = 2,            /* the image of this very process, moved here by PE_GO */
       UH_LENT = 3 };           /* the image of a PE_GO child, lent to it until it ends */
enum { PROCESS_LIVE, PROCESS_DISPOSAL_PENDING, PROCESS_EXIT_HANDOFF,
       PROCESS_DISPOSING, PROCESS_RETIRED };
#endif

/* The option gates only the record itself: the #else stubs stay in every
 * link so the public p_xdta field keeps working verbatim. */

#if CONF_WITH_KPROC

/* PD is the public GEMDOS basepage and is writable by its process. Keep
 * native pointers and the user-range snapshot in this kernel-only record. */
struct kproc {
    ULONG references;            /* live lookup plus explicit retained references */
    BOOL completed;
    UWORD exit_status;
#if CONF_WITH_CUIS
    char executable_name[33]; /* Bounded basename; GUI normalization stays in userspace. */
#endif
#if CONF_WITH_USER_CALLBACKS
    USER_CALLBACK_STATE *callbacks;
    UQUAD resume_rsp;
#endif
    PD *pd;
    DTAINFO *dta;
    BOOL started;               /* proc_go() has launched it */
    PD *parent;                 /* who launched it: the trusted copy of p_parent */
#if CONF_WITH_USER_ASPACE
    KPROC *parent_identity;     /* owning identity, never live parent resources */
    BOOL parent_untracked;      /* legacy kernel parent without a KPROC */
    UQUAD parent_gen;            /* generation if that parent has an allocation */
#endif
    WORD pid;                   /* its process id, 1..KPROC_PID_MAX, never reused while it lives */
    SBYTE prio;                 /* Psetpriority(): kept, and with no effect (scheduling is cooperative) */
    ULONG start_tick;           /* hz_200 when the record was made, for Prusage() */
    ULONG child_ticks;          /* the ticks the processes it launched have used up */
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
    ASPACE *aspace;      /* borrowed from active_space */
    KASPACE *active_space; /* owning reference, NULL until prepared */
    KTHREAD *threads;     /* non-owning execution membership */
    UBYTE disposal_state; /* identity survives the final live-resource cleanup */
    BOOL clone_identity_only; /* no executable context/startup contract yet */
    BOOL completion_pending;
    UWORD pending_status;
    PD *creator;                /* who made the basepage (Pexec() modes 3, 5, 7) */
#if CONF_WITH_NATIVE_SCHED
    PD *async_parent;           /* stable controller for exit, not logical PPID */
#endif
    PROG_LAYOUT layout;          /* the program to map private (built in, or loaded) */
    BOOL has_image;             /* ... if there is one */
    BOOL legacy;                /* a classic PRG: its stack is in its TPA (KPROC_LEGACY_PRG) */
#if CONF_WITH_ELF_SHARED
    PROG_LAYOUT interpreter_layout;
    BOOL standard_entry;
    UQUAD interp_entry, initial_rsp, interp_bytes;
#endif
    ASPACE *pending_as;  /* borrowed from pending_space */
    KASPACE *pending_space; /* owning reference while the image is loaded */
                                /* at load time, with its image in it; the launch adopts it */
    BOOL lost;                  /* ... and the launch failed after adopting it: nothing to launch */
    PD *load_parent;            /* PE_LOAD: the process whose heap holds the image */
    VADDR load_va;              /* until the launch moves it here: where, */
    ULONG load_pages;           /* and how many pages (startup area included) */
    BOOL give_back;             /* PE_GO: the image goes back to its loader when this ends */
    PD *lender;                 /* ... the process whose heap it came from, once moved */
    struct uheap_block {        /* loader staging and legacy returned-image blocks */
        VADDR va;               /* sorted by address */
        ULONG pages;
        UBYTE kind;             /* UH_MALLOC here is only legacy PE_GO give-back */
        PD *pd;                 /* UH_LOADED: the process it is loaded for */
    } *heap;
    ULONG nheap, heap_capacity;
    VADDR stack_size;           /* Psetlimit(): the stack's address space; 0: the default */
    VADDR stack_low;            /* the lowest stack page backed so far; 0: none */
    VADDR lim_core, lim_data;   /* Psetlimit(): total / allocatable memory in bytes; 0: no limit */
    VADDR entry;                /* its entry point once loaded, else 0 */
    VADDR stack_top;            /* its private stack's top once loaded, else 0 */
    ULONG ancestors;            /* basepage copies on its read-only ancestors page */
    PADDR kstack_phys;          /* its kernel stack (system calls), 0 if none */
    uintptr_t kstack_top;       /* initial stack pointer of that stack */
    struct {                    /* the search state Fsfirst()/Fsnext() keep in */
        VADDR va;               /* the DTAs of this process, as the kernel */
        ULONG stamp;            /* left them, and when (see below) */
        UBYTE state[offsetof(DTAINFO, dt_fattr)];
    } dtas[KPROC_DTAS];
    ULONG dta_clock;            /* counts the searches, to find the oldest */
    struct {                    /* blocks of a child being launched from this */
        VADDR va, bytes;        /* process, mapped supervisor-only into its */
    } borrowed[KPROC_BORROWS];  /* address space until they are freed */
#endif
    KPROC *next;
};

static KPROC *kproc_list;
static KPROC *retired_processes; /* identities retained after removal from lookup */

/* KPROC records are kernel-only objects.  On x86-64 they live in the
 * growable kernel heap, so the number of processes is bounded by memory
 * and not by the fixed xmgetblk() pool; they are never GEMDOS memory. */
#if CONF_WITH_KHEAP
#define KPROC_ALLOC()   ((KPROC *)kalloc(sizeof(KPROC)))
#define KPROC_FREE(k)   kfree(k)
#else
#define KPROC_ALLOC()   MGET(KPROC)
#define KPROC_FREE(k)   xmfreblk(k)
#endif

#define KPROC_PID_MAX   32767
static WORD last_pid;
#if CONF_WITH_X86_64_MEMTEST
static UWORD test_pid_candidates;
void kproc_test_pid_search(WORD previous, UWORD candidates)
{
    last_pid = previous;
    test_pid_candidates = candidates;
}
#endif

/* a process id no record has */
static WORD new_pid(void)
{
    KPROC *k;
    WORD pid;
    UWORD attempts, limit = KPROC_PID_MAX;

#if CONF_WITH_X86_64_MEMTEST
    if (test_pid_candidates) limit = test_pid_candidates;
#endif
    for (attempts = 0; attempts < limit; attempts++) {
        last_pid = (last_pid >= KPROC_PID_MAX) ? 1 : last_pid + 1;
        pid = last_pid;
        for (k = kproc_list; k; k = k->next)
            if (k->pid == pid)
                break;
        if (!k)
            for (k = retired_processes; k; k = k->next)
                if (k->pid == pid)
                    break;
        if (!k
#if CONF_WITH_NATIVE_SCHED
            && !ksched_pid_reserved(pid)
#endif
        ) return pid;
    }
    return 0;                    /* all candidates are reserved */
}

static KPROC *kproc_find(PD *pd)
{
    KPROC *kproc;

    for (kproc = kproc_list; kproc; kproc = kproc->next)
        if (kproc->pd == pd)
            return kproc;
    return NULL;
}

ULONG kproc_count(void)
{
    ULONG n = 0;
    KPROC *kproc;

    for (kproc = kproc_list; kproc; kproc = kproc->next)
        n++;
    return n;
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
    kproc->references = 1;       /* the live lookup owns this reference */
    kproc->pd = pd;
    kproc->pid = new_pid();
    if (!kproc->pid) {
        KPROC_FREE(kproc);
        return FALSE;
    }
    kproc->start_tick = (ULONG)hz_200;
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
                     procmem_size(kproc->env_start);
    kproc->env_gen = procmem_gen(kproc->env_start);
    kproc->tpa_gen = procmem_gen(pd);
    kproc->user_start = (UBYTE *)pd;
    kproc->user_end = USERPTR_TO_PTR(pd->p_hitpa);
    /* The basepage is writable by whoever holds it until launch, so the
     * snapshot is the only trusted bound: the TPA must lie within the one
     * allocation the basepage sits at the start of, whatever p_hitpa says. */
    {
        UBYTE *alloc_end = kproc->user_start +
                           procmem_size(kproc->user_start);

        if (kproc->user_end > alloc_end || kproc->user_end < kproc->user_start)
            kproc->user_end = alloc_end;
    }
#endif
    kproc->next = kproc_list;
    kproc_list = kproc;
    return TRUE;
}

#if CONF_WITH_USER_ASPACE
static void give_image_back(KPROC *kproc);
static void release_loaded(KPROC *kproc);
#if CONF_WITH_X86_64_MEMTEST
ULONG kproc_test_child_ticks(const KPROC *process) { return process->child_ticks; }
void kproc_test_age_unstarted(KPROC *process, ULONG ticks)
{
    if (process->started) panic("test cannot age an executing process");
    process->start_tick = (ULONG)hz_200 - ticks;
}
KASPACE *kproc_test_retain_pending(PD *pd)
{
    KPROC *process = kproc_find(pd);
    return process ? kaspace_share(process->pending_space) : NULL;
}
KASPACE *kproc_test_retain_active(PD *pd)
{
    KPROC *process = kproc_find(pd);
    return process ? kaspace_share(process->active_space) : NULL;
}
BOOL kproc_test_share_active(PD *pd, KASPACE *space)
{
    KPROC *process = kproc_find(pd);
    KASPACE *shared;
    if (!process || process->active_space || process->pending_space)
        return FALSE;
    shared = kaspace_share(space);
    if (!shared)
        return FALSE;
    process->active_space = shared;
    process->aspace = kaspace_backend(shared);
    return TRUE;
}
#endif
#endif

KPROC *kproc_retain(PD *pd)
{
    KPROC *process = kproc_find(pd);
    if (!process || process->references == ~(ULONG)0)
        return NULL;
    process->references++;
    return process;
}

void kproc_release(KPROC *process)
{
    KPROC **link;
    if (!process)
        return;
    if (!process->references)
        panic("kproc_release: reference underflow");
    if (process->references > 1) {
        process->references--;
        return;
    }
    for (link = &kproc_list; *link; link = &(*link)->next)
        if (*link == process)
            panic("kproc_release: final live reference");
    if (process->pd)
        panic("kproc_release: final live reference");
    /* Only the live lookup owns its reference; final release is retired. */
    for (link = &retired_processes; *link; link = &(*link)->next)
        if (*link == process) {
            *link = process->next;
            KPROC_FREE(process);
            return;
        }
    panic("kproc_release: unregistered final reference");
}

WORD kproc_ref_pid(const KPROC *process) { return process ? process->pid : 0; }
BOOL kproc_ref_has_basepage(const KPROC *process) { return process && process->pd != NULL; }
BOOL kproc_ref_status(const KPROC *process, UWORD *status)
{
    if (!process || !process->completed) return FALSE;
    if (status) *status = process->exit_status;
    return TRUE;
}
void kproc_complete(PD *pd, UWORD status)
{
    KPROC *process = kproc_find(pd);
    if (process && !process->completed) {
        process->exit_status = status;
        process->completed = TRUE;
    }
}

#if CONF_WITH_USER_ASPACE
KPROC *kproc_retain_live(KPROC *process)
{
    if (!process || !process->pd || process->completed ||
        process->disposal_state != PROCESS_LIVE || process->references == ~(ULONG)0)
        return NULL;
    process->references++;
    return process;
}

PD *kproc_live_pd(const KPROC *process)
{
    return process && !process->completed && process->disposal_state == PROCESS_LIVE ? process->pd : NULL;
}

BOOL kproc_clone_inherit_identity(KPROC *target, KPROC *source)
{
    KPROC *parent;
    if (!kproc_clone_target_ready(target, NULL) || target->parent_identity ||
        !(parent = kproc_retain_live(source)))
        return FALSE;
    target->parent_identity = parent;
    target->parent = parent->pd; /* borrowed only while identity remains live */
    target->prio = source->prio;
    target->lim_core = source->lim_core;
    target->lim_data = source->lim_data;
    return TRUE;
}

KASPACE *kproc_active_space(const KPROC *process)
{
    return process && process->pd && !process->completed &&
        process->disposal_state == PROCESS_LIVE ? process->active_space : NULL;
}

BOOL kproc_thread_start_allowed(const KPROC *process)
{
    return process && process->pd && !process->completed &&
        !process->clone_identity_only && process->disposal_state == PROCESS_LIVE;
}

BOOL kproc_thread_attach_space(KPROC *p, KASPACE *construction_space,
    KTHREAD *thread, KPROC **process, KASPACE **space, KTHREAD **next)
{
    KASPACE *as;
    if (!thread || !construction_space || !p ||
        (p->active_space && p->active_space != construction_space) ||
        !kproc_retain_live(p))
        return FALSE;
    as = kaspace_share(construction_space);
    if (!as) {
        kproc_release(p);
        return FALSE;
    }
    *process = p;
    *space = as;
    *next = p->threads;
    p->threads = thread;
    return TRUE;
}

BOOL kproc_thread_attach(PD *pd, KTHREAD *thread, KPROC **process,
                         KASPACE **space, KTHREAD **next)
{
    KPROC *p = kproc_find(pd);
    return kproc_thread_attach_space(p, p ? p->active_space : NULL,
                                    thread, process, space, next);
}

/* Only a caller-prepared, empty resource container can import shared VM.
 * No image, stack or loader loan is transferred by this ownership operation. */
BOOL kproc_clone_target_ready(const KPROC *p, KTHREAD *thread)
{
    ULONG i;
    if (!p || !p->pd || p->started || p->completed ||
        p->disposal_state != PROCESS_LIVE || p->clone_identity_only ||
        p->active_space || p->pending_space || p->has_image || p->lost || p->kstack_phys ||
        p->nheap || p->load_parent || p->lender ||
        (thread ? p->threads != thread || *kthread_next_link(thread) : p->threads != NULL))
        return FALSE;
    for (i = 0; i < KPROC_BORROWS; i++)
        if (p->borrowed[i].bytes) return FALSE;
    return TRUE;
}

BOOL kproc_clone_adopt_space(KPROC *p, KTHREAD *thread, KASPACE *space)
{
    if (!space || !thread || !kproc_clone_target_ready(p, thread) ||
        kthread_process(thread) != p || kthread_aspace(thread) != space)
        return FALSE;
    p->active_space = space; /* transfer the transaction's owning reference */
    p->aspace = kaspace_backend(space);
    p->clone_identity_only = TRUE; /* existing loaders/scheduler cannot start it */
    return TRUE;
}

BOOL kproc_begin_disposal(KPROC *process, PD **pd)
{
    if (!process || process->disposal_state == PROCESS_EXIT_HANDOFF ||
        process->disposal_state >= PROCESS_DISPOSING)
        return FALSE;
    if (process->threads) {
        process->disposal_state = PROCESS_DISPOSAL_PENDING;
        return FALSE;
    }
    process->disposal_state = PROCESS_DISPOSING;
    *pd = process->pd;
    return *pd != NULL;
}

BOOL kproc_thread_is_sole(KPROC *process, KTHREAD *thread)
{
    return process && thread && process->disposal_state == PROCESS_LIVE &&
        process->pd && !process->completed && process->threads == thread &&
        !*kthread_next_link(thread) && kthread_process(thread) == process;
}

static BOOL sole_execution_thread(KPROC *process, KTHREAD *thread)
{
    return thread && process->threads == thread &&
        !*kthread_next_link(thread) && kthread_is_executing(thread);
}

BOOL kproc_prepare_thread_exit_handoff(KPROC *process, KTHREAD *thread)
{
    if (!process || process->disposal_state != PROCESS_LIVE ||
        process->completed || !process->pd ||
        (thread ? !sole_execution_thread(process, thread) : process->threads != NULL))
        return FALSE;
    process->disposal_state = PROCESS_EXIT_HANDOFF;
    return TRUE;
}

BOOL kproc_prepare_exit_handoff(KPROC *process)
{
    return kproc_prepare_thread_exit_handoff(process, NULL);
}

BOOL kproc_finish_thread_handoff(KPROC *process, KTHREAD *thread)
{
    if (!process || process->disposal_state != PROCESS_EXIT_HANDOFF ||
        !sole_execution_thread(process, thread))
        return FALSE;
    process->threads = NULL;
    return TRUE;
}

BOOL kproc_commit_exit_handoff(KPROC *process, PD **pd)
{
    if (!process || process->disposal_state != PROCESS_EXIT_HANDOFF ||
        process->threads || !process->pd)
        return FALSE;
    process->disposal_state = PROCESS_DISPOSING;
    *pd = process->pd;
    return TRUE;
}

BOOL kproc_begin_exit(PD *pd)
{
    PD *claimed;
    return kproc_begin_disposal(kproc_find(pd), &claimed) && claimed == pd;
}

BOOL kproc_request_completion(KPROC *process, KTHREAD *thread, UWORD status)
{
    if (!process || !process->started || !thread || process->threads != thread ||
        *kthread_next_link(thread) || !kthread_is_stopped(thread) ||
        process->completed || process->disposal_state != PROCESS_LIVE)
        return FALSE;
    process->completion_pending = TRUE;
    process->pending_status = status;
    process->disposal_state = PROCESS_DISPOSAL_PENDING;
    return TRUE;
}

BOOL kproc_pending_completion(const KPROC *process, UWORD *status)
{
    if (!process || !process->completion_pending)
        return FALSE;
    if (status)
        *status = process->pending_status;
    return TRUE;
}

void kproc_require_exit_thread(PD *pd, KTHREAD *thread)
{
    KPROC *process = kproc_find(pd);
    if (process && process->threads && !sole_execution_thread(process, thread))
        panic("xterm: unsupported attached threads");
}

void kproc_require_no_threads(PD *pd)
{
    KPROC *process = kproc_find(pd);
    if (process && process->threads)
        panic("xterm: attached non-runnable threads");
}

BOOL kproc_thread_detach(KPROC *process, KTHREAD *thread, KTHREAD *next)
{
    KTHREAD **link;

    for (link = &process->threads; *link; link = kthread_next_link(*link))
        if (*link == thread) {
            *link = next;
            return !process->threads && process->disposal_state == PROCESS_DISPOSAL_PENDING;
        }
    panic("kproc_thread_detach: not a member");
}
#endif

void kproc_destroy(PD *pd)
{
    KPROC **link;

    for (link = &kproc_list; *link; link = &(*link)->next)
        if ((*link)->pd == pd) {
            KPROC *kproc = *link;
#if CONF_WITH_USER_ASPACE
            KPROC *launcher = kproc->parent_identity;
            if (launcher && !launcher->pd) launcher = NULL;
#else
            KPROC *launcher = kproc->parent ? kproc_find(kproc->parent) : NULL;
#endif

#if CONF_WITH_USER_ASPACE
            if (kproc->threads)
                panic("kproc_destroy: attached non-runnable threads");
#endif

#if CONF_WITH_CUIS
            cuis_process_exit(pd);
#endif
#if CONF_WITH_X32_VDI
            vdi_service_process_exit(pd);
#endif
#if CONF_WITH_USER_CALLBACKS
            user_callback_process_exit(pd);
#endif
            *link = kproc->next;
            /* Keep the PID reserved even if cleanup re-enters allocation. */
            kproc->next = retired_processes;
            retired_processes = kproc;
            /* Identity-only construction/cancellation consumed no child CPU
             * lifetime. Preserve classic accounting outside user-ASPACE. */
            if (launcher
#if CONF_WITH_USER_ASPACE
                && kproc->started
#endif
            )
                launcher->child_ticks += (ULONG)hz_200 - kproc->start_tick;
#if CONF_WITH_USER_ASPACE
            /* a program PE_LOAD put in its caller's heap that was never
             * launched goes back with the basepage */
            if (kproc->load_parent)
                release_loaded(kproc);
            if (kproc->lender)
                give_image_back(kproc);
            kproc_load_leave();             /* (a disk error jumped out of a load) */
            kaspace_put(kproc->pending_space);
            /* Unlinked first, so the record is gone before its address
             * space is torn down: a second kproc_destroy() for the same
             * PD finds nothing and cannot free either twice. */
            kaspace_put(kproc->active_space);
            kstack_free(kproc->kstack_phys);
            kfree(kproc->heap);
#endif
            /* Only immutable identity and completion survive. In particular,
             * no retained reference may dereference the now-releasable PD. */
            {
                ULONG references = kproc->references;
                WORD pid = kproc->pid;
                BOOL completed = kproc->completed;
                UWORD status = kproc->exit_status;
                KPROC *next = kproc->next;
#if CONF_WITH_USER_ASPACE
                KPROC *parent_identity = kproc->parent_identity;
#endif
                bzero(kproc, sizeof *kproc);
                kproc->references = references;
                kproc->pid = pid;
                kproc->completed = completed;
                kproc->exit_status = status;
                kproc->next = next;
#if CONF_WITH_USER_ASPACE
                kproc->disposal_state = PROCESS_RETIRED;
                kproc_release(parent_identity);
#endif
            }
            kproc_release(kproc); /* drop the lookup's ownership */
            return;
        }
}


WORD kproc_pid(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    return kproc ? kproc->pid : 0;
}

#if CONF_WITH_CUIS
void kproc_set_executable_name(PD *pd, const char *path)
{
    KPROC *k = kproc_find(pd);
    const char *base = path, *p;
    UWORD i;
    if (!k) return;
    for (p = path; *p; p++) if (*p == '\\' || *p == '/' || *p == ':') base = p + 1;
    for (i = 0; i < 32 && base[i]; i++) k->executable_name[i] = base[i];
    k->executable_name[i] = 0;
}
const char *kproc_executable_name(PD *pd)
{
    KPROC *k = kproc_find(pd);
    return k ? k->executable_name : "";
}
#endif

WORD kproc_parent_pid(PD *pd)
{
    KPROC *kproc = kproc_find(pd);
#if CONF_WITH_NATIVE_SCHED
    WORD pid;
    if (ksched_parent_pid(pd, &pid)) return pid;
#endif

#if CONF_WITH_USER_ASPACE
    if (kproc && kproc->parent_identity)
        return kproc->parent_identity->pd ? kproc->parent_identity->pid : 0;
    return 0; /* an untracked parent had no PID at association */
#else
    return (kproc && kproc->parent) ? kproc_pid(kproc->parent) : 0;
#endif
}

SBYTE kproc_get_prio(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    return kproc ? kproc->prio : 0;
}

void kproc_set_prio(PD *pd, SBYTE prio)
{
    KPROC *kproc = kproc_find(pd);

    if (kproc)
        kproc->prio = prio;
}

/*
 * Prusage(), after FreeMiNT's (sys/k_resource.c), times in milliseconds: [0] the
 * system time, [1] the user time, [2] and [3] the same of the processes it
 * launched, [4] the memory used, [5..7] 0.  The system does not tell kernel from
 * program time: a process's time is the time since it was made less what its
 * children used (they run while it waits, one at a time), all of it counted as
 * user time; the system times are 0.  The memory is the private pages it has.
 */
void kproc_usage(PD *pd, ULONG r[8])
{
    KPROC *kproc = kproc_find(pd);
    ULONG elapsed, i;

    for (i = 0; i < 8; i++)
        r[i] = 0;
    if (!kproc)
        return;
    elapsed = (ULONG)hz_200 - kproc->start_tick;
    if (elapsed < kproc->child_ticks)
        elapsed = kproc->child_ticks;
    r[1] = (elapsed - kproc->child_ticks) * (1000UL / CLOCKS_PER_SEC);
    r[3] = kproc->child_ticks * (1000UL / CLOCKS_PER_SEC);
#if CONF_WITH_USER_ASPACE
    if (kproc->aspace)
        r[4] = aspace_private_pages(kproc->aspace) * (ULONG)USER_PAGE_SIZE;
#endif
}

/* The tables of a process with a record are in it; the few without one (the
 * boot-time basepages, which only ring 0 ever touches) keep them in the PD. */
SBYTE *kproc_uft(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    return kproc ? kproc->uft : pd->p_uft;
}

#if CONF_WITH_CUIS
void kproc_standard_file_refs(ULONG *references)
{
    KPROC *kproc;
    WORD i;

    bzero(references,OPNFILES*sizeof(*references));
    for (kproc = kproc_list; kproc; kproc = kproc->next)
        for (i = 0; i < NUMSTD; i++) {
            WORD handle = kproc->uft[i];
            if (handle >= NUMSTD && handle < NUMHANDLES)
                references[handle-NUMSTD]++;
        }
}
#endif

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
#if CONF_WITH_USER_ASPACE
    {
        KPROC *old = kproc->parent_identity;
        KPROC *record = parent && parent != pd ? kproc_find(parent) : NULL;
        kproc->parent_identity = record ? kproc_retain(parent) : NULL;
        /* A tracked parent whose retain failed must not become an unsafe
         * borrowed notification identity. */
        kproc->parent_untracked = parent && parent != pd && !record;
        kproc->parent_gen = kproc->parent_untracked ? procmem_gen(parent) : 0;
        kproc_release(old);
    }
#endif
    kproc->parent = parent;
}

#if CONF_WITH_USER_ASPACE
PD *kproc_notification_parent(PD *pd)
{
    KPROC *process = kproc_find(pd);
    if (!process)
        return NULL;
    if (process->parent_identity)
        return process->parent_identity->pd;
    if (!process->parent_untracked || kproc_find(process->parent))
        return NULL;
    if (process->parent_gen && procmem_gen(process->parent) != process->parent_gen)
        return NULL;
    return process->parent;
}
#endif

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
        pd->p_xdta = PTR_TO_USERPTR_UNCHECKED((DTA *)dta);
        return;
#endif
    }
    kproc->dta = dta;
    pd->p_xdta = PTR_TO_USERPTR_UNCHECKED((DTA *)dta);
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
    if (kproc && PTR_TO_USERPTR_UNCHECKED((DTA *)kproc->dta) == pd->p_xdta)
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
static BOOL build_ancestors(KPROC *kproc, ASPACE *as, PD *parent)
{
    static UBYTE page[USER_ANCESTORS * sizeof(PD)];
    PD *copy = (PD *)page;
    KPROC *pk = kproc_find(parent);
    ULONG n = 1, i;

    memcpy(&copy[0], parent, sizeof(PD));
    if (pk && pk->aspace && pk->ancestors) {
        ULONG take = pk->ancestors;

        if (take > USER_ANCESTORS - 1)
            take = USER_ANCESTORS - 1;
        if (!aspace_copy_from_user(pk->aspace, &copy[1],
                                          USER_ANCESTORS_VA,
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
        copy[i].p_env = PTR_TO_USERPTR((char *)kproc->env_start);
        copy[i].p_parent = (i + 1 < n) ?
            PTR_TO_USERPTR_UNCHECKED((PD *)(uintptr_t)(USER_ANCESTORS_VA + (i + 1) * sizeof(PD))) :
            PTR_TO_USERPTR_UNCHECKED((PD *)NULL);
    }
    if (!aspace_load_private(as, USER_ANCESTORS_VA,
                                    4096, ASPACE_PROT_USER,
                                    page, n * sizeof(PD)))
        return FALSE;
    kproc->ancestors = n;
    return TRUE;
}

static BOOL move_loaded_image(KPROC *kproc, ASPACE *to, PD *launcher);
static VADDR stack_bytes(const KPROC *kproc);

/* A launch that cannot be completed: the address space is gone.  One that was
 * made at load time holds the program's only copy, so there is nothing left
 * to launch (`lost`): the basepage can only be freed. */
static BOOL prepare_failed(KPROC *kproc, KASPACE *owner)
{
    if (owner == kproc->pending_space) {
        kproc->pending_as = NULL;
        kproc->pending_space = NULL;
        kproc->has_image = FALSE;
        kproc->lost = TRUE;
    }
    kaspace_put(owner);
    if (kproc->kstack_phys)
        kstack_free(kproc->kstack_phys);
    kproc->kstack_phys = 0;
    kproc->entry = 0;
    return FALSE;
}

#if CONF_WITH_NATIVE_SCHED
void kproc_add_child_usage(PD *pd, ULONG milliseconds)
{
    KPROC *kproc = kproc_find(pd);
    if (kproc) kproc->child_ticks += milliseconds / (1000UL / CLOCKS_PER_SEC);
}

void kproc_commit_async(PD *pd)
{
    KPROC *kproc = kproc_find(pd);
    /* Preparation has validated these allocation identities. Revoke the
     * loader's mappings before it can run alongside the child or rewrite
     * the child's basepage/environment after ownership is transferred. */
    kproc->creator = NULL;
    procmem_set_owner(kproc->env_start, pd);
    procmem_set_owner(kproc->user_start, pd);
    kproc_unborrow(kproc->env_start);
    kproc_unborrow(kproc->user_start);
}

BOOL kproc_prepare_async_thread(PD *pd, PD *parent, PD *controller, KTHREAD **thread)
{
    KPROC *kproc = kproc_find(pd);
    if (thread) *thread = NULL;
    if (!kproc || kproc->started || kproc->aspace) return FALSE;
    kproc->async_parent = controller;
    return kproc_prepare_user_thread(pd, parent, thread);
}

BOOL kproc_prepare_async(PD *pd, PD *parent, PD *controller)
{
    return kproc_prepare_async_thread(pd, parent, controller, NULL);
}
#endif

BOOL kproc_prepare_user_thread(PD *pd, PD *parent, KTHREAD **thread_result)
{
    KPROC *kproc = kproc_find(pd);
    KTHREAD *thread = NULL;
    ASPACE *as;
    KASPACE *owner;
    VADDR env, tpa, hitpa;
    ULONG envbytes;

    if (thread_result) *thread_result = NULL;
    if (!kproc || kproc->lost)
        return FALSE;
    if (kproc->aspace) {
        /* Already prepared -- but a process that has been launched has handed
         * its kernel stack to gouser(), and cannot be launched a second time
         * (Pexec(PE_GO) of its own basepage, from ring 3): refuse, where the
         * launch can still fail cleanly. */
        if (!kproc->kstack_phys) return FALSE;
        if (thread_result) {
            *thread_result = kthread_create(pd);
            return *thread_result != NULL;
        }
        return TRUE;
    }
    env = (VADDR)(uintptr_t)kproc->env_start;
    tpa = (VADDR)(uintptr_t)kproc->user_start;
    hitpa = (VADDR)(uintptr_t)kproc->user_end;
    envbytes = (ULONG)(kproc->env_end - kproc->env_start);
    /* Map only what was recorded when the basepage was made.  The public
     * fields may have been rewritten since (PE_BASEPAGE/PE_LOAD hand the
     * caller a writable basepage), so they must still agree with it:
     * otherwise a launch could map a neighbouring allocation. */
    if (!envbytes || hitpa <= tpa ||
        !kproc->env_gen || !kproc->tpa_gen ||
        procmem_gen(kproc->env_start) != kproc->env_gen ||
        procmem_gen(kproc->user_start) != kproc->tpa_gen ||
        (VADDR)pd->p_env != env || (VADDR)pd->p_hitpa > hitpa ||
        (VADDR)pd->p_hitpa <= tpa)
        return FALSE;

    /* a program loaded from a file already has its address space, with its
     * image in it (kproc_load_begin()) */
    owner = kproc->pending_space;
    if (owner) {
        as = kaspace_backend(owner);
    } else {
        as = aspace_create();
        if (!as) return FALSE;
        owner = kaspace_adopt(as);
        if (!owner) { aspace_destroy(as); return FALSE; }
    }
    kproc->kstack_phys = kstack_alloc(&kproc->kstack_top);
    if (!kproc->kstack_phys) {
        if (owner != kproc->pending_space)
            kaspace_put(owner);
        return FALSE;
    }

    /*
     * The process's own environment block and its basepage + TPA + stack
     * (the PD is the first thing in the TPA allocation, p_hitpa its end):
     * two separate allocations, wherever the window put them.  The environment
     * is user read/write.  The TPA is user read/write too, and also execute
     * only for a process without an image of its own, whose code (put there
     * by the caller of Pexec(PE_BASEPAGE)) is in it; a process with an image
     * runs that, from private pages mapped further down, and its TPA is data.
     * Then its parent's basepage, kernel-only: xterm() writes
     * the exit code through it from ring 0 while this address space is
     * still loaded.  Any failure unwinds the whole address space.
     */
    if (!aspace_map_procmem(as, env, envbytes,
                                   ASPACE_PROT_WRITE | ASPACE_PROT_USER) ||
        !aspace_map_procmem(as, tpa, hitpa - tpa,
                                   kproc->has_image ? ASPACE_PROT_WRITE | ASPACE_PROT_USER
                                                : ASPACE_PROT_WRITE | ASPACE_PROT_EXEC | ASPACE_PROT_USER) ||
        !aspace_map_procmem(as, (VADDR)(uintptr_t)
#if CONF_WITH_NATIVE_SCHED
                                   (kproc->async_parent ? kproc->async_parent : parent),
#else
                                   parent,
#endif
                                   sizeof(PD),
                                   ASPACE_PROT_WRITE)) {
        return prepare_failed(kproc, owner);
    }
    if (!build_ancestors(kproc, as, parent)) {
        return prepare_failed(kproc, owner);
    }
#if CONF_WITH_USER_CALLBACKS
    if (!user_callback_image_load(as))
        return prepare_failed(kproc, owner);
#endif
#if CONF_WITH_X32_VDI
    /* Before moving a PE_LOAD image: every allocation that can fail must
     * precede that irreversible transfer from the launcher's address space. */
    if (!x32_vdi_image_load(as))
        return prepare_failed(kproc, owner);
#endif
    /* the limits of Psetlimit() are the launcher's at this moment (a stack
     * that would run into the image the loader put in the heap stays the
     * default).  Decided before the image is moved below, so that the stack
     * can be mapped first where it is not lazy; the moved image is the only
     * heap block the process will have, at the place it has in the loader's
     * heap. */
    {
        KPROC *pk = kproc_find(parent);

        if (pk && pk != kproc) {
            kproc->lim_core = pk->lim_core;
            kproc->lim_data = pk->lim_data;
            kproc->stack_size = 0;
            if (pk->stack_size && pk->aspace) {
                VADDR highest = USER_HEAP_BASE;

                if (kproc->has_image && !kproc->pending_as && kproc->load_parent)
                    highest = kproc->load_va + (VADDR)kproc->load_pages * USER_PAGE_SIZE;
                else if (kproc->nheap)
                    highest = kproc->heap[kproc->nheap - 1].va +
                              (VADDR)kproc->heap[kproc->nheap - 1].pages * USER_PAGE_SIZE;
                if (USER_STACK_TOP - pk->stack_size - USER_PAGE_SIZE >= highest)
                    kproc->stack_size = pk->stack_size;
            }
        }
    }
#if !CONF_WITH_DEMAND_PAGING
    /* Without demand paging the stack is mapped now, all of it, and before the
     * move below.  (A classic PRG has its stack in its TPA.) */
    if (!kproc->legacy &&
        !aspace_map_private(as, USER_STACK_TOP - stack_bytes(kproc), stack_bytes(kproc),
                            ASPACE_PROT_WRITE | ASPACE_PROT_USER)) {
        return prepare_failed(kproc, owner);
    }
    if (!kproc->legacy)
        kproc->stack_low = USER_STACK_TOP - stack_bytes(kproc);
#endif
    /* The constructor retains this explicit construction space. Keep the
     * PROCESS's active-space view unpublished until preparation commits. */
    if (thread_result) {
        thread = kthread_prepare(pd, owner);
        if (!thread)
            return prepare_failed(kproc, owner);
    }
    /*
     * A built-in image gets its segments as private pages at the fixed
     * addresses of include/procmem.h's layout (one loaded from a file has them
     * already, and one PE_LOAD put in its caller's heap is moved here).  The
     * private stack (a TPA is no stack: the image of a program loaded into it,
     * or the code a caller put there, is not something to push onto) is lazy
     * with CONF_WITH_DEMAND_PAGING: nothing is mapped for it, its pages come
     * with the first touch; without it, it was mapped above.  The move
     * of a PE_LOAD image out of its caller cannot be undone, so nothing that
     * can fail may follow it.
     */
    if (kproc->has_image && !kproc->pending_as &&
        (kproc->load_parent ? !move_loaded_image(kproc, as, parent)
                            : !layout_load(as, &kproc->layout))) {
        kthread_destroy(thread);
        return prepare_failed(kproc, owner);
    }
#if CONF_WITH_ELF_SHARED
    if (kproc->standard_entry && USER_STACK_TOP - kproc->stack_low >
        (kproc->stack_size ? kproc->stack_size : USER_STACK_SIZE)) {
        kthread_destroy(thread);
        return prepare_failed(kproc, owner);
    }
#endif
    if (kproc->legacy)
        kproc->stack_top = hitpa & ~3UL;    /* the top of the TPA: the program sets its own stack */
    else
        kproc->stack_top = USER_STACK_TOP - 8;   /* RSP + 8 divisible by 16 */
    if (kproc->has_image)
        kproc->entry = kproc->layout.entry;
#if CONF_WITH_ELF_SHARED
    if (kproc->standard_entry) {
        kproc->entry = kproc->interp_entry;
        kproc->stack_top = kproc->initial_rsp;
    }
#endif
    kproc->active_space = owner;
    kproc->aspace = as;
    kproc->pending_as = NULL;               /* (the address space is the process's now) */
    kproc->pending_space = NULL;            /* transfer, not an additional reference */
    if (thread_result) *thread_result = thread;
    return TRUE;
}

BOOL kproc_prepare_user(PD *pd, PD *parent)
{
    return kproc_prepare_user_thread(pd, parent, NULL);
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
    VADDR va = (VADDR)(uintptr_t)dta;
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
    VADDR va = (VADDR)(uintptr_t)dta;
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
    VADDR va = (VADDR)(uintptr_t)dta;
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

VADDR kproc_ancestors_va(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    return kproc && kproc->aspace && kproc->ancestors ? USER_ANCESTORS_VA : 0;
}

BOOL kproc_set_image(PD *pd, const PROG_IMAGE *image)
{
    KPROC *kproc = kproc_find(pd);

    if (!kproc || kproc->aspace || kproc->has_image ||
        !image_layout(image, &kproc->layout))
        return FALSE;
    kproc->has_image = TRUE;
    return TRUE;
}

/*
 * A program Pexec() loads from a file (mode 0, and PE_LOAD by a caller with no
 * address space of its own) is read straight into pages of the address space
 * that will be the new process's.  kproc_load_begin() makes that address space
 * and maps [va, va + bytes) in it, the image and the startup area after it
 * (USER_IMAGE_SLACK: zeroed read/write memory for the startup code to
 * build argv and environ in); kproc_load_enter() makes it the one in use, so
 * the loader can address the pages by their user addresses, and
 * kproc_load_leave() puts the previous one back.  The record owns the address
 * space until the launch adopts it (kproc_prepare_user()), or it is destroyed.
 * Leaving is also done by destroying the record, which is what a disk error
 * that jumps past the loader gets.
 */
static ASPACE_ROOT load_prev_root;
static KPROC *load_active;

BOOL kproc_load_begin(PD *pd, VADDR va, VADDR bytes)
{
    KPROC *kproc = kproc_find(pd);
    ASPACE *as;
    KASPACE *owner;

    if (!kproc || kproc->aspace || kproc->pending_as || kproc->has_image || kproc->lost)
        return FALSE;
    as = aspace_create();
    if (!as)
        return FALSE;
    owner = kaspace_adopt(as);
    if (!owner) { aspace_destroy(as); return FALSE; }
    if (!aspace_map_private(as, va, bytes, ASPACE_PROT_WRITE | ASPACE_PROT_USER)) {
        kaspace_put(owner);
        return FALSE;
    }
    kproc->pending_as = as;
    kproc->pending_space = owner;
    return TRUE;
}

void kproc_load_enter(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    if (kproc && kproc->pending_as && !load_active) {
        load_prev_root = aspace_enter(kproc->pending_as);
        load_active = kproc;
    }
}

void kproc_load_leave(void)
{
    if (load_active) {
        load_active = NULL;
        aspace_leave(load_prev_root);
    }
}

/*
 * The file is in the pages kproc_load_begin() mapped, relocated if it had to
 * be, and laid out as `layout`: each segment gets its own permissions now.
 * FALSE if the layout does not satisfy layout_valid(), leaves no
 * room for the whole startup area inside the image window, or the process has
 * no pending address space; the record's destruction frees the pages.
 */
static BOOL protect_layout(ASPACE *as, const PROG_LAYOUT *layout)
{
    ULONG i;

    for (i = 0; i < layout->nseg; i++) {
        const PROG_SEGMENT *s = &layout->seg[i];
        VADDR first = s->vaddr & ~(USER_PAGE_SIZE - 1);
        VADDR end = (s->vaddr + s->memsz + USER_PAGE_SIZE - 1) & ~(USER_PAGE_SIZE - 1);
        UWORD prot = ASPACE_PROT_USER;

        if (s->flags & 2)                   /* PF_W */
            prot |= ASPACE_PROT_WRITE;
        if (s->flags & 1)                   /* PF_X */
            prot |= ASPACE_PROT_EXEC;
        if (!aspace_protect_private(as, first, end - first, prot))
            return FALSE;
    }
    return TRUE;
}

BOOL kproc_set_loaded_image(PD *pd, const PROG_LAYOUT *layout)
{
    KPROC *kproc = kproc_find(pd);

    if (!kproc || kproc->aspace || !kproc->pending_as || kproc->has_image ||
        !layout_valid(layout, FALSE) ||
        ((layout->end + USER_PAGE_SIZE - 1) & ~(USER_PAGE_SIZE - 1)) +
        USER_IMAGE_SLACK > USER_IMAGE_BASE + USER_IMAGE_SIZE ||
        !protect_layout(kproc->pending_as, layout))
        return FALSE;
    kproc->layout = *layout;
    kproc->has_image = TRUE;
    return TRUE;
}

BOOL kproc_standard_entry(PD *pd)
{
#if CONF_WITH_ELF_SHARED
    KPROC *kproc = kproc_find(pd);
    return kproc && kproc->standard_entry;
#else
    (void)pd;
    return FALSE;
#endif
}

#if CONF_WITH_ELF_SHARED
#define INTERPRETER_BASE  0x0f000000UL
#define INTERPRETER_LIMIT 0x0ffc0000UL

static BOOL interpreter_fill(ASPACE *as, const DL_IMAGE *plan, const UBYTE *file, ULONG bias)
{
    ULONG i;
    for (i = 0; i < plan->nload; i++) {
        const DL_LOAD *s = &plan->loads[i];
        if (s->filesz && !aspace_copy_to_user(as, (UQUAD)s->address + bias,
                                             file + s->offset, s->filesz))
            return FALSE;
    }
    if (!aspace_protect_private(as, (UQUAD)plan->base + bias, plan->size, 0))
        return FALSE;
    for (i = 0; i < plan->nload; i++) {
        const DL_LOAD *s = &plan->loads[i];
        UQUAD first = s->address & ~(USER_PAGE_SIZE - 1);
        UQUAD end = ((UQUAD)s->address + s->memsz + USER_PAGE_SIZE - 1) & ~(USER_PAGE_SIZE - 1);
        UWORD prot = ASPACE_PROT_USER;
        if (s->flags & DL_PF_W) prot |= ASPACE_PROT_WRITE;
        if (s->flags & DL_PF_X) prot |= ASPACE_PROT_EXEC;
        if (!aspace_protect_private(as, first + bias, end - first, prot))
            return FALSE;
    }
    return TRUE;
}

LONG kproc_set_interpreter(PD *pd, const DL_IMAGE *program, const UBYTE *file,
                            const DL_IMAGE *interpreter, const UBYTE *interp_file,
                            const char *filename)
{
    KPROC *kproc = kproc_find(pd);
    ULONG bias = 0, ibias, i, fnsize = 0;
    uint32_t rsp = 0, used = 0;        /* (the types of dl_start_stack(): not always ULONG) */
    UQUAD stack_first;
    UBYTE *stack;
    DL_START_INFO info;
    LONG rc = ENSMEM;
    int status;

    if (!kproc || kproc->has_image || kproc->pending_as || kproc->aspace ||
        program->nload > PROG_MAX_SEGMENTS || interpreter->nload > PROG_MAX_SEGMENTS ||
        (program->type == 3 && program->base > USER_IMAGE_BASE) ||
        interpreter->base > INTERPRETER_BASE)
        return EPLFMT;
    if (program->type == 3)
        bias = USER_IMAGE_BASE - program->base;
    ibias = INTERPRETER_BASE - interpreter->base;
    ibias = (ibias + interpreter->align - 1) & ~(interpreter->align - 1);
    if (bias % program->align || (UQUAD)program->base + bias < USER_IMAGE_BASE ||
        (UQUAD)program->base + bias + program->size > USER_IMAGE_BASE + USER_IMAGE_SIZE ||
        (UQUAD)interpreter->base + ibias < INTERPRETER_BASE ||
        (UQUAD)interpreter->base + ibias + interpreter->size > INTERPRETER_LIMIT)
        return EPLFMT;
    while (fnsize < 1024 && filename[fnsize]) fnsize++;
    if (fnsize == 1024)
        return ERANGE;
    stack = kalloc(DL_START_CAPACITY);
    if (!stack)
        return ENSMEM;
    info.phdr = program->phdr + bias;
    info.phnum = program->phnum;
    info.entry = program->entry + bias;
    info.base = ibias;
    info.pagesz = USER_PAGE_SIZE;
    info.basepage = (ULONG)(uintptr_t)pd;
    info.bootdrive = bootdev;
    info.load_bias = bias;
    info.capture_launch = 0;
    info.environment = 0;
#if CONF_WITH_LIBAES
    info.capture_launch = 1;
    info.environment = (ULONG)(uintptr_t)kproc->env_start;
#endif
    status = dl_start_stack(stack, DL_START_CAPACITY, USER_STACK_TOP, filename, fnsize + 1,
                        (const unsigned char *)pd->p_cmdlin, sizeof(pd->p_cmdlin), kproc->env_start,
                        (ULONG)(kproc->env_end - kproc->env_start), &info, &rsp, &used);
    if (status != DL_OK) {
        KDEBUG(("PT_INTERP: stack construction failed %ld\n", (long)status));
        rc = status == DL_NO_SPACE ? ENSMEM : EPLFMT;
        goto done;
    }
    if (!kproc_load_begin(pd, program->base + bias, program->size)) {
        KDEBUG(("PT_INTERP: main reservation failed\n"));
        goto done;
    }
    if (!aspace_map_private(kproc->pending_as, interpreter->base + ibias, interpreter->size,
                           ASPACE_PROT_USER | ASPACE_PROT_WRITE)) {
        KDEBUG(("PT_INTERP: interpreter reservation failed\n"));
        goto done;
    }
    if (!interpreter_fill(kproc->pending_as, program, file, bias) ||
        !interpreter_fill(kproc->pending_as, interpreter, interp_file, ibias)) {
        KDEBUG(("PT_INTERP: image copy/protection failed\n"));
        goto done;
    }
    stack_first = rsp & ~(USER_PAGE_SIZE - 1);
    if (!aspace_load_private(kproc->pending_as, stack_first, USER_STACK_TOP - stack_first,
                            ASPACE_PROT_USER | ASPACE_PROT_WRITE,
                            stack + DL_START_CAPACITY - (USER_STACK_TOP - stack_first),
                            (ULONG)(USER_STACK_TOP - stack_first))) {
        KDEBUG(("PT_INTERP: stack reservation failed\n"));
        goto done;
    }
    kproc->layout.nseg = program->nload;
    kproc->layout.entry = info.entry;
    kproc->layout.end = program->base + bias + program->size;
    for (i = 0; i < program->nload; i++) {
        PROG_SEGMENT *s = &kproc->layout.seg[i];
        s->vaddr = program->loads[i].address + bias;
        s->filesz = program->loads[i].filesz;
        s->memsz = program->loads[i].memsz;
        s->flags = program->loads[i].flags;
        s->src = NULL;                 /* already in the child's private pages */
    }
    kproc->interpreter_layout.nseg = interpreter->nload;
    for (i = 0; i < interpreter->nload; i++) {
        PROG_SEGMENT *s = &kproc->interpreter_layout.seg[i];
        s->vaddr = interpreter->loads[i].address + ibias;
        s->memsz = interpreter->loads[i].memsz;
        s->flags = interpreter->loads[i].flags;
        kproc->interp_bytes += s->memsz;
    }
    kproc->has_image = kproc->standard_entry = TRUE;
    kproc->interp_entry = interpreter->entry + ibias;
    kproc->initial_rsp = rsp;
    kproc->stack_low = stack_first;
    pd->p_tbase = PTR_TO_USERPTR_UNCHECKED((UBYTE *)(uintptr_t)info.entry);
    pd->p_tlen = program->size;
    pd->p_dbase = pd->p_bbase = PTR_TO_USERPTR_UNCHECKED((UBYTE *)(uintptr_t)kproc->layout.end);
    pd->p_dlen = pd->p_blen = 0;
    rc = E_OK;
done:
    kfree(stack);
    return rc;
}
#endif

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
    kproc->user_end = kproc->user_start + procmem_size(kproc->user_start);
    aspace_regrant(launcher->aspace, (VADDR)(uintptr_t)kproc->env_start,
                          kproc->env_end - kproc->env_start,
                          ASPACE_PROT_WRITE | ASPACE_PROT_USER);
    aspace_regrant(launcher->aspace, (VADDR)(uintptr_t)kproc->user_start,
                          procmem_size(kproc->user_start),
                          ASPACE_PROT_WRITE | ASPACE_PROT_EXEC | ASPACE_PROT_USER);
}

#if CONF_WITH_CUIS
/* Identify from trusted creator/allocation metadata, without reading the
 * caller-writable basepage or allowing a child to run before observation. */
LONG kproc_child_image_pid(PD *pd, PD *caller)
{
    KPROC *kproc = kproc_find(pd);
    if (!kproc || kproc->started || kproc->creator != caller || kproc->aspace ||
        procmem_gen(kproc->user_start) != kproc->tpa_gen ||
        procmem_gen(kproc->env_start) != kproc->env_gen) return EIMBA;
    return kproc->pid;
}
#endif


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
    VADDR lowtpa, hitpa, tbase, first;
    UBYTE *alloc_end;

    if (!kproc || kproc->started || kproc->creator != caller || kproc->aspace)
        return EIMBA;
    /* the blocks must still be the ones recorded: freed and reused ones are not */
    if (procmem_gen(kproc->user_start) != kproc->tpa_gen ||
        procmem_gen(kproc->env_start) != kproc->env_gen)
        return EIMBA;
    /* each field is read once into a local: that is the value validated */
    lowtpa = (VADDR)(uintptr_t)USERPTR_TO_PTR(pd->p_lowtpa);
    hitpa = (((VADDR)(uintptr_t)USERPTR_TO_PTR(pd->p_hitpa) + 8UL) & ~15UL) - 8;   /* RSP + 8 divisible by 16 */
    tbase = (VADDR)(uintptr_t)USERPTR_TO_PTR(pd->p_tbase);
    first = (VADDR)(uintptr_t)kproc->user_start + sizeof(PD);
    /* the block's own end, not user_end: that is the bound of the launch
     * last validated, and a launch that failed for lack of memory may be
     * retried with different fields */
    alloc_end = (UBYTE *)kproc->user_start + procmem_size(kproc->user_start);
    if (lowtpa != (VADDR)(uintptr_t)kproc->user_start ||
        hitpa < first || hitpa > (VADDR)(uintptr_t)alloc_end ||
        /* a program with an image of its own starts at the entry point the
         * kernel recorded, not at p_tbase, and that lies in the image */
        (!kproc->has_image && (tbase < first || tbase >= hitpa)) ||
        pd->p_env != PTR_TO_USERPTR((char *)kproc->env_start))
        return EPLFMT;
    pd->p_hitpa = PTR_TO_USERPTR_UNCHECKED((UBYTE *)(uintptr_t)hitpa);
    kproc->user_end = (UBYTE *)(uintptr_t)hitpa;    /* the trusted bound */
    return E_OK;
}

#if KPROC_LEGACY_PRG
void kproc_mark_legacy(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    if (kproc)
        kproc->legacy = TRUE;
}

/*
 * Mshrink() of the TPA of a basepage the caller owns: the pages after the first
 * `len` bytes go back to the window.  They are unmapped from every address
 * space that has them (the process's own, and the launcher's loan) first, so
 * that nothing keeps reaching memory that somebody else is about to be given.
 * Rounded up to whole pages, as the stack that is just below the new end must
 * stay.  p_hitpa of a process that has run stays what it was, as on TOS.
 */
LONG kproc_shrink_tpa(PD *caller, void *blk, long len)
{
    KPROC *kproc = kproc_find((PD *)blk), *k;
    ULONG old, keep;
    int i;

    if (!kproc || kproc->user_start != (UBYTE *)blk)
        return EIMBA;
    /* the block of the caller, or the TPA the caller runs in: a PE_GO process
     * does not own the block its launcher loaded with PE_LOAD */
    if (procmem_owner(blk) != (const void *)caller && blk != (void *)caller)
        return EACCDN;
    old = procmem_size(blk);
    if (len < (long)sizeof(PD))
        return EIMBA;
    keep = ((ULONG)len + USER_PAGE_SIZE - 1) & ~(USER_PAGE_SIZE - 1);
    if (keep > old)
        return EGSBF;
    if (keep == old)
        return E_OK;
    for (k = kproc_list; k; k = k->next) {
        if (k->aspace)
            aspace_shrink_procmem(k->aspace, (VADDR)(uintptr_t)blk, keep);
        for (i = 0; i < KPROC_BORROWS; i++)
            if (k->borrowed[i].bytes && k->borrowed[i].va == (VADDR)(uintptr_t)blk)
                k->borrowed[i].bytes = keep;
    }
    procmem_shrink(blk, keep);
    if (kproc->user_end > (UBYTE *)blk + keep) {
        kproc->user_end = (UBYTE *)blk + keep;
        if (!kproc->aspace)             /* not launched yet: the launch checks p_hitpa against the block */
            ((PD *)blk)->p_hitpa = PTR_TO_USERPTR_UNCHECKED(kproc->user_end);
    }
    return E_OK;
}
#endif

BOOL kproc_borrow(PD *launcher, void *block)
{
    KPROC *kproc = kproc_find(launcher);
    VADDR va = (VADDR)(uintptr_t)block;
    VADDR bytes = procmem_size(block);
    int i;

    if (!kproc || !kproc->aspace)
        return TRUE;                /* a ring-0 launcher reaches all of it */
    if (!bytes)
        return FALSE;
    for (i = 0; i < KPROC_BORROWS; i++)
        if (!kproc->borrowed[i].bytes) {
            if (!aspace_borrow(kproc->aspace, va, bytes))
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
    VADDR va = (VADDR)(uintptr_t)block;
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
    VADDR va = (VADDR)(uintptr_t)block;
    KPROC *kproc;
    int i;

    for (kproc = kproc_list; kproc; kproc = kproc->next)
        for (i = 0; i < KPROC_BORROWS; i++)
            if (kproc->borrowed[i].bytes && kproc->borrowed[i].va == va) {
                if (kproc->aspace)
                    aspace_unborrow(kproc->aspace, va, kproc->borrowed[i].bytes);
                kproc->borrowed[i].bytes = 0;
                return;
            }
}

PADDR kproc_take_kernel_stack(PD *pd, uintptr_t *top)
{
    KPROC *kproc = kproc_find(pd);
    PADDR phys;

    if (!kproc || !kproc->kstack_phys)
        return 0;
    phys = kproc->kstack_phys;
    *top = kproc->kstack_top;
    kproc->kstack_phys = 0;         /* the launcher frees it, after the exit */
    return phys;
}

/*
 * Malloc() memory of a ring-3 process: private pages of its own address space
 * in [USER_HEAP_BASE, USER_HEAP_LIMIT), a whole number of pages
 * per block, placed first fit.  The kernel's own pool (the window the
 * basepage and environment come from) is not where a process's Malloc()
 * memory is: that window is small and shared by every process, and its
 * addresses are the kernel's.  The pages go back with the address space
 * when the process ends; Mfree() and Mshrink() give them back sooner.
 */
BOOL kproc_has_heap(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    return kproc && kproc->aspace;
}

static VADDR stack_bytes(const KPROC *kproc)
{
    return kproc->stack_size ? kproc->stack_size : USER_STACK_SIZE;
}

#if CONF_WITH_USER_CALLBACKS
USER_CALLBACK_STATE *kproc_callbacks_get(PD *pd)
{
    KPROC *k = kproc_find(pd);
    return k ? k->callbacks : NULL;
}

BOOL kproc_callbacks_set(PD *pd, USER_CALLBACK_STATE *state)
{
    KPROC *k = kproc_find(pd);
    if (!k)
        return FALSE;
    k->callbacks = state;
    return TRUE;
}

UQUAD kproc_resume_rsp(PD *pd)
{
    KPROC *k = kproc_find(pd);
    return k ? (k->resume_rsp ? k->resume_rsp : k->stack_top) : 0;
}

void kproc_record_rsp(PD *pd, UQUAD rsp)
{
    KPROC *k = kproc_find(pd);
    if (k)
        k->resume_rsp = rsp;
}

UQUAD kproc_stack_bottom(PD *pd)
{
    KPROC *k = kproc_find(pd);
    return k ? USER_STACK_TOP - stack_bytes(k) : 0;
}
#endif

/* where the Malloc() memory ends: the stack's address space starts, less a guard
 * page that belongs to neither (a stack that outgrows its limit faults) */
static VADDR heap_limit(const KPROC *kproc)
{
    return USER_STACK_TOP - stack_bytes(kproc) - USER_PAGE_SIZE;
}

/* how many bytes of Malloc()/Mmap() blocks the process holds, and how big its image is
 * (what Psetlimit()'s limits 3 and 2 count) */
static VADDR heap_malloc_bytes(const KPROC *kproc)
{
    VADDR bytes = 0;
    ULONG i;
    KASPACE_HEAP *heap = kaspace_heap(kproc->active_space);

    if (heap)
        for (i = 0; i < heap->count; i++)
            bytes += (VADDR)heap->blocks[i].pages * USER_PAGE_SIZE;

    for (i = 0; i < kproc->nheap; i++)
        if (kproc->heap[i].kind == UH_MALLOC)
            bytes += (VADDR)kproc->heap[i].pages * USER_PAGE_SIZE;
#if CONF_WITH_MMAP
    if (kproc->active_space)
        bytes += kaspace_mmap(kproc->active_space)->bytes;
#endif
    return bytes;
}

static VADDR image_bytes(const KPROC *kproc)
{
    VADDR bytes = 0;
    ULONG i;

    if (kproc->has_image)
        for (i = 0; i < kproc->layout.nseg; i++)
            bytes += kproc->layout.seg[i].memsz;
#if CONF_WITH_ELF_SHARED
    bytes += kproc->interp_bytes;
#endif
    return bytes;
}

/* how many more bytes the limits of Psetlimit() let the process Malloc() */
static VADDR alloc_room(const KPROC *kproc)
{
    VADDR used = heap_malloc_bytes(kproc), room = ~(VADDR)0, r;

    if (kproc->lim_data) {
        r = kproc->lim_data > used ? kproc->lim_data - used : 0;
        if (r < room)
            room = r;
    }
    if (kproc->lim_core) {
        VADDR all = used + image_bytes(kproc);

        r = kproc->lim_core > all ? kproc->lim_core - all : 0;
        if (r < room)
            room = r;
    }
    return room;
}

#if CONF_WITH_MMAP
static LONG mapping_length(UQUAD *length)
{
    if (!*length)
        return EINVAL;
    if (*length > PTOS_MMAP_LIMIT - PTOS_MMAP_BASE)
        return EIMBA;
    *length = (*length + USER_PAGE_SIZE - 1) & ~(USER_PAGE_SIZE - 1);
    return E_OK;
}

static BOOL mapping_range(UQUAD address, UQUAD length)
{
    return !(address & (USER_PAGE_SIZE - 1)) &&
           address >= PTOS_MMAP_BASE && address < PTOS_MMAP_LIMIT &&
           length <= PTOS_MMAP_LIMIT - address;
}

static LONG mapping_protection(UQUAD flags, UWORD *prot)
{
    if (flags & ~(UQUAD)(PROT_READ | PROT_WRITE | PROT_EXEC))
        return EINVAL;
    if ((flags & PROT_WRITE) && (flags & PROT_EXEC))
        return EACCDN;
    *prot = flags ? ASPACE_PROT_USER : 0;  /* NONE: supervisor-only, NX */
    if (flags & PROT_WRITE)
        *prot |= ASPACE_PROT_WRITE;
    if (flags & PROT_EXEC)
        *prot |= ASPACE_PROT_EXEC;
    return E_OK;
}

/* Sorted per-process reservations; a hint may fall back to first fit, an
 * exact no-replace request may not.  No existing mapping is ever removed. */
static MMAP_REGION **mapping_slot(KPROC *kproc, UQUAD *address,
                                 UQUAD length, BOOL exact)
{
    MMAP_REGION **link;
    UQUAD at;

    if (*address && mapping_range(*address, length)) {
        for (link = &kaspace_mmap(kproc->active_space)->regions; *link; link = &(*link)->next) {
            if (*address + length <= (*link)->address)
                break;
            if (*address < (UQUAD)(*link)->address + (*link)->length)
                break;
        }
        if (!*link || *address + length <= (*link)->address)
            return link;
    }
    if (exact)
        return NULL;
    at = PTOS_MMAP_BASE;
    for (link = &kaspace_mmap(kproc->active_space)->regions; *link; link = &(*link)->next) {
        if (at + length <= (*link)->address)
            break;
        at = (UQUAD)(*link)->address + (*link)->length;
    }
    if (!mapping_range(at, length))
        return NULL;
    *address = at;
    return link;
}

LONG kproc_mmap(PD *pd, UQUAD address, UQUAD length, UQUAD protection,
                UQUAD flags, UQUAD fd, UQUAD offset)
{
    KPROC *kproc = kproc_find(pd);
    MMAP_REGION **link, *region;
    UWORD prot;
    LONG rc;
    BOOL exact = (flags & MAP_FIXED_NOREPLACE) != 0;

    if (!kproc || !kproc->aspace)
        return EACCDN;
    /* Private anonymous or file-backed reservations; never MAP_SHARED. */
    if ((flags & ~(UQUAD)(MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE)) ||
        !(flags & MAP_PRIVATE))
        return EOPNOTSUPP;
    if ((flags & MAP_ANONYMOUS) ? offset != 0 : (offset > 0x7fffffffUL || (offset & (USER_PAGE_SIZE - 1))))
        return EINVAL;
    if (address > 0xffffffffULL)
        return EIMBA;
    rc = mapping_length(&length);
    if (rc)
        return rc;
    rc = mapping_protection(protection, &prot);
    if (rc)
        return rc;
    if (exact) {
        if (address & (USER_PAGE_SIZE - 1))
            return EINVAL;
        if (!mapping_range(address, length))
            return EIMBA;
    } else {
        address &= ~(USER_PAGE_SIZE - 1);  /* ordinary hint, not MAP_FIXED */
    }
    link = mapping_slot(kproc, &address, length, exact);
    if (!link)
        return exact ? EACCDN : ENSMEM;
    if (length > alloc_room(kproc))
        return ENSMEM;
    region = kalloc(sizeof(*region));
    if (!region)
        return ENSMEM;
    region->backing = NULL; region->offset = 0;
    if (!(flags & MAP_ANONYMOUS)) {
        if (fd >= NUMHANDLES) { kfree(region); return EIHNDL; }
        rc = fileback_get((LONG)fd, &region->backing);
        if (rc) { kfree(region); return rc; }
        /* Initially restrict reservations to the file's rounded extent. */
        if (offset >= fileback_size(region->backing) ||
            length > ((UQUAD)fileback_size(region->backing) + USER_PAGE_SIZE - 1) /
                        USER_PAGE_SIZE * USER_PAGE_SIZE - offset) {
            fileback_put(region->backing); kfree(region); return EIMBA;
        }
        region->offset = (ULONG)offset;
    }
    region->address = (ULONG)address;
    region->length = (ULONG)length;
    region->prot = prot;
    region->next = *link;
    *link = region;
    kaspace_mmap(kproc->active_space)->bytes += length;
    return (LONG)address;
}

/* Shutdown never returns to user code. Drop file views before flushing the
 * filesystem so unlinked chains do not become orphans on a normal reset.
 * Keep address spaces and kernel stacks intact while the syscall finishes. */
void kproc_shutdown_file_mappings(void)
{
    kaspace_shutdown_file_mappings();
}

LONG kproc_munmap(PD *pd, UQUAD address, UQUAD length)
{
    KPROC *kproc = kproc_find(pd);
    MMAP_REGION **link, *region, *tail;
    UQUAD end, first, last, region_end;
    LONG rc;

    if (!kproc || !kproc->aspace)
        return EACCDN;
    if (address & (USER_PAGE_SIZE - 1))
        return EINVAL;
    rc = mapping_length(&length);
    if (rc)
        return rc;
    if (!mapping_range(address, length))
        return EIMBA;
    if (!kaspace_mapping_change(kproc->aspace, address, length)) return EACCDN;
    end = address + length;
    for (link = &kaspace_mmap(kproc->active_space)->regions; *link;) {
        region = *link;
        region_end = (UQUAD)region->address + region->length;
        if (region->address >= end)
            break;
        if (region_end <= address) {
            link = &region->next;
            continue;
        }
        first = address > region->address ? address : region->address;
        last = end < region_end ? end : region_end;
        tail = NULL;
        if (first > region->address && last < region_end) {
            /* A middle cut is the only case needing metadata. Allocate it
             * before touching pages; this case intersects just one region. */
            tail = kalloc(sizeof(*tail));
            if (!tail)
                return ENSMEM;
            tail->address = (ULONG)last;
            tail->length = (ULONG)(region_end - last);
            tail->prot = region->prot;
            tail->backing = region->backing;
            tail->offset = region->offset + (ULONG)(last - region->address);
            tail->next = region->next;
        }
        if (!aspace_release_lazy(kproc->aspace, first, last - first)) {
            kfree(tail);
            return EINTRN;              /* ledger and ownership disagree */
        }
        if (tail) fileback_hold(tail->backing);
        kaspace_mmap(kproc->active_space)->bytes -= last - first;
        if (first == region->address && last == region_end) {
            *link = region->next;
            fileback_put(region->backing);
            kfree(region);
        } else if (first == region->address) {
            region->offset += (ULONG)(last - region->address);
            region->address = (ULONG)last;
            region->length = (ULONG)(region_end - last);
            link = &region->next;
        } else {
            region->length = (ULONG)(first - region->address);
            if (tail)
                region->next = tail;
            link = &region->next;
        }
    }
    return E_OK;                       /* unmapped holes are harmless */
}

/* Split a reservation at an interior boundary using preallocated metadata. */
static void mapping_split(MMAP_REGION *region, UQUAD at, MMAP_REGION *tail)
{
    *tail = *region;
    fileback_hold(tail->backing);
    tail->offset += (ULONG)(at - region->address);
    tail->address = (ULONG)at;
    tail->length = region->length - (ULONG)(at - region->address);
    region->length = (ULONG)(at - region->address);
    region->next = tail;
}

#if CONF_WITH_ELF_SHARED
/* Dynamic image permissions are owned by kernel layouts, not writable PD
 * fields. Only actual segment pages are eligible; holes and other images
 * cannot be claimed by extending the requested range. */
static BOOL standard_image_range(const KPROC *kproc, UQUAD address, UQUAD length)
{
    UQUAD page;
    ULONG which, i;
    if (!kproc->standard_entry || address > USER_VA_LIMIT || length > USER_VA_LIMIT - address)
        return FALSE;
    for (page = address; page < address + length; page += USER_PAGE_SIZE) {
        BOOL found = FALSE;
        for (which = 0; which < 2; which++) {
            const PROG_LAYOUT *layout = which ? &kproc->interpreter_layout : &kproc->layout;
            for (i = 0; i < layout->nseg; i++) {
                UQUAD first = layout->seg[i].vaddr & ~(USER_PAGE_SIZE - 1);
                UQUAD end = (layout->seg[i].vaddr + layout->seg[i].memsz + USER_PAGE_SIZE - 1)
                            & ~(USER_PAGE_SIZE - 1);
                if (page >= first && page < end) found = TRUE;
            }
        }
        if (!found) return FALSE;
    }
    return TRUE;
}
#endif

LONG kproc_mprot(PD *pd, UQUAD address, UQUAD length, UQUAD protection)
{
    KPROC *kproc = kproc_find(pd);
    MMAP_REGION *region, *first = NULL, *last = NULL;
    MMAP_REGION *left = NULL, *right = NULL;
    UQUAD at = address, end;
    UWORD prot;
    LONG rc;

    if (!kproc || !kproc->aspace)
        return EACCDN;
    if (address & (USER_PAGE_SIZE - 1))
        return EINVAL;
    rc = mapping_length(&length);
    if (rc)
        return rc;
    rc = mapping_protection(protection, &prot);
    if (rc)
        return rc;
    if (!kaspace_mapping_change(kproc->aspace, address, length)) return EACCDN;
    if (!mapping_range(address, length)) {
#if CONF_WITH_ELF_SHARED
        if (standard_image_range(kproc, address, length))
            return aspace_protect_private(kproc->aspace, address, length, prot) ? E_OK : EINTRN;
#endif
        return EIMBA;
    }
    end = address + length;
    /* Check full coverage first, including pages not physically backed. */
    for (region = kaspace_mmap(kproc->active_space)->regions; region && at < end; region = region->next) {
        if ((UQUAD)region->address + region->length <= at)
            continue;
        if (region->address > at)
            return EIMBA;
        if (!first)
            first = region;
        last = region;
        at = (UQUAD)region->address + region->length;
    }
    if (at < end)
        return EIMBA;
    /* At most two boundary splits are needed. Reserve both nodes before
     * changing any permission, so allocation failure is atomic. */
    if (address > first->address) {
        left = kalloc(sizeof(*left));
        if (!left)
            return ENSMEM;
    }
    if (end < (UQUAD)last->address + last->length) {
        right = kalloc(sizeof(*right));
        if (!right) {
            kfree(left);
            return ENSMEM;
        }
    }
    if (!aspace_protect_lazy(kproc->aspace, address, length, prot)) {
        kfree(left);
        kfree(right);
        return EINTRN;
    }
    if (left) {
        mapping_split(first, address, left);
        if (last == first)
            last = left;
    }
    if (right)
        mapping_split(last, end, right);
    for (region = kaspace_mmap(kproc->active_space)->regions; region; region = region->next)
        if (region->address >= address && region->address < end)
            region->prot = prot;
    /* Repeated permission changes should not accumulate redundant nodes. */
    for (region = kaspace_mmap(kproc->active_space)->regions; region && region->next;) {
        MMAP_REGION *next = region->next;

        if ((UQUAD)region->address + region->length == next->address &&
            region->prot == next->prot && region->backing == next->backing &&
            (!region->backing || (UQUAD)region->offset + region->length == next->offset)) {
            region->length += next->length;
            region->next = next->next;
            fileback_put(next->backing);
            kfree(next);
        } else {
            region = next;
        }
    }
    return E_OK;
}
#endif

static BOOL heap_room(KPROC *kproc)
{
    struct uheap_block *grown;
    ULONG cap;

    if (kproc->nheap < kproc->heap_capacity)
        return TRUE;
    cap = kproc->heap_capacity ? kproc->heap_capacity * 2 : 16;
    grown = kalloc(cap * sizeof(*grown));
    if (!grown)
        return FALSE;
    if (kproc->heap) {
        memcpy(grown, kproc->heap, kproc->nheap * sizeof(*grown));
        kfree(kproc->heap);
    }
    kproc->heap = grown;
    kproc->heap_capacity = cap;
    return TRUE;
}

static BOOL ordinary_heap_room(KPROC *kproc)
{
    KASPACE_HEAP *heap = kaspace_heap(kproc->active_space);
    KASPACE_HEAP_BLOCK *grown;
    ULONG cap;

    if (!heap) return FALSE;
    if (heap->count < heap->capacity) return TRUE;
    cap = heap->capacity ? heap->capacity * 2 : 16;
    grown = kalloc(cap * sizeof(*grown));
    if (!grown) return FALSE;
    if (heap->blocks) {
        memcpy(grown, heap->blocks, heap->count * sizeof(*grown));
        kfree(heap->blocks);
    }
    heap->blocks = grown;
    heap->capacity = cap;
    return TRUE;
}

/* Merge ordinary and loader reservations into one address-order search.
 * For a new staged image, index is its insertion point in the loader table. */
static VADDR heap_gap(const KPROC *kproc, VADDR bytes, ULONG *index, VADDR *largest)
{
    VADDR at = USER_HEAP_BASE, found = 0;
    ULONG i = 0, j = 0;
    KASPACE_HEAP *heap = kaspace_heap(kproc->active_space);

    *largest = 0;
    for (;;) {
        BOOL staged = i < kproc->nheap &&
            (!heap || j >= heap->count || kproc->heap[i].va < heap->blocks[j].va);
        BOOL ordinary = heap && j < heap->count && !staged;
        VADDR end = staged ? kproc->heap[i].va :
                    ordinary ? heap->blocks[j].va : heap_limit(kproc);

        if (end - at > *largest)
            *largest = end - at;
        if (!found && bytes && end - at >= bytes) {
            found = at;
            *index = i;
        }
        if (staged)
            at = end + (VADDR)kproc->heap[i++].pages * USER_PAGE_SIZE;
        else if (ordinary)
            at = end + (VADDR)heap->blocks[j++].pages * USER_PAGE_SIZE;
        else
            break;
    }
    return found;
}

static VADDR heap_alloc(KPROC *kproc, ULONG bytes, UBYTE kind, PD *pd, BOOL eager)
{
    VADDR va, largest, size;
    ULONG index = 0;

    if (!kproc || !kproc->aspace || !bytes ||
        bytes > heap_limit(kproc) - USER_HEAP_BASE)
        return 0;
    size = ((VADDR)bytes + USER_PAGE_SIZE - 1) & ~(USER_PAGE_SIZE - 1);
    if (kind == UH_MALLOC && size > alloc_room(kproc))
        return 0;                       /* over a Psetlimit() limit */
    if (!(kind == UH_MALLOC ? ordinary_heap_room(kproc) : heap_room(kproc)))
        return 0;
    va = heap_gap(kproc, size, &index, &largest);
    /* (Malloc() memory is lazy with CONF_WITH_DEMAND_PAGING; a program image
     * is filled in by the kernel and moved page by page, so it is backed at
     * once) */
    if (!CONF_WITH_DEMAND_PAGING)
        eager = TRUE;
    if (!va || (eager && !aspace_map_private(kproc->aspace, va, size,
                                                    ASPACE_PROT_WRITE | ASPACE_PROT_USER)))
        return 0;
    if (kind == UH_MALLOC) {
        KASPACE_HEAP *heap = kaspace_heap(kproc->active_space);
        for (index = 0; index < heap->count && heap->blocks[index].va < va; index++) ;
        memmove(&heap->blocks[index + 1], &heap->blocks[index],
                (heap->count - index) * sizeof(heap->blocks[0]));
        heap->blocks[index].va = va;
        heap->blocks[index].pages = (ULONG)(size / USER_PAGE_SIZE);
        heap->count++;
        return va;
    }
    memmove(&kproc->heap[index + 1], &kproc->heap[index],
            (kproc->nheap - index) * sizeof(kproc->heap[0]));
    kproc->heap[index].va = va;
    kproc->heap[index].pages = (ULONG)(size / USER_PAGE_SIZE);
    kproc->heap[index].kind = kind;
    kproc->heap[index].pd = pd;
    kproc->nheap++;
    return va;
}

/*
 * Demand paging. The stack, Malloc() blocks and anonymous mappings reserve
 * address space, but have no physical page until first touch. This is the fault
 * handler's question (and the kernel's, before it reads or writes a user buffer):
 * is `va` in such an area, and if so, back its page now.  FALSE: it is not (a
 * real fault), or no physical page could be had (the process is terminated).
 */
static BOOL lazy_area(const KPROC *kproc, VADDR va, UWORD *prot)
{
#if CONF_WITH_DEMAND_PAGING
    ULONG i;

    *prot = ASPACE_PROT_USER | ASPACE_PROT_WRITE;
#if CONF_WITH_MMAP
    if (kaspace_mmap_protection(kproc->active_space, va, prot))
        return TRUE;
#endif
    if (va >= USER_STACK_TOP - stack_bytes(kproc) && va < USER_STACK_TOP)
        return TRUE;
    if (kaspace_heap_contains(kproc->active_space, va))
        return TRUE;
    for (i = 0; i < kproc->nheap; i++)
        if (va >= kproc->heap[i].va &&
            va < kproc->heap[i].va + (VADDR)kproc->heap[i].pages * USER_PAGE_SIZE)
            return TRUE;
#else
    UNUSED(kproc);
    UNUSED(va);
    UNUSED(prot);
#endif
    return FALSE;               /* (without demand paging nothing is lazy) */
}

#if CONF_WITH_DEMAND_PAGING
/* backs the page of a lazy area; the stack remembers how far down it has been */
static int back_page(KPROC *kproc, VADDR va, UWORD access)
{
    UWORD prot;

#if CONF_WITH_MMAP
    {
        int rc = kaspace_mmap_fault(kproc->active_space, va, access);
        if (rc >= 0) return rc;
    }
#endif
    if (!lazy_area(kproc, va, &prot) || !(prot & ASPACE_PROT_USER)) return 0;
    if (!aspace_fault_in(kproc->aspace, va, prot)) return 2;
    va &= ~(USER_PAGE_SIZE - 1);
    if (va >= USER_STACK_TOP - stack_bytes(kproc) && va < USER_STACK_TOP &&
        (!kproc->stack_low || va < kproc->stack_low))
        kproc->stack_low = va;
    return TRUE;
}

#endif

int kproc_demand_page(PD *pd, VADDR va, UWORD access)
{
#if CONF_WITH_DEMAND_PAGING
    KPROC *kproc = kproc_find(pd);
    UWORD prot;

    if (!kproc || !kproc->aspace)
        return 0;
#if CONF_WITH_MMAP
    {
        int rc = kaspace_mmap_demand_page(kproc->active_space, va, access);
        if (rc >= 0) return rc;
    }
#endif
    {
        int rc = kaspace_heap_demand_page(kproc->active_space, va, access);
        if (rc >= 0) return rc;
    }
    if (!lazy_area(kproc, va, &prot) ||
        !(prot & ASPACE_PROT_USER) || (access & ~prot))
        return 0;
    if (aspace_translate(kproc->aspace, va, NULL, NULL)) {
        return 0;
    }
    return back_page(kproc, va, access);
#else
    UNUSED(pd);
    UNUSED(va);
    UNUSED(access);
    return 0;                   /* nothing is lazy: a fault is a fault */
#endif
}

#if CONF_WITH_DEMAND_PAGING
/* A kernel call needs a page of a lazy area and no physical page is left: the
 * same end as a touch of the page by the process itself, not a bad pointer. */
static void lazy_oom(void)
{
    kcprintf("process: no physical page left for its memory\n");
#if CONF_WITH_X86_64_MEMTEST && CONF_WITH_USER_CALLBACKS
    x86_64_memtest_callback_lazy_oom();
#endif
#if CONF_WITH_NATIVE_SCHED
    ksched_fault(run, 9); /* SIGKILL, as for a user-mode paging failure */
#endif
#if CONF_WITH_USER_CALLBACKS
    user_callback_fault();
#endif
    xterm((UWORD)-1);
}
#endif

/* Whether the process may read (or write) [address, address + size), the pages
 * of its lazy areas that are not there yet included: a range with a gap, or a
 * page the access is not allowed on, is not.  Changes nothing. */
static BOOL range_allowed(KPROC *kproc, VADDR address, ULONG size, BOOL write)
{
    VADDR first = address & ~(USER_PAGE_SIZE - 1);
    VADDR last = (address + size - 1) & ~(USER_PAGE_SIZE - 1), page;

    for (page = first; page <= last; page += USER_PAGE_SIZE) {
        UWORD prot;

        if (aspace_translate(kproc->aspace, page, NULL, &prot)) {
            if (!(prot & ASPACE_PROT_USER)) return FALSE;
            if (write && !(prot & ASPACE_PROT_WRITE)) {
#if CONF_WITH_MMAP
                if (aspace_file_cow(kproc->aspace, page) &&
                    kaspace_mmap_protection(kproc->active_space, page, &prot) &&
                    (prot & ASPACE_PROT_USER) && (prot & ASPACE_PROT_WRITE)) continue;
#endif
                return FALSE;
            }
        } else if (!lazy_area(kproc, page, &prot) ||
                   !(prot & ASPACE_PROT_USER) || (write && !(prot & ASPACE_PROT_WRITE))) {
            return FALSE;
        }
    }
    return TRUE;
}

/* The same, and the pages of its lazy areas that are not there yet get their
 * physical pages now (what a read or write of them by the process would do).
 * The first pass rejects a range before any page is backed.  A range that is
 * fine but cannot be backed ends the process. */
static BOOL demand_range(KPROC *kproc, VADDR address, ULONG size, BOOL write)
{
#if CONF_WITH_DEMAND_PAGING
    VADDR first = address & ~(USER_PAGE_SIZE - 1);
    VADDR last = (address + size - 1) & ~(USER_PAGE_SIZE - 1), page;

    if (!range_allowed(kproc, address, size, write))
        return FALSE;
    for (page = first; page <= last; page += USER_PAGE_SIZE) {
        UWORD prot;
        int rc;
        if (aspace_translate(kproc->aspace, page, NULL, &prot) &&
            (!write || (prot & ASPACE_PROT_WRITE))) continue;
        rc = back_page(kproc, page, write ? ASPACE_PROT_WRITE : 0);
        if (rc != 1) {
            if (rc == 3) {
                kcprintf("process: file mapping I/O error\n");
#if CONF_WITH_NATIVE_SCHED
                ksched_fault(run, 10); /* SIGBUS */
#endif
                xterm((UWORD)-1);
            }
            lazy_oom();
        }
    }
    return TRUE;
#else
    return range_allowed(kproc, address, size, write);   /* all of it is there */
#endif
}

/*
 * Psetlimit(), after MiNT's (sys/k_resource.c): the limit `mode` of the process
 * is set to `value` (0: none; negative: not changed), and the old value is
 * returned.  A limit is inherited by the processes it launches later; memory
 * it already holds is not taken back.  Modes 2 and 3 are MiNT's: the process's
 * memory in total (image and Malloc() blocks) and what it Malloc()s, in bytes.
 * Mode 1, CPU time, is not kept on this port (EINVFN).  The stack is this
 * port's own mode, PSETLIMIT_STACK: how many bytes of address space the stack
 * may grow to (0: the default, USER_STACK_SIZE).  It takes that space
 * from the Malloc() memory, so it fails (ENSMEM) if a block is in the way, and
 * (ERANGE) if the stack has already grown further than the new size or the size
 * is below USER_STACK_MIN.
 */
LONG kproc_psetlimit(PD *pd, WORD mode, LONG value)
{
    KPROC *kproc = kproc_find(pd);
    VADDR old, size, highest;

    if (!kproc || !kproc->aspace)
        return EINVFN;
    switch (mode) {
    case PSETLIMIT_CORE:
        old = kproc->lim_core;
        if (value >= 0)
            kproc->lim_core = (VADDR)value;
        return (LONG)old;
    case PSETLIMIT_DATA:
        old = kproc->lim_data;
        if (value >= 0)
            kproc->lim_data = (VADDR)value;
        return (LONG)old;
    case PSETLIMIT_STACK:
        old = stack_bytes(kproc);
        if (value < 0)
            return (LONG)old;
        if (value == 0) {
            size = USER_STACK_SIZE;
        } else {
            if ((VADDR)value < USER_STACK_MIN)
                return ERANGE;
            size = ((VADDR)value + USER_PAGE_SIZE - 1) & ~(USER_PAGE_SIZE - 1);
        }
        if (size + USER_PAGE_SIZE > USER_STACK_TOP - USER_HEAP_BASE)
            return ENSMEM;
        highest = USER_HEAP_BASE;
        if (kproc->nheap) {
            const struct uheap_block *b = &kproc->heap[kproc->nheap - 1];

            highest = b->va + (VADDR)b->pages * USER_PAGE_SIZE;
        }
        {
            KASPACE_HEAP *heap = kaspace_heap(kproc->active_space);
            if (heap && heap->count) {
                const KASPACE_HEAP_BLOCK *b = &heap->blocks[heap->count - 1];
                VADDR end = b->va + (VADDR)b->pages * USER_PAGE_SIZE;
                if (end > highest) highest = end;
            }
        }
        if (USER_STACK_TOP - size - USER_PAGE_SIZE < highest)
            return ENSMEM;
        if (kproc->stack_low && USER_STACK_TOP - size > kproc->stack_low)
            return ERANGE;
#if !CONF_WITH_DEMAND_PAGING
        /* the stack is mapped, all of it: a bigger one gets its pages now (and
         * a smaller one was refused above, every page counting as touched) */
        if (USER_STACK_TOP - size < kproc->stack_low) {
            if (!aspace_map_private(kproc->aspace, USER_STACK_TOP - size,
                                    kproc->stack_low - (USER_STACK_TOP - size),
                                    ASPACE_PROT_WRITE | ASPACE_PROT_USER))
                return ENSMEM;
            kproc->stack_low = USER_STACK_TOP - size;
        }
#endif
        kproc->stack_size = size == USER_STACK_SIZE ? 0 : size;
        return (LONG)old;
    default:
        return EINVFN;
    }
}

/*
 * Mxalloc()'s mode word, after MiNT (sys/dosmem.c sys_m_xalloc()).  A new
 * block of this port is private pages: no other process can be given access
 * to them, so MiNT's "global", "readable by all" (and "super", "invalid") kinds
 * cannot be honoured and the call says so instead of allocating something else.
 * A program's default protection (the bits 4-7 of its flags) is not applied to
 * Malloc() either: memory is private.  The bit that changes the protection of a
 * block is disabled in MiNT too; "keep" would have the block outlive the process,
 * which an address space of its own cannot.
 */
LONG kproc_uheap_mode(WORD mode)
{
    UWORD m = (UWORD)mode;

    if (m & (MXMODE_CHANGE | MXMODE_KEEP))
        return EINVFN;
    if ((m & MXMODE_PROT) != 0 && (m & MXMODE_PROT) != MXMODE_PROTP)
        return EINVFN;
    return E_OK;
}

/*
 * Maccess(addr, size, mode), MiNT's: 0 if the caller may use the range (mode 0:
 * read and write, 1: read only), else EINVAL, also for any other mode.
 */
LONG kproc_maccess(PD *pd, VADDR address, LONG size, WORD mode)
{
    if ((mode != 0 && mode != 1) || size <= 0)
        return EINVAL;
    return kproc_user_access(pd, address, (ULONG)size, mode == 0) ? E_OK : EINVAL;
}

/*
 * Mvalidate(pid, addr, size, flags), MiNT's: whether the range is memory of the
 * process, and in `flags` (a LONG of the caller, if not NULL) its protection
 * bits and map: private memory, in the one kind of RAM, is 0.  Only the
 * caller's own memory (pid 0, or its own pid) can be asked about: another
 * process's pages are not a thing this one can reach.
 */
LONG kproc_mvalidate(PD *pd, WORD pid, VADDR address, LONG size, UQUAD flags)
{
    ULONG zero = 0;

    if (pid != 0 && pid != kproc_pid(pd))
        return ESRCH;
    if (size <= 0 || !kproc_user_access(pd, address, (ULONG)size, FALSE))
        return EINVAL;
    if (flags && !kproc_copy_to_user(flags, &zero, sizeof(zero)))
        return EINVAL;
    return E_OK;
}

/* Malloc(): the address of `bytes` of fresh zeroed memory, 0 if there is none */
VADDR kproc_uheap_alloc(PD *pd, ULONG bytes)
{
    return heap_alloc(kproc_find(pd), bytes, UH_MALLOC, NULL, FALSE);
}

/* Malloc(-1): the size of the largest block that could be allocated */
ULONG kproc_uheap_largest(PD *pd)
{
    KPROC *kproc = kproc_find(pd);
    VADDR largest;
    ULONG index;

    if (!kproc || !kproc->aspace)
        return 0;
    heap_gap(kproc, 0, &index, &largest);
    /* (Malloc() rounds a request up to whole pages: what is left, down to them) */
    if (largest > (alloc_room(kproc) & ~(USER_PAGE_SIZE - 1)))
        largest = alloc_room(kproc) & ~(USER_PAGE_SIZE - 1);
    /* without demand paging a block is backed when it is handed out: no more
     * than there is memory for.  The table of blocks is grown first if the
     * next Malloc() would have to (heap_room()), so that the memory it takes
     * is not counted as the block's: the answer is then what that Malloc()
     * gets. */
    if (!CONF_WITH_DEMAND_PAGING) {
        if (!ordinary_heap_room(kproc))
            return 0;
        if (largest > aspace_mappable_bytes())
            largest = aspace_mappable_bytes();
    }
    return (ULONG)largest;
}

static struct uheap_block *heap_find(KPROC *kproc, VADDR va)
{
    ULONG i;

    for (i = 0; i < kproc->nheap; i++)
        if (kproc->heap[i].va == va)
            return &kproc->heap[i];
    return NULL;
}

static KASPACE_HEAP_BLOCK *ordinary_heap_find(KPROC *kproc, VADDR va)
{
    KASPACE_HEAP *heap = kaspace_heap(kproc->active_space);
    ULONG i;
    if (!heap) return NULL;
    for (i = 0; i < heap->count; i++)
        if (heap->blocks[i].va == va) return &heap->blocks[i];
    return NULL;
}

/* unmaps and frees private pages of the process: lazy ones, some of which may
 * never have been touched, or, without demand paging, pages that are all there */
static BOOL release_pages(ASPACE *as, VADDR va, VADDR bytes)
{
#if CONF_WITH_DEMAND_PAGING
    return aspace_release_lazy(as, va, bytes);
#else
    return aspace_unmap_private(as, va, bytes);
#endif
}

#if CONF_WITH_USER_CALLBACKS
BOOL kproc_callback_mfree_allowed(PD *pd, UQUAD address)
{
    KPROC *kproc = kproc_find(pd);
    struct uheap_block *b;

    return kproc && kproc->aspace &&
        (ordinary_heap_find(kproc, address) ||
         ((b = heap_find(kproc, address)) != NULL && b->kind == UH_MALLOC));
}

BOOL kproc_callback_stack_stable(PD *pd, UQUAD address, ULONG size)
{
    KPROC *kproc = kproc_find(pd);
    ULONG i;

    if (!kproc || !kproc->aspace)
        return FALSE;
    /* Child basepage/environment grants have the same independent lifetime
     * as PE_LOAD heap images, even though they are writable in the caller. */
    for (i = 0; i < KPROC_BORROWS; i++)
        if (kproc->borrowed[i].bytes &&
            address < kproc->borrowed[i].va + kproc->borrowed[i].bytes &&
            kproc->borrowed[i].va < address + size)
            return FALSE;
    for (i = 0; i < kproc->nheap; i++) {
        const struct uheap_block *b = &kproc->heap[i];

        if ((b->kind == UH_LOADED || b->kind == UH_LENT) &&
            address < b->va + (UQUAD)b->pages * USER_PAGE_SIZE &&
            b->va < address + size)
            return FALSE;
    }
    return TRUE;
}
#endif

/* gives the block back: unmaps its pages and drops its entry */
static LONG heap_release(KPROC *kproc, struct uheap_block *b)
{
    if (!release_pages(kproc->aspace, b->va,
                                     (VADDR)b->pages * USER_PAGE_SIZE))
        return ERR;
    memmove(b, b + 1, (kproc->nheap - 1 - (ULONG)(b - kproc->heap)) * sizeof(*b));
    kproc->nheap--;
    return E_OK;
}

/* Mfree(): E_OK, or EIMBA if va is not the start of a block of this process */
LONG kproc_uheap_free(PD *pd, VADDR va)
{
    KPROC *kproc = kproc_find(pd);
    struct uheap_block *b;
    KASPACE_HEAP_BLOCK *ordinary;
    KASPACE_HEAP *heap;

    if (!kproc || !kproc->aspace)
        return EIMBA;
    ordinary = ordinary_heap_find(kproc, va);
    b = ordinary ? NULL : heap_find(kproc, va);
    if (!ordinary && (!b || b->kind != UH_MALLOC)) return EIMBA;
#if CONF_WITH_USER_CALLBACKS
    if (user_callback_pinned(pd, va, (UQUAD)(ordinary ? ordinary->pages : b->pages) * USER_PAGE_SIZE))
        return EACCDN;
#endif
    if (ordinary) {
        heap = kaspace_heap(kproc->active_space);
        if (!release_pages(kproc->aspace, va, (VADDR)ordinary->pages * USER_PAGE_SIZE))
            return ERR;
        memmove(ordinary, ordinary + 1,
                (heap->count - 1 - (ULONG)(ordinary - heap->blocks)) * sizeof(*ordinary));
        heap->count--;
        return E_OK;
    }
    return heap_release(kproc, b);
}

/* Mshrink(): keeps the first `len` bytes of the block (rounded up to whole
 * pages); EGSBF if that would make it bigger, EIMBA if va is no block */
LONG kproc_uheap_shrink(PD *pd, VADDR va, long len)
{
    KPROC *kproc = kproc_find(pd);
    struct uheap_block *b;
    KASPACE_HEAP_BLOCK *ordinary;
    ULONG pages;
    VADDR keep;

    if (!kproc || !kproc->aspace)
        return EIMBA;
    ordinary = ordinary_heap_find(kproc, va);
    b = ordinary ? NULL : heap_find(kproc, va);
    if (!ordinary && (!b || b->kind != UH_MALLOC)) return EIMBA;
    pages = ordinary ? ordinary->pages : b->pages;
    if (len < 0)
        return ERANGE;
    if (len == 0)
        return kproc_uheap_free(pd, va);
    keep = ((VADDR)len + USER_PAGE_SIZE - 1) / USER_PAGE_SIZE;
    if (keep > pages)
        return EGSBF;
    if (keep == pages)
        return E_OK;
#if CONF_WITH_USER_CALLBACKS
    if (user_callback_pinned(pd, va + keep * USER_PAGE_SIZE,
                            (VADDR)(pages - keep) * USER_PAGE_SIZE))
        return EACCDN;
#endif
    if (!release_pages(kproc->aspace, va + keep * USER_PAGE_SIZE,
                                     (VADDR)(pages - keep) * USER_PAGE_SIZE))
        return ERR;
    if (ordinary) ordinary->pages = (ULONG)keep;
    else b->pages = (ULONG)keep;
    return E_OK;
}

/*
 * The end of a PE_GO child that was loaded by PE_LOAD: its image, which the
 * launch moved out of the loader's heap, goes back to the loader, at the same
 * addresses, as a block it owns (Malloc() memory: it frees it with Mfree()).
 * The loader was stopped all the time and now sees what the program left in
 * its data.  This is for Pterm() and Ptermres() alike; if a Ptermres() process
 * later keeps more than its basepage, that is where it would change.  If the
 * pages cannot be moved (a page table cannot be allocated), they are freed
 * with the child and the loader's block is dropped.
 */
static void give_image_back(KPROC *kproc)
{
    KPROC *pk = kproc_find(kproc->lender);
    struct uheap_block *b, *own;

    /* (the heap table is sorted by address: the child may have put blocks
     * below its image, so look for it where it is) */
    if (!pk || !pk->aspace || !kproc->aspace ||
        !(own = heap_find(kproc, kproc->load_va)) || own->kind != UH_IMAGE ||
        !(b = heap_find(pk, kproc->load_va)) || b->kind != UH_LENT || b->pd != kproc->pd)
        return;
    if (aspace_move_private(kproc->aspace, pk->aspace, b->va,
                                   (VADDR)b->pages * USER_PAGE_SIZE,
                                   ASPACE_PROT_WRITE | ASPACE_PROT_USER)) {
        b->kind = UH_MALLOC;
        b->pd = NULL;
    } else {
        memmove(b, b + 1, (pk->nheap - 1 - (ULONG)(b - pk->heap)) * sizeof(*b));
        pk->nheap--;
    }
}

/* Pexec(PE_GO) (TRUE) or PE_GOTHENFREE: whether the image goes back to its
 * loader when the process ends; set on every launch attempt */
void kproc_set_give_back(PD *pd, BOOL give_back)
{
    KPROC *kproc = kproc_find(pd);

    if (kproc)
        kproc->give_back = give_back;
}

/*
 * Pexec(PE_LOAD) from a ring-3 caller puts the program in the caller's own
 * address space, in its heap, where the caller can read it and patch it
 * before launching it.  The block is the caller's until PE_GO/PE_GOTHENFREE,
 * which moves its pages, at the same addresses, to the new process (see
 * move_loaded_image()); Mfree() leaves it alone, and freeing the child's
 * basepage gives it back.
 */
VADDR kproc_load_alloc(PD *caller, PD *child, ULONG bytes)
{
    KPROC *ck = kproc_find(child);
    VADDR va;

    if (!ck || ck->load_parent)
        return 0;
    va = heap_alloc(kproc_find(caller), bytes, UH_LOADED, child, TRUE);
    if (!va)
        return 0;
    /* recorded with the child at once: a disk error before the load is done
     * jumps past the loader, and destroying the child gives the block back */
    ck->load_parent = caller;
    ck->load_va = va;
    ck->load_pages = (bytes + USER_PAGE_SIZE - 1) / USER_PAGE_SIZE;
    return va;
}

void kproc_load_release(PD *caller, VADDR va)
{
    KPROC *kproc = kproc_find(caller);
    struct uheap_block *b;

    if (kproc && kproc->aspace && (b = heap_find(kproc, va)) != NULL && b->kind == UH_LOADED)
        heap_release(kproc, b);
}

/* the block a never-launched child holds in its loader's heap, if it is still
 * that child's */
static void release_loaded(KPROC *kproc)
{
    KPROC *pk = kproc_find(kproc->load_parent);
    struct uheap_block *b;

    if (pk && pk->aspace && (b = heap_find(pk, kproc->load_va)) != NULL &&
        b->kind == UH_LOADED && b->pd == kproc->pd)
        heap_release(pk, b);
}

/* The process `pd` is the program loaded into [va, va + pages) of `caller`'s
 * heap (kproc_load_alloc()), laid out as `layout` there.  FALSE, with the block
 * still the caller's to release, if that is not so. */
BOOL kproc_set_moved_image(PD *pd, const PROG_LAYOUT *layout, PD *caller, VADDR va, ULONG pages)
{
    KPROC *kproc = kproc_find(pd);
    KPROC *ck = kproc_find(caller);
    struct uheap_block *b;

    if (!kproc || kproc->aspace || kproc->has_image || !ck || !ck->aspace ||
        kproc->load_parent != caller || kproc->load_va != va || kproc->load_pages != pages ||
        !(b = heap_find(ck, va)) || b->kind != UH_LOADED || b->pd != pd || b->pages != pages ||
        !layout_valid_in(layout, FALSE, va, va + (VADDR)pages * USER_PAGE_SIZE))
        return FALSE;
    kproc->layout = *layout;
    kproc->has_image = TRUE;
    return TRUE;
}

/* The launch: the block leaves the caller's address space for the new one,
 * with each segment's own permissions, and is the new process's image. */
static BOOL move_loaded_image(KPROC *kproc, ASPACE *to, PD *launcher)
{
    KPROC *pk = kproc_find(kproc->load_parent);
    struct uheap_block *b;

    if (!pk || !pk->aspace || kproc->load_parent != launcher ||
        !(b = heap_find(pk, kproc->load_va)) || b->kind != UH_LOADED ||
        b->pd != kproc->pd || b->pages != kproc->load_pages || !heap_room(kproc))
        return FALSE;
    if (!aspace_move_private(pk->aspace, to, kproc->load_va,
                                    (VADDR)kproc->load_pages * USER_PAGE_SIZE,
                                    ASPACE_PROT_WRITE | ASPACE_PROT_USER))
        return FALSE;
    kproc->heap[0].va = kproc->load_va;     /* (the new process has no other block yet) */
    kproc->heap[0].pages = kproc->load_pages;
    kproc->heap[0].kind = UH_IMAGE;
    kproc->heap[0].pd = NULL;
    kproc->nheap = 1;
    if (kproc->give_back) {                 /* PE_GO: only lent, the loader's block stays */
        b->kind = UH_LENT;
        kproc->lender = kproc->load_parent;
    } else {
        memmove(b, b + 1, (pk->nheap - 1 - (ULONG)(b - pk->heap)) * sizeof(*b));
        pk->nheap--;
    }
    kproc->load_parent = NULL;
    if (!protect_layout(to, &kproc->layout))
        return FALSE;
    return TRUE;
}

VADDR kproc_user_stack(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    return (kproc && kproc->aspace) ? kproc->stack_top : 0;
}

VADDR kproc_user_entry(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    return (kproc && kproc->aspace) ? kproc->entry : 0;
}

ASPACE *kproc_user_aspace(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    return kproc ? kproc->aspace : NULL;
}

ASPACE_ROOT kproc_user_root(PD *pd)
{
    KPROC *kproc = kproc_find(pd);

    return (kproc && kproc->aspace) ? aspace_root(kproc->aspace) : 0;
}

BOOL kproc_validate_user_dta(VADDR address)
{
    /* the filesystem writes the DTA through this pointer later */
    return kproc_validate_user_write(address, sizeof(DTAINFO));
}

/* True iff [address, address + size) lies inside [start, end). */
static BOOL range_within(VADDR address, ULONG size, const UBYTE *start_p, const UBYTE *end_p)
{
    VADDR start = (VADDR)(uintptr_t)start_p;
    VADDR end = (VADDR)(uintptr_t)end_p;

    if (!start || end > USER_VA_LIMIT || end < start || end - start < size)
        return FALSE;
    return address >= start && address <= end - size;
}

static BOOL validate_user(VADDR address, ULONG size, BOOL write)
{
    KPROC *kproc = kproc_find(run);

    /* No user address reaches USER_VA_LIMIT (the 32-bit ABI's limit on
     * x86-64, the end of the user half on a 32-bit MMU).  Test the
     * subtraction, rather than address + size, so an attacker cannot wrap
     * the range. */
    if (!kproc || !size || address >= USER_VA_LIMIT)
        return FALSE;

    /* A ring-3 process: valid means mapped, user-accessible (and writable
     * for a write) in THIS process's own page tables -- not merely a number
     * that falls in some range. */
    if (kproc->aspace)
        return demand_range(kproc, address, size, write) &&
               aspace_user_range_ok(kproc->aspace, address, size, write);

    /* No address space (a kernel-code process, which runs in ring 0 on the
     * kernel's own tables): the ranges recorded when it was created, wholly
     * inside its environment block or wholly inside its basepage/TPA. */
    return range_within(address, size, kproc->user_start, kproc->user_end) ||
           range_within(address, size, kproc->env_start, kproc->env_end);
}

/* Maccess(), Mvalidate(): may the process touch the range?  Asks only; no page
 * of its lazy areas is backed for the answer. */
BOOL kproc_user_access(PD *pd, VADDR address, ULONG size, BOOL write)
{
    KPROC *kproc = kproc_find(pd);

    if (!kproc || !kproc->aspace || !size || address >= USER_VA_LIMIT ||
        size > USER_VA_LIMIT - address)
        return FALSE;
    return range_allowed(kproc, address, size, write);
}

BOOL kproc_validate_user_range(VADDR address, ULONG size)
{
    return validate_user(address, size, FALSE);
}

#if CONF_WITH_PCLONE
BOOL kproc_validate_user_exec(VADDR address)
{
    KPROC *kproc = kproc_find(run);
    PADDR phys;
    UWORD prot;

    if (!kproc || !kproc->aspace || address >= USER_VA_LIMIT) return FALSE;
    if (!aspace_translate(kproc->aspace, address, &phys, &prot) &&
        !lazy_area(kproc, address & ~(USER_PAGE_SIZE - 1), &prot)) return FALSE;
    return (prot & (ASPACE_PROT_USER | ASPACE_PROT_EXEC)) ==
           (ASPACE_PROT_USER | ASPACE_PROT_EXEC);
}
#endif

BOOL kproc_validate_user_write(VADDR address, ULONG size)
{
    return validate_user(address, size, TRUE);
}

BOOL kproc_copy_from_user(void *dst, VADDR address, ULONG size)
{
    KPROC *kproc = kproc_find(run);

    if (!validate_user(address, size, FALSE))
        return FALSE;
    if (kproc->aspace)
        return aspace_copy_from_user(kproc->aspace, dst, address, size);
    memcpy(dst, (const void *)(uintptr_t)address, size);
    return TRUE;
}

BOOL kproc_copy_to_user(VADDR address, const void *src, ULONG size)
{
    KPROC *kproc = kproc_find(run);

    if (!validate_user(address, size, TRUE))
        return FALSE;
    if (kproc->aspace)
        return aspace_copy_to_user(kproc->aspace, address, src, size);
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
    pd->p_xdta = PTR_TO_USERPTR_UNCHECKED((DTA *)dta);
}

DTAINFO *kproc_get_dta(PD *pd)
{
    return (DTAINFO *)USERPTR_TO_PTR(pd->p_xdta);
}

#endif /* CONF_WITH_KPROC */
