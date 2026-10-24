/* Deterministic callback failure injection and independent exit observations.
 * No test syscall or user-controlled failure injection is exposed. */
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
#endif
