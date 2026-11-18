/* Completion observation protocol, independent of CPU and GUI policy. */
#ifndef KSCHED_SEQUENCE_H
#define KSCHED_SEQUENCE_H
#include "portab.h"
#include "biosext.h" /* owns panic() and its noreturn/format declaration */
#include "kowner.h"

/* The owner retains this storage through observation/admission. The rank-40
 * publication domain serializes every counter access, including 64-bit loads
 * on 32-bit CPUs, and the final compare-to-BLOCKED transaction. Payload state
 * still needs its rank-10/20/30 owner. This is a single-CPU backend, not an SMP
 * atomic primitive. State is level-triggered; the sequence is not a payload. */
typedef struct ksched_wait_sequence { UQUAD value; } KSCHED_WAIT_SEQUENCE;
typedef struct ksched_wait_observation {
    const KSCHED_WAIT_SEQUENCE *source;
    UQUAD value;
} KSCHED_WAIT_OBSERVATION;

static inline void ksched_sequence_advance(KSCHED_WAIT_SEQUENCE *sequence)
{
    KOWNER_GUARD guard; kowner_enter(&guard,KOWNER_RUNQUEUE);
    if (sequence->value == ~0ULL) panic("scheduler: wait sequence exhausted");
    sequence->value++;
    kowner_leave(&guard);
}
static inline void ksched_sequence_observe(const KSCHED_WAIT_SEQUENCE *sequence,
                                         KSCHED_WAIT_OBSERVATION *observation)
{
    KOWNER_GUARD guard; kowner_enter(&guard,KOWNER_RUNQUEUE);
    observation->source = sequence;
    observation->value = sequence->value;
    kowner_leave(&guard);
}
static inline BOOL ksched_sequence_changed(const KSCHED_WAIT_OBSERVATION *observation)
{
    BOOL changed;
    KOWNER_GUARD guard; kowner_enter(&guard,KOWNER_RUNQUEUE);
    changed=observation && observation->source->value != observation->value;
    kowner_leave(&guard);
    return changed;
}
#endif
