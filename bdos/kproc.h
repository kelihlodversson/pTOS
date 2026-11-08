/*
 * kproc.h - kernel-private process state
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef KPROC_H
#define KPROC_H

#include "bdosdefs.h"
#if CONF_WITH_USER_ASPACE
#if CONF_WITH_MMAP
#include "mman.h"
/* Kernel-owned mapping records: only this process's linker window is
 * affected.  Full-width syscall slots are checked before narrowing. */
LONG kproc_mmap(PD *pd, UQUAD address, UQUAD length, UQUAD protection,
                UQUAD flags, UQUAD fd, UQUAD offset);
void kproc_shutdown_file_mappings(void);
LONG kproc_munmap(PD *pd, UQUAD address, UQUAD length);
LONG kproc_mprot(PD *pd, UQUAD address, UQUAD length, UQUAD protection);
#endif
#include "procmem.h"      /* ASPACE */
#include "progimage.h"
#endif

/*
 * A classic PRG on the 68k keeps the TOS contract (doc/mmu-address-space.txt):
 * its TPA is the free memory (up to KPROC_LEGACY_TPA_MAX), it starts with its
 * stack pointer at the top of the TPA and manages it itself, and Mshrink()
 * gives the rest back.  Programs in ELF have a private stack and no such TPA.
 */
#if CONF_WITH_USER_ASPACE && defined(__m68k__) && CONF_WITH_PRG_LOADER
#define KPROC_LEGACY_PRG 1
#define KPROC_LEGACY_TPA_MAX  0x100000L    /* half of the window of procmem */
#else
#define KPROC_LEGACY_PRG 0
#endif



#if CONF_WITH_USER_CALLBACKS
#include "user_callback.h"
USER_CALLBACK_STATE *kproc_callbacks_get(PD *pd);
BOOL kproc_callbacks_set(PD *pd, USER_CALLBACK_STATE *state);
UQUAD kproc_resume_rsp(PD *pd);
void kproc_record_rsp(PD *pd, UQUAD rsp);
UQUAD kproc_stack_bottom(PD *pd);
/* A callback stack cannot borrow pages whose loaded-child lifetime can
 * release or transfer them independently of this process's stack pins. */
BOOL kproc_callback_stack_stable(PD *pd, UQUAD address, ULONG size);
BOOL kproc_callback_mfree_allowed(PD *pd, UQUAD address);
#endif

typedef struct dta_info DTAINFO;

BOOL kproc_create(PD *pd);
void kproc_destroy(PD *pd);
void kproc_set_dta(PD *pd, DTAINFO *dta);
DTAINFO *kproc_get_dta(PD *pd);
#if CONF_WITH_KPROC
typedef struct kproc KPROC;
#include "process_identity.h"
#include "process_completion.h"
/* Snapshot queries never own wait/reap rights. Numbers are not kernel pointers,
 * PIDs, TIDs or GUI client IDs. Retained identity reserves its number/PID only. */
UQUAD kproc_ref_number(const KPROC *process);
LONG kproc_identity_info(PD *caller, PROCESS_IDENTITY *info);
/* A successful retain owns one reference. The live PD lookup holds another.
 * After destruction the retained record is an identity/completion snapshot:
 * it does not retain or provide access to the PD or live process resources.
 * References are serialized by the current cooperative kernel. */
KPROC *kproc_retain(PD *pd);
void kproc_release(KPROC *process);
WORD kproc_ref_pid(const KPROC *process);
/* Completion may already have been recorded while the PD is still present. */
BOOL kproc_ref_has_basepage(const KPROC *process);
BOOL kproc_ref_status(const KPROC *process, UWORD *status);
void kproc_complete(PD *pd, UWORD status);
/* Pending fault intent is not completion. Scheduler callers attribute it only
 * to the final THREAD; untracked nested/legacy PROCESSes have no sibling state. */
void kproc_note_fault(PD *pd, WORD signal);
BOOL kproc_ref_completion(const KPROC *process, PROCESS_COMPLETION *out);
/* Delivery readiness is later than snapshot availability. Caller retains the
 * identity; publish only after teardown and scheduler/public-wait publication. */
