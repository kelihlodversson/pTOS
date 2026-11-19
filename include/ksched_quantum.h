/* Single-CPU timer accounting, used only with local IRQ exclusion. This does
 * not grant return eligibility or clear a deferred request on an unsafe IRQ. */
#ifndef KSCHED_QUANTUM_H
#define KSCHED_QUANTUM_H
#include "portab.h"
#define KSCHED_QUANTUM_TICKS 4 /* existing 200 Hz tick: 20 ms */
typedef struct { UWORD left;BOOL pending; } KSCHED_QUANTUM;
static inline void ksched_quantum_reset(KSCHED_QUANTUM *q)
{ q->left=KSCHED_QUANTUM_TICKS;q->pending=FALSE; }
/* IRQ entry precedes the device handler's tick. Capture before the tick
 * that can expire the quantum, without charging or clearing anything here. */
static inline BOOL ksched_quantum_capture_needed(const KSCHED_QUANTUM *q)
{ return q->pending || q->left<=1; }
static inline void ksched_quantum_tick(KSCHED_QUANTUM *q,BOOL running)
{
    if (running && q->left && !--q->left) q->pending=TRUE;
}
#endif
