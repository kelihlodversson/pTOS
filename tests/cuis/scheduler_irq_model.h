/* Hosted ownership tests do not execute privileged local IRQ instructions.
 * Keep the production CPU/root helpers and model only IRQ exclusion. */
#define native_sched_irq_save scheduler_privileged_irq_save
#define native_sched_irq_restore scheduler_privileged_irq_restore
#define native_sched_irq_disable scheduler_privileged_irq_disable
#include "../../bdos/native_sched_backend.h"
#undef native_sched_irq_save
#undef native_sched_irq_restore
#undef native_sched_irq_disable
static uintptr_t scheduler_model_flags=0x200;
static inline uintptr_t native_sched_irq_save(void)
{ uintptr_t old=scheduler_model_flags; scheduler_model_flags=0; return old; }
static inline void native_sched_irq_restore(uintptr_t flags) { scheduler_model_flags=flags; }
static inline void native_sched_irq_disable(void) { scheduler_model_flags=0; }