BOOL kproc_ref_terminal(const KPROC *process);
void kproc_publish_observers(KPROC *process);
#if CONF_WITH_X86_64_MEMTEST
/* Exercise the production allocator on a local test cursor, never reset the
 * live registry's non-recycled number counter. */
BOOL kproc_test_number_next(UQUAD *previous, UQUAD *number);
UQUAD kproc_test_number_cursor(UQUAD replacement); /* serialized test injection only */
/* Restrict the next PID search to a bounded number of candidates for tests. */
void kproc_test_pid_search(WORD previous, UWORD candidates);
#if CONF_WITH_USER_ASPACE
/* Test private pending images only; this does not enable active VM sharing. */
struct kaspace;
ULONG kproc_test_child_ticks(const KPROC *process);
void kproc_test_age_unstarted(KPROC *process, ULONG ticks);
struct kaspace *kproc_test_retain_pending(PD *pd);
struct kaspace *kproc_test_retain_active(PD *pd);
/* Non-runnable ownership test only; does not establish VM sharing policy. */
BOOL kproc_test_share_active(PD *pd, struct kaspace *space);
#endif
#endif
/* The launching process, recorded by proc_go() and used by Pterm() instead
 * of the user-writable p_parent field.  kproc_get_parent() halts if there
 * is no record (like kproc_get_dta()). */
void kproc_set_parent(PD *pd, PD *parent);
PD *kproc_get_parent(PD *pd);
#if CONF_WITH_USER_ASPACE
/* Borrowed only for immediate notification; NULL if the tracked parent has
 * retired. Does not keep its PD or heavyweight resources alive. */
PD *kproc_notification_parent(PD *pd);
#endif
void kproc_mark_started(PD *pd);        /* proc_go() launched it */
/* The process-id calls (bdos/mintproc.c).  A pid is 1..32767 and names a record;
 * 0 is the kernel's own context, which has none and is every boot-time basepage's
 * launcher. */
WORD kproc_pid(PD *pd);                 /* 0: no record */
/* Immutable loaded-image basename, retained with PROCESS identity after exit.
 * Copies a complete NUL-terminated snapshot to kernel storage, or ERANGE
 * without writing. The caller owns the reference; no PD is borrowed. */
void kproc_set_executable_name(PD *pd, const char *path);
LONG kproc_ref_name(const KPROC *process, char *name, ULONG capacity);
#if CONF_WITH_CUIS
/* Snapshot all live standard-handle aliases into OPNFILES counters. */
void kproc_standard_file_refs(ULONG *references);
#endif
WORD kproc_parent_pid(PD *pd);          /* 0: launched by the kernel, or no record */
SBYTE kproc_get_prio(PD *pd);
void kproc_set_prio(PD *pd, SBYTE prio);
/* Prusage(): the 8 longs of MiNT's, in milliseconds and bytes */
void kproc_usage(PD *pd, ULONG r[8]);
ULONG kproc_count(void);                /* live records, for leak tests and S_MEMINFO */
/* kproc_destroy() that also says whether the record belonged to a basepage
 * that was never launched, whose inherited file and directory references
 * are therefore still held. */
BOOL kproc_discard(PD *pd);
#if CONF_WITH_USER_ASPACE
void kproc_discard_after_free(PD *pd);
#endif
BOOL kproc_unlaunched(PD *pd);          /* has a record and was never launched */
#endif
#if CONF_WITH_USER_ASPACE
struct kthread;
struct kaspace;
/* Borrowed execution policy; terminal processes cannot publish new work. */
#if CONF_WITH_PTERMGRP
/* Serialized membership latch; existing execution remains valid until handoff.
 * First accepted status wins. Retained identity, never reusable PD identity. */
