/*
 * x32crt.S - entry point of the x32 (ring 3) EmuCON
 *
 * The process-entry contract (doc/process-entry.txt) gives this program
 * RDI = basepage, RSI = entry type (ENTRY_PROGRAM = 0) and an RSP for which
 * RSP + 8 is divisible by 16: the stack of a function that has just been
 * called.  So this does not `call`; it tail-jumps, leaving RSP as it is, to
 * a C function whose first two arguments are exactly those registers.
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
        .text
        .globl  _start
        .type   _start, @function
_start:
        xorl    %ebp, %ebp              /* end of the frame chain */
        jmp     x32_start
        .size   _start, . - _start
        .section .note.GNU-stack,"",@progbits
