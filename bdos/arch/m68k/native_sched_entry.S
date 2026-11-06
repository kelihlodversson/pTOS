/* Fresh cooperative continuation, entered with IRQs excluded. */
#include "asmdefs.h"
        .text
        .globl C_SYM(native_sched_enter)
C_SYM(native_sched_enter):
#if CONF_WITH_MFASTCALL
        move.l d0,sp
        jmp (a0)
#else
        move.l 4(sp),d0
        move.l 8(sp),a0
        move.l d0,sp
        jmp (a0)
#endif