BOOL kproc_request_group_exit(KPROC *process, UWORD status);
BOOL kproc_group_status(const KPROC *process, UWORD *status);
#endif
BOOL kproc_thread_start_allowed(const KPROC *process);
BOOL kproc_thread_is_sole(KPROC *process, struct kthread *thread);
/* Caller already holds the identity; only live resources may be acquired. */
KPROC *kproc_retain_live(KPROC *process);
PD *kproc_live_pd(const KPROC *process); /* borrowed during serialized live-resource operation */
BOOL kproc_clone_inherit_identity(KPROC *target, KPROC *source);
void proc_dispose_process(KPROC *process);
struct kaspace *kproc_active_space(const KPROC *process); /* borrowed */
/* Explicit construction-space attachment never publishes active_space.
 * identity_only constructs a trusted non-runnable identity; it cannot install
 * a backend through ordinary KTHREAD setters. */
BOOL kproc_thread_attach_space(KPROC *process, struct kaspace *construction_space,
    struct kthread *thread, KPROC **retained_process, struct kaspace **space,
    struct kthread **next, BOOL identity_only);
/* Identity-only VM adoption into an independently prepared resource container.
 * Success transfers space. Failure changes neither object nor references. */
BOOL kproc_clone_target_ready(const KPROC *process, struct kthread *thread);
BOOL kproc_clone_adopt_space(KPROC *process, struct kthread *thread, struct kaspace *space);
#if CONF_WITH_NATIVE_SCHED
BOOL kproc_clone_prepare_compat(KPROC *, struct kthread *, struct kaspace *);
void kproc_clone_drop_compat(KPROC *);
BOOL kproc_clone_run_ready(KPROC *, struct kthread *, struct kaspace *);
BOOL kproc_clone_prepare_controller(KPROC *process, PD *controller);
BOOL kproc_clone_prepare_parent(KPROC *, KPROC *);
BOOL kproc_clone_parent_ready(const KPROC *);
BOOL kproc_clone_execution_bound(KPROC *, struct kthread *);
BOOL kproc_clone_sibling_ready(KPROC *, struct kthread *, struct kaspace *);
void kproc_clone_commit_sibling(KPROC *, struct kthread *);
void kproc_clone_commit_run(KPROC *, struct kaspace *, void *, PADDR, uintptr_t, BOOL);
void *kproc_clone_startup(PD *);
#if CONF_WITH_X86_64_MEMTEST
void kproc_test_fail_clone_controller(void);
void kproc_test_controller_first(KPROC *process);
void kproc_test_fail_clone_compat(UWORD stage);
#endif
#endif
/* Prepared association. Both returned references are owned by the thread;
 * cleanup must not begin until all such associations are safely detached. */
BOOL kproc_thread_attach(PD *pd, struct kthread *thread, KPROC **process,
                         struct kaspace **space, struct kthread **next);
/* Return TRUE when the last association must finish deferred disposal. */
BOOL kproc_thread_detach(KPROC *process, struct kthread *thread,
                         struct kthread *next);
/* Borrowed retained process; commit cleanup once or return FALSE. */
BOOL kproc_begin_disposal(KPROC *process, PD **pd);
/* Reserve a single-thread launcher handoff; only its owner can commit it. */
/* Architecture launcher: FALSE leaves legacy single-thread teardown safe.
 * Success retains identity and commits to its safe continuation. */
BOOL proc_defer_exit(PD *pd, UWORD status, BOOL resident, ULONG resident_len);
/* On TRUE the existing launcher consumes this reference, publishing only
 * after its child syscall stack is reclaimed. FALSE transfers nothing. */
BOOL proc_defer_observer(PD *pd, KPROC *observer);
/* Borrow handoff reference on success. Escaping disk errors consume it and
 * panic; other teardown callbacks must not exit non-locally. */
void proc_finalize_exit(KPROC *process, UWORD status, BOOL resident, ULONG resident_len);
BOOL kproc_prepare_exit_handoff(KPROC *process);
/* Backend-owned executing threads use the launcher handoff. A non-final
 * thread leaves PROCESS live; only the final association reserves disposal.
 * Detachment is deferred until the caller has left its syscall stack. */
