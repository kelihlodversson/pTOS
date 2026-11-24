/* Trusted identity observation, not another completion registry or reaper. */
#ifndef PROCESS_OBSERVER_H
#define PROCESS_OBSERVER_H
#include "kproc.h"
#if CONF_WITH_USER_ASPACE
typedef KPROC PROCESS_OBSERVER;
/* The caller retains target while attaching; *out must be NULL. A live target
 * is retained before publication. Completed/retired targets cannot attach.
 * No PD, ASPACE, stack, descriptor or caller-stack ownership is transferred. */
LONG process_observer_attach(KPROC *target, PROCESS_OBSERVER **out);
/* A copied snapshot; no zombie consumption, notification, callback or allocation.
 * UNSTARTED/RUNNING are not completion. ABANDONED carries no exit status. */
BOOL process_observer_snapshot(const PROCESS_OBSERVER *observer, PROCESS_COMPLETION *out);
/* Status commitment alone is NOT ready. Ready follows resource retirement and
 * managed public-wait state publication; snapshot order is then nonzero for
 * completion. ABANDONED is ready with no status/order. IRQ-safe, no callbacks. */
BOOL process_observer_ready(void *observer);
/* Consume this handle once, outside CUIS exclusion or the runqueue leaf. */
void process_observer_release(PROCESS_OBSERVER **observer);
#endif
#endif
