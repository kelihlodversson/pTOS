/* Legacy non-nesting VDI/AES IRQ scope, owned by the loaded execution CPU.
 * This is local interrupt state, not a PROCESS lock or SMP exclusion. */
#ifndef KSCHED_LEGACY_IRQ_H
#define KSCHED_LEGACY_IRQ_H
#include "portab.h"
uintptr_t ksched_legacy_irq_disable(void);
void ksched_legacy_irq_enable(void);
#endif