BOOL kproc_prepare_thread_exit_handoff(KPROC *process, struct kthread *thread);
BOOL kproc_finish_thread_handoff(KPROC *process, struct kthread *thread);
void kproc_require_exit_thread(PD *pd, struct kthread *thread);
BOOL kproc_exit_handoff_pending(const KPROC *process);
BOOL kproc_commit_exit_handoff(KPROC *process, PD **pd);
/* Commit the existing single-thread xterm cleanup while lookup owns KPROC. */
BOOL kproc_begin_exit(PD *pd);
/* Prepare completion only for the sole stopped association of a started
 * process; the caller must immediately reap it from a safe context. */
BOOL kproc_request_completion(KPROC *process, struct kthread *thread, UWORD status);
BOOL kproc_pending_completion(const KPROC *process, UWORD *status);
/* Legacy/unmanaged xterm cannot tear down attached non-runnable identities. */
void kproc_require_no_threads(PD *pd);
/*
 * Builds the ring-3 address space for a process about to be launched:
 * its environment block and its basepage..p_hitpa range, and `parent`'s
 * basepage kernel-only.  FALSE
 * (nothing built, nothing leaked) if memory ran out; the caller reports
 * ENSMEM.  The address space belongs to the KPROC record and is freed
 * with it by kproc_destroy(), exactly once.  kproc_create() must have
 * succeeded for pd first.
 */
#if CONF_WITH_ELF_SHARED
#include "../lib/ldso/image.h"
LONG kproc_set_interpreter(PD *pd, const DL_IMAGE *program, const UBYTE *file,
                            const DL_IMAGE *interpreter, const UBYTE *interp_file,
                            const char *filename);
#endif
BOOL kproc_standard_entry(PD *pd);
BOOL kproc_prepare_user(PD *pd, PD *parent);
/* Optional generic identity construction is part of preparation, before any
 * consuming image transfer. Failure leaves *thread NULL with no association. */
BOOL kproc_prepare_user_thread(PD *pd, PD *parent, struct kthread **thread);
#if CONF_WITH_NATIVE_SCHED
void kproc_add_child_usage(PD *pd, ULONG milliseconds);
void kproc_commit_async(PD *pd);
BOOL kproc_prepare_async(PD *pd, PD *parent, PD *controller);
BOOL kproc_prepare_async_thread(PD *pd, PD *parent, PD *controller, struct kthread **thread);
#endif
/*
 * Makes the process a built-in x32 program: kproc_prepare_user() will also
 * map the image's segments as private pages of its address space.  Must
 * come after kproc_create() and before the launch; FALSE if the image does
 * not pass image_check() or the address space already exists.
 * The entry point is then the image's own, kept in the KPROC record and
 * never read back from the user-writable p_tbase.
 */
BOOL kproc_set_image(PD *pd, const PROG_IMAGE *image);
/* A program Pexec() loads from a file (bdos/elfld.c) is read straight into pages
 * of the address space that will be the new process's: kproc_load_begin() makes
 * it and maps [va, va + bytes) in it, the image and the startup area after it
 * (USER_IMAGE_SLACK); kproc_load_enter() makes it the one in use while
 * the loader fills the pages, kproc_load_leave() puts the previous one back (and
 * destroying the process does too, for a disk error that jumps past the loader).
 * kproc_set_loaded_image() then says where the segments are (`layout`, already
 * relocated if it had to be), and gives each its permissions.  The launch adopts
 * the address space.  If the launch fails after that the program is gone, and
 * the basepage can only be freed. */
