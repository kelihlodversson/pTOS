/* Kernel-only regression hooks for callbacks, exit observations and CUIS
 * input. No test syscall or user-controlled injection is exposed. */
#ifndef PC_X86_64_MEMTEST_H
#define PC_X86_64_MEMTEST_H

#include "config.h"
#include "portab.h"
#include "bdosdefs.h"

#if CONF_WITH_X86_64_MEMTEST
void x86_64_memtest_disposal_committed(PD *pd);
struct kproc;
BOOL x86_64_memtest_kstack_contains(UQUAD phys, UQUAD address);
UQUAD x86_64_memtest_kstack_bytes(void);
void x86_64_memtest_exit_handoff(struct kproc *process, PD *pd,
                                UQUAD stack_phys, UWORD status, UWORD stage);
void x86_64_memtest_fail_next_exit_handoff(void);
#if CONF_WITH_NATIVE_SCHED
struct kthread;
void x86_64_memtest_task_thread(struct kthread *thread, UQUAD stack_phys, UWORD stage);
#endif
#endif
#if CONF_WITH_X86_64_MEMTEST && CONF_WITH_USER_CALLBACKS
void x86_64_memtest_callback_entry(UQUAD flags);
void x86_64_memtest_callback_lazy_oom(void);
void x86_64_memtest_callback_syscall(UQUAD call, UQUAD output);
void x86_64_memtest_callback_exit(PD *pd, UWORD rc);
#endif
#if CONF_WITH_X86_64_MEMTEST && CONF_WITH_CUIS
void x86_64_memtest_cuis_wait(ULONG events);
void x86_64_memtest_cuis_tick(void);
#endif
#endif
