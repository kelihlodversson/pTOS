/* CPU bootstrap only; all dispatch policy lives in bdos/native_sched.c. */
#include "asmdefs.h"
        .text
        .globl C_SYM(native_sched_enter)
C_SYM(native_sched_enter):
        mov sp, r0
        bx r1