BOOL kproc_load_begin(PD *pd, VADDR va, VADDR bytes);
void kproc_load_enter(PD *pd);
void kproc_load_leave(void);
BOOL kproc_set_loaded_image(PD *pd, const PROG_LAYOUT *layout);
/*
 * Pexec(PE_LOAD) from a ring-3 caller loads the program into the caller's own
 * address space, in its heap, where it can read and patch it before the
 * launch: kproc_load_alloc() takes the block for the process `child` that is
 * being made and records it with the child, so that destroying the child, a
 * failed load included, gives it back (0: none), the loader reads the file into it, in place, and
 * kproc_set_moved_image() makes it the child's image, laid out as `layout`.
 * Nothing is mapped for the child yet: PE_GOTHENFREE moves the block's pages, at the
 * same addresses, from the caller's address space to the child's, with each
 * segment's permissions.  The caller cannot free the block (Mfree() leaves it
 * alone); freeing the child's basepage unlaunched gives it back, and so does
 * kproc_load_release().  With PE_GOTHENFREE the pages stay with the child.
 * Ring-3 PE_GO is currently rejected. The legacy give-back path remains for
 * compatible internal callers.
 */
VADDR kproc_load_alloc(PD *caller, PD *child, ULONG bytes);
void kproc_load_release(PD *caller, VADDR va);
BOOL kproc_set_moved_image(PD *pd, const PROG_LAYOUT *layout, PD *caller, VADDR va, ULONG pages);
/* Legacy PE_GO give-back: call before kproc_prepare_user(). */
void kproc_set_give_back(PD *pd, BOOL give_back);
/* Fsfirst()/Fsnext() search state kept kernel-side: kproc_dta_save() after a
 * search, kproc_dta_restore() before Fsnext() (FALSE: no search was made with
 * this DTA, the search is over). */
BOOL kproc_dta_restore(PD *pd, DTAINFO *dta);
void kproc_dta_save(PD *pd, const DTAINFO *dta);
void kproc_dta_forget(PD *pd, const DTAINFO *dta);   /* a new Fsfirst() starts */
VADDR kproc_ancestors_va(PD *pd);       /* where its basepage copies are; 0 if none */
VADDR kproc_user_entry(PD *pd);         /* image entry point; 0 if no image (it starts at p_tbase) */
/* Hands the process's kernel stack (the one its system calls run on) to the
 * launcher, which frees it with kstack_free() once the process has
 * exited and the launcher runs on its own stack again: the process's last
 * system call, Pterm(), is still using it while the record is destroyed.
 * Returns the physical base (0 if none) and the initial stack pointer. */
/*
 * A ring-3 process launching a child (Pexec() from its own system call)
 * builds the child's blocks under its own page tables.  kproc_borrow() maps a
 * newly allocated block into the launcher's address space, supervisor-only
 * (FALSE if that fails: the caller frees the block); a ring-0 launcher needs
 * nothing.  kproc_unborrow() takes it away again, and must run before the
 * block is freed.
 */
BOOL kproc_borrow(PD *launcher, void *block);
/* Pexec() from ring 3: who made a basepage, handing its blocks to that
 * caller, and checking a basepage it asks to launch (E_OK, EIMBA, EPLFMT). */
void kproc_set_creator(PD *pd, PD *creator);
void kproc_hand_over(PD *caller, PD *child);
LONG kproc_check_launch(PD *pd, PD *caller);
/* CUIS-only: identify a live unlaunched image belonging to this caller. */
LONG kproc_child_image_pid(PD *pd, PD *caller);
void kproc_unborrow(void *block);
ULONG kproc_borrow_count(PD *launcher, void *block);
PADDR kproc_take_kernel_stack(PD *pd, uintptr_t *top);
/*
 * Malloc() memory of a process with an address space: private pages in
 * [USER_HEAP_BASE, USER_HEAP_LIMIT), freed with the process;
 * lazy: a page gets its physical page when it is first touched.
 * Alloc returns the address of `bytes` rounded up to whole pages (0: none),
 * largest what Malloc(-1) reports; free and shrink are Mfree() and
 * Mshrink() of a block's start (EIMBA: no such block, EGSBF: bigger).
 */
BOOL kproc_has_heap(PD *pd);
/* The page fault handler's question: `va` is in lazy stack, Malloc() or anonymous memory of
 * the process, whose page has not been touched yet.  0: it is not (a real fault);
 * 1: it is, and the page is backed now; 2: it is, and no physical page was left
 * (the process is to be terminated); 3: backing-file I/O failed. */
