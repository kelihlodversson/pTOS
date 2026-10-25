/* Fresh kernel-task bootstrap, reached with IRQs disabled. */
#include "config.h"
#include "asmdefs.h"
        .text
        .globl C_SYM(native_sched_enter)
C_SYM(native_sched_enter):
        movq %rdi, %rsp
        jmp *%rsi
