/* Single-CPU IRQ wake/idle admission. Owner/IRQ exclusion is required;
 * this is not an atomic counter, SMP lock or context-switch capability. */
#ifndef KSCHED_WAKE_H
#define KSCHED_WAKE_H
#include "portab.h"
#include "biosext.h"
typedef struct {
    UQUAD sequence;
    BOOL armed;
} KSCHED_WAKE_STATE;
static inline void ksched_wake_publish(KSCHED_WAKE_STATE *state)
{
    if (state->sequence==~(UQUAD)0) panic("scheduler: IRQ wake sequence exhausted");
    state->sequence++;
    state->armed=FALSE;
}
static inline BOOL ksched_wake_arm(KSCHED_WAKE_STATE *state,UQUAD observed)
{
    if (state->armed) panic("scheduler: IRQ idle already armed");
    if (state->sequence!=observed) return FALSE;
    state->armed=TRUE;
    return TRUE;
}
static inline void ksched_wake_disarm(KSCHED_WAKE_STATE *state)
{ state->armed=FALSE; }
#endif
