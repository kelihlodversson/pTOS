/* Fixture-only CPU/IRQ endpoints. Tests including the actual scheduler supply
 * stronger definitions backed by its real CPU record and hosted IRQ model. */
#include "emutos.h"
#include "kowner.h"
#include "ksched.h"
#include "kthread.h"
#if CONF_WITH_MULTITHREADING
/* These fixtures have one executing owner unless they provide a scheduler model. */
UQUAD __attribute__((weak)) ksched_context_id(void) { return 1; }
void __attribute__((weak)) kthread_forget_completions(struct kproc *process)
{ (void)process; }
static KOWNER_CPU cpu;
static uintptr_t flags=0x200;
KOWNER_CPU *__attribute__((weak)) kowner_cpu(void) { return &cpu; }
uintptr_t __attribute__((weak)) kowner_irq_save(void)
{ uintptr_t saved=flags;flags=0;return saved; }
void __attribute__((weak)) kowner_irq_restore(uintptr_t saved) { flags=saved; }
#endif
