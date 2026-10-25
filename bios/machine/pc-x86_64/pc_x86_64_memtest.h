/* Kernel-only regression hooks for callbacks, exit observations and CUIS
 * input. No test syscall or user-controlled injection is exposed. */
#ifndef PC_X86_64_MEMTEST_H
#define PC_X86_64_MEMTEST_H

#include "config.h"
#include "portab.h"
#include "bdosdefs.h"

#if CONF_WITH_X86_64_MEMTEST && CONF_WITH_USER_CALLBACKS
void x86_64_memtest_callback_entry(UQUAD flags);
void x86_64_memtest_callback_lazy_oom(void);
void x86_64_memtest_callback_syscall(UQUAD call, UQUAD output);
void x86_64_memtest_callback_exit(PD *pd, UWORD rc);
#endif
#if CONF_WITH_X86_64_MEMTEST && CONF_WITH_CUIS
void x86_64_memtest_cuis_wait(void);
void x86_64_memtest_cuis_tick(void);
#endif
#endif
