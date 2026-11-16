/* Kernel-only reversible admission for same-PROCESS executable replacement.
 * This is preparation, not Pexec(200) execution or a committed-exec event. */
#ifndef EXEC_REPLACE_H
#define EXEC_REPLACE_H

#include "portab.h"
struct kproc;
struct kthread;
struct kaspace;
struct _pd;
typedef struct kproc_exec_replacement KPROC_EXEC_REPLACEMENT;
typedef struct kproc_exec_image KPROC_EXEC_IMAGE;
struct thread_startup_draft;

#if CONF_WITH_EXEC_REPLACE
/* Caller owns source and a private, unpublished selected root. The current
 * sole managed THREAD/execution is authenticated. Success retains both roots
 * and source identity, admits one replacement operation and defers cancellation
 * through the existing cleanup scope until abort. No mapping or user/kernel
 * continuation changes, THREAD attachment, event or runnable publication occurs.
 * A caller must abort before returning to userspace or leaving its disk-error
 * boundary; this handle is not a scheduler continuation or a launch right.
 * Selected-root and source-sharing admission includes unpublished initial
 * THREADs and existing prepared/committed mapping memberships. */
LONG kproc_exec_replacement_prepare(struct kproc *source, struct kthread *thread,
        struct kaspace *selected, KPROC_EXEC_REPLACEMENT **empty);
/* Revalidates the exact PROCESS number, TID, execution and roots. Preparation
 * cannot mask final/group cancellation or turn a different PROCESS into exec. */
LONG kproc_exec_replacement_validate(const KPROC_EXEC_REPLACEMENT *);
/* Idempotent through the unique owning handle. May run yielding destructors;
 * ownership admission and cancellation protection end only after all releases.
 * Leaves the old user context, root, PID and public completion unchanged. */
void kproc_exec_replacement_abort(KPROC_EXEC_REPLACEMENT **);
/* Existing loader only: bind its fresh, unstarted image container to this
 * selected root before reading file segments. Receipt owns its retained
 * identity and disposal obligation; ordinary exec_common error cleanup stays
 * idempotent. No PROCESS identity is substituted for the source. */
LONG kproc_exec_replacement_load_begin(KPROC_EXEC_REPLACEMENT *,struct kproc *image);
BOOL kproc_exec_replacement_image_ready(const KPROC_EXEC_REPLACEMENT *);
/* Trusted strings, used only by the loader's private staging consumer. */
LONG proc_exec_replacement_stage(KPROC_EXEC_REPLACEMENT *,char *path,char *tail,char *env);

/* KPROC owner internals. Not an address-space adoption or execution capability. */
LONG kproc_exec_replacement_claim(struct kproc *, struct kthread *, struct kaspace *, UQUAD *ticket);
BOOL kproc_exec_replacement_matches(struct kproc *, struct kthread *, struct kaspace *,
                                  struct kaspace *, struct kproc *image,UQUAD ticket);
void kproc_exec_replacement_drop(struct kproc *, UQUAD ticket);
/* Bind the container to the selected root; basepage is the source PD the
 * staged image records as its basepage (initial stack, auxv). */
LONG kproc_exec_image_select(struct kproc *,struct kaspace *,struct _pd *basepage);
BOOL kproc_exec_image_pending(const struct kproc *,const struct kaspace *,BOOL complete);
/* Owning image bundle for the source PROCESS, prepared in the selected root
 * from the completed staging container exactly like an ordinary launch:
 * mapping membership, new environment, the source's TPA/basepage, parent and
 * controller mappings, ancestors, private stack and a fresh syscall stack.
 * Every step is reversible; the source PROCESS is unchanged until commit.
 * Requires *empty == NULL. Abort is idempotent through the unique handle. */
LONG kproc_exec_image_prepare(struct kproc *source, struct kproc *image,
                              struct kaspace *selected, KPROC_EXEC_IMAGE **empty);
void kproc_exec_image_abort(KPROC_EXEC_IMAGE **);
/* The bundle's program-entry startup draft, moved to the scheduler handoff. */
struct thread_startup_draft *kproc_exec_image_take_startup(KPROC_EXEC_IMAGE *);
/* Masked-window preflight of every no-fail commit precondition. */
BOOL kproc_exec_image_sealable(const KPROC_EXEC_IMAGE *);
/* Quiescent scheduler root only, after kthread_exec_rebind(): no allocation,
 * callback, user copy or failure return. */
void kproc_exec_image_commit(KPROC_EXEC_IMAGE *);
/* Off-root, on the relaunched execution: releases the old root/memberships. */
void kproc_exec_image_retire(KPROC_EXEC_IMAGE **);
/* Receipt side of the commit. transfer: at the no-fail boundary, ends the
 * receipt's cleanup scope. commit: at the quiescent root, drops the ticket.
 * retire: off-root, disposes the container that now owns the old
 * environment and releases the receipt's references. */
void kproc_exec_replacement_transfer(KPROC_EXEC_REPLACEMENT *);
void kproc_exec_replacement_commit(KPROC_EXEC_REPLACEMENT *);
void kproc_exec_replacement_retire(KPROC_EXEC_REPLACEMENT **);
struct kproc *kproc_exec_replacement_image(const KPROC_EXEC_REPLACEMENT *);
struct kaspace *kproc_exec_replacement_selected(const KPROC_EXEC_REPLACEMENT *);
/* Scheduler-root commit entry for public Pexec(200). Consumes the receipt,
 * bundle and draft on success, which never returns; every error precedes the
 * no-fail boundary and leaves the three handles with the caller. */
LONG ksched_exec_replace(KPROC_EXEC_REPLACEMENT **, KPROC_EXEC_IMAGE **);
/* Committed-exec generation producer, for the scheduler-root commit only.
 * Check capacity while failure is still allowed; advance has no failure return. */
BOOL kproc_exec_generation_capacity(const struct kproc *);
void kproc_exec_generation_advance(struct kproc *);
#endif
#if CONF_WITH_EXEC_REPLACEMENT_TEST
/* Test gate only: fail after source/old-root/new-root/final-validation prefix. */
void kproc_exec_replacement_test_fail_after(UWORD stage);
#define EXEC_REPLACEMENT_PROBE 0xc9700000UL /* private Pclone test transport */
LONG kproc_exec_replacement_probe(UWORD fail_after);
/* Test gate only: fail kproc_exec_image_prepare after the given step (1..8). */
void kproc_exec_image_test_fail_after(UWORD stage);
/* Test gate only: fail ksched_exec_replace() at the given stage (1..10). */
void ksched_exec_replace_test_fail_after(UWORD stage);
#define EXEC_REPLACE_FAILURES 10
#endif
#endif
