/* Interrupt backend for the currently x32-only CUIS input path. */
#ifndef CUIS_INPUT_H
#define CUIS_INPUT_H
#include "portab.h"
UQUAD cuis_input_lock(void);
void cuis_input_unlock(UQUAD flags);
/* Called with IRQs masked; returns with IRQs masked after one interrupt. */
void cuis_input_idle(void);
ULONG cuis_input_ticks(void);
BOOL cuis_input_standard_pointer(void);
BOOL cuis_input_pointer_state(LONG *x, LONG *y, ULONG *buttons);
void cuis_input_clip_pointer(LONG *x, LONG *y);
void cuis_input_call_pointer(SBYTE *packet);
#if CONF_WITH_X86_64_MEMTEST
void cuis_input_test_wait(ULONG events);
#endif
#endif
