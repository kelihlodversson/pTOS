/* CPU bootstrap only; all dispatch policy lives in bdos/native_sched.c. */
#include "asmdefs.h"
        .text
        .globl C_SYM(native_sched_enter)
C_SYM(native_sched_enter):
#if CONF_WITH_IRQ_RESTART && CPU_ARMV7
        clrex                       /* a new THREAD cannot inherit a USER reservation */
#endif
        mov sp, r0
        bx r1
