/* Immutable executable policy. This value conveys no mapping/backing rights. */
#ifndef KPROC_IMAGE_POLICY_H
#define KPROC_IMAGE_POLICY_H

#include "portab.h"
#include "progimage.h"

#if CONF_WITH_IMAGE_POLICY
struct kproc;
struct kthread;
typedef struct {
    VADDR vaddr;
    ULONG memsz,flags;
} KPROC_IMAGE_SEGMENT;
typedef struct {
    KPROC_IMAGE_SEGMENT seg[PROG_MAX_SEGMENTS];
    ULONG nseg;
    VADDR entry,end;
} KPROC_IMAGE_LAYOUT;
typedef struct kproc_image_policy {
    KPROC_IMAGE_LAYOUT program,interpreter;
    VADDR entry;
    UQUAD interpreter_bytes;
    ULONG legacy_text_bytes;
    BOOL has_image,legacy,legacy_text_recorded,standard_entry;
} KPROC_IMAGE_POLICY;
typedef struct kproc_image_preparation KPROC_IMAGE_PREPARATION;

/* The caller retains an exact live runtime or pending-image KPROC. Capture is
 * allocation-free, does not inspect user PDs or PTEs, and changes output only
 * on success. No source pointer, file bytes, stack/initial SP, recipe, cleanup
 * receipt, reservation birth, THREAD, root or resource owner is copied. */
LONG kproc_image_policy_capture(const struct kproc *,KPROC_IMAGE_POLICY *output);
/* Only an empty unpublished target with its sole, prepared initial THREAD and
 * canonical mapping membership is admissible. Preparation owns a retained
 * target identity, not its resources. The target holds a borrowed marker.
 * Source policy is copied, never borrowed. *empty must initially be NULL.
 * Only the existing clone activation may install the policy, without failure.
 * No started/live execution context may be replaced through this interface. */
LONG kproc_image_policy_prepare(struct kproc *,struct kthread *,
        const KPROC_IMAGE_POLICY *,KPROC_IMAGE_PREPARATION **empty);
LONG kproc_image_policy_validate(const KPROC_IMAGE_PREPARATION *);
/* Abort/finish exactly once. Target retirement cancels the borrowed marker
 * before callbacks; release of that cancelled receipt reads identity only. */
void kproc_image_policy_release(KPROC_IMAGE_PREPARATION **);
#if CONF_WITH_IMAGE_POLICY_TEST
LONG kproc_image_policy_test_query(const struct kproc *,UWORD selector);
void kproc_image_policy_test_fail_after(UWORD stage);
#endif
#endif
#endif
