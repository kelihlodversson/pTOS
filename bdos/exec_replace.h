/* Kernel-only reversible admission for same-PROCESS executable replacement.
 * This is preparation, not Pexec(200) execution or a committed-exec event. */
#ifndef EXEC_REPLACE_H
#define EXEC_REPLACE_H

#include "portab.h"
struct kproc;
struct kthread;
struct kaspace;
typedef struct kproc_exec_replacement KPROC_EXEC_REPLACEMENT;

#if CONF_WITH_EXEC_REPLACEMENT_TEST
/* Caller owns source and a private, unpublished selected root. The current
 * sole managed THREAD/execution is authenticated. Success retains both roots
 * and source identity, admits one replacement operation and defers cancellation
 * through the existing cleanup scope until abort. No mapping or user/kernel
 * continuation changes, THREAD attachment, event or runnable publication occurs.
 * A caller must abort before returning to userspace or leaving its disk-error
 * boundary; this handle is not a scheduler continuation or a launch right. */
LONG kproc_exec_replacement_prepare(struct kproc *source, struct kthread *thread,
        struct kaspace *selected, KPROC_EXEC_REPLACEMENT **empty);
/* Revalidates the exact PROCESS number, TID, execution and roots. Preparation
 * cannot mask final/group cancellation or turn a different PROCESS into exec. */
LONG kproc_exec_replacement_validate(const KPROC_EXEC_REPLACEMENT *);
/* Idempotent through the unique owning handle. May run yielding destructors;
 * ownership admission and cancellation protection end only after all releases.
 * Leaves the old user context, root, PID and public completion unchanged. */
void kproc_exec_replacement_abort(KPROC_EXEC_REPLACEMENT **);
/* Test gate only: fail after source/old-root/new-root/final-validation prefix. */
void kproc_exec_replacement_test_fail_after(UWORD stage);
#define EXEC_REPLACEMENT_PROBE 0xc9700000UL /* private Pclone test transport */
LONG kproc_exec_replacement_probe(UWORD fail_after);
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
LONG kproc_exec_image_select(struct kproc *,struct kaspace *);
BOOL kproc_exec_image_pending(const struct kproc *,const struct kaspace *,BOOL complete);
#endif
#endif