/* access: ASPACE_PROT_WRITE for a write, ASPACE_PROT_EXEC for a fetch,
 * zero for a read. Disallowed access returns 0 without allocating a page. */
int kproc_demand_page(PD *pd, VADDR va, UWORD access);
/* Psetlimit(): modes 2 and 3 are MiNT's (total memory, Malloc()ed memory, in
 * bytes), PSETLIMIT_STACK is this port's (the stack's address space); see
 * bdos/kproc.c.  Returns the old value, or a negative error code. */
#define PSETLIMIT_CORE  2
#define PSETLIMIT_DATA  3
#define PSETLIMIT_STACK 0x100
LONG kproc_psetlimit(PD *pd, WORD mode, LONG value);
/* The mode word of Mxalloc() (MiNT's, sys/mint/mem.h): bits 0-2 the RAM kind
 * (there is one kind of memory here: accepted, and ignored), bit 3 asks to
 * change the protection of a block already allocated (MiNT has that disabled),
 * bits 4-7 the protection of a new block plus one (0: the program's default),
 * 0x2000 zeroed memory (it always is), 0x4000 "keep after termination". */
#define MXMODE_RAMKIND  0x0007
#define MXMODE_CHANGE   0x0008
#define MXMODE_PROT     0x00f0
#define MXMODE_PROTP    0x0010  /* private: nobody else sees it (the only kind here) */
#define MXMODE_ZERO     0x2000
#define MXMODE_KEEP     0x4000
/* E_OK if a block of this mode can be allocated from the private heap, else
 * EINVFN: a protection other than private (others cannot be given access to
 * memory of this process), a protection change of an existing block, memory
 * that outlives the process. */
LONG kproc_uheap_mode(WORD mode);
/* Maccess(), Mvalidate(): MiNT's checks of the caller's own memory */
LONG kproc_maccess(PD *pd, VADDR address, LONG size, WORD mode);
LONG kproc_mvalidate(PD *pd, WORD pid, VADDR address, LONG size, UQUAD flags);
VADDR kproc_uheap_alloc(PD *pd, ULONG bytes);
ULONG kproc_uheap_largest(PD *pd);
LONG kproc_uheap_free(PD *pd, VADDR va);
LONG kproc_uheap_shrink(PD *pd, VADDR va, long len);
#if KPROC_LEGACY_PRG
void kproc_mark_legacy(PD *pd);
LONG kproc_shrink_tpa(PD *caller, void *blk, long len);
#endif
VADDR kproc_user_stack(PD *pd);         /* initial RSP of the private stack of a prepared process; 0 if not prepared */
ASPACE_ROOT kproc_user_root(PD *pd);          /* 0 if not prepared */
ASPACE *kproc_user_aspace(PD *pd);   /* NULL if none (tests) */
BOOL kproc_validate_user_dta(VADDR address);
/* valid user memory of the current process: mapped, user-accessible and (for
 * _write) writable in its own address space */
BOOL kproc_validate_user_range(VADDR address, ULONG size);
#if CONF_WITH_PCLONE
/* Validate that a user entry point is mapped and executable. */
BOOL kproc_validate_user_exec(VADDR address);
#endif
/* the same question for Maccess()/Mvalidate(): the process's own access, with
 * no page backed for the answer */
BOOL kproc_user_access(PD *pd, VADDR address, ULONG size, BOOL write);
BOOL kproc_validate_user_write(VADDR address, ULONG size);
BOOL kproc_copy_from_user(void *dst, VADDR address, ULONG size);
BOOL kproc_copy_to_user(VADDR address, const void *src, ULONG size);
/* No demand backing, allocation, I/O or xterm. Call after ordinary preflight;
 * later mapping changes fail instead of unwinding resource-owning callers. */
BOOL kproc_copy_from_user_resident(void *dst, VADDR address, ULONG size);
BOOL kproc_copy_to_user_resident(VADDR address, const void *src, ULONG size);
#endif

#endif
