/* Interrupt backend for the currently x32-only CUIS input path. */
#ifndef CUIS_INPUT_H
#define CUIS_INPUT_H
#include "portab.h"
UQUAD cuis_input_lock(void);
void cuis_input_unlock(UQUAD flags);
/* Called with IRQs masked; returns with IRQs masked after one interrupt. */
void cuis_input_idle(void);
ULONG cuis_input_ticks(void);
#if CONF_WITH_X86_64_MEMTEST
void cuis_input_test_wait(void);
#endif
#endif
