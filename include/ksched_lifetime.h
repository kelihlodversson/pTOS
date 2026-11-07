/* Internal scheduler ownership, not a public PROCESS wait/reap ABI. */
#ifndef KSCHED_LIFETIME_H
#define KSCHED_LIFETIME_H
#include "portab.h"
#include "bdosdefs.h"
struct kproc;
/* An owning reference identified only by a non-recycled token. Retain before
 * copying ownership; release consumes and clears the handle. Stale IDs never
 * dereference freed storage. No caller buffer, PD or continuation is stored. */
typedef struct { UQUAD identity; } KSCHED_ADMISSION;
/* Boot transfers an ownerless low allocation to the permanent kernel service.
 * It is pinned, never exposed as a PROCESS/PID, and never freed on a live stack. */
BOOL ksched_controller_install(PD *owned);
PD *ksched_controller_pd(void); /* borrowed from permanent kernel owner */
LONG ksched_admission_create(struct kproc *owner, UWORD count, KSCHED_ADMISSION *empty);
BOOL ksched_admission_retain(const KSCHED_ADMISSION *source, KSCHED_ADMISSION *empty);
void ksched_admission_release(KSCHED_ADMISSION *);
BOOL ksched_admission_complete(const KSCHED_ADMISSION *, UWORD index, LONG status);
/* All-or-nothing kernel result delivery. Failure leaves outputs unchanged.
 * Exactly one successful take; this does not consume any public PID zombie. */
LONG ksched_admission_take(const KSCHED_ADMISSION *, LONG *results, UWORD count);
BOOL ksched_admission_abandoned(const KSCHED_ADMISSION *);
void ksched_admission_abandon(const KSCHED_ADMISSION *);
void ksched_admission_owner_exited(struct kproc *owner);
PD *ksched_admission_owner_pd(const KSCHED_ADMISSION *); /* borrowed only if live */
#endif
