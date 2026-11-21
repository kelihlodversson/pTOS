/* Fixture-only CPU/IRQ endpoints. Tests including the actual scheduler supply
 * stronger definitions backed by its real CPU record and hosted IRQ model. */
#include "emutos.h"
#include "kowner.h"
#if CONF_WITH_NATIVE_SCHED
static KOWNER_CPU cpu;
static uintptr_t flags=0x200;
KOWNER_CPU *__attribute__((weak)) kowner_cpu(void) { return &cpu; }
uintptr_t __attribute__((weak)) kowner_irq_save(void)
{ uintptr_t saved=flags;flags=0;return saved; }
void __attribute__((weak)) kowner_irq_restore(uintptr_t saved) { flags=saved; }
#endif
