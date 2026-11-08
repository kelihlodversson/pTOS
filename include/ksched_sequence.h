/* Completion observation protocol, independent of CPU and GUI policy. */
#ifndef KSCHED_SEQUENCE_H
#define KSCHED_SEQUENCE_H
#include "portab.h"
#include "kprint.h"

/* The owner retains this storage through observation/admission. Access under
 * owner exclusion (local IRQ exclusion on today's single CPU); this is NOT an
 * atomic/SMP primitive. A future owner lock precedes the leaf runqueue lock.
 * State is durable/level-triggered; a sequence is not an event payload. */
typedef struct { UQUAD value; } KSCHED_WAIT_SEQUENCE;
typedef struct {
    const KSCHED_WAIT_SEQUENCE *source;
    UQUAD value;
} KSCHED_WAIT_OBSERVATION;

static inline void ksched_sequence_advance(KSCHED_WAIT_SEQUENCE *sequence)
{
    if (sequence->value == ~0ULL) panic("scheduler: wait sequence exhausted");
    sequence->value++;
}
static inline void ksched_sequence_observe(const KSCHED_WAIT_SEQUENCE *sequence,
                                         KSCHED_WAIT_OBSERVATION *observation)
{
    observation->source = sequence;
    observation->value = sequence->value;
}
static inline BOOL ksched_sequence_changed(const KSCHED_WAIT_OBSERVATION *observation)
{
    return observation && observation->source->value != observation->value;
}
#endif
