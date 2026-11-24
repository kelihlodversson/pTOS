/* Compile-only: the sequence header must supply panic's real declaration,
 * independent of whether a caller includes BIOS extensions before or after it. */
#include "emutos.h"
#include "ksched_sequence.h"
#include "biosext.h"
BOOL sched_sequence_header_check(KSCHED_WAIT_SEQUENCE *sequence);
BOOL sched_sequence_header_check(KSCHED_WAIT_SEQUENCE *sequence)
{
    KSCHED_WAIT_OBSERVATION observation;
    ksched_sequence_observe(sequence,&observation);
    ksched_sequence_advance(sequence);
    return ksched_sequence_changed(&observation);
}
