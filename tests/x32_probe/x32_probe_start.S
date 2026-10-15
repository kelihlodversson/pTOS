/*
 * x32_probe_start.S - capture the process-entry state before any C code runs
 *
 * At entry RSP + 8 is divisible by 16 (doc/process-entry.txt), as if a call
 * had just pushed a return address, so one `subq $8` makes the stack right
 * for the C call below.  The arguments, in System V order: RDI = basepage and
 * RSI = entry type already, RDX = the entry RSP, RCX = the CS and R8 = the SS
 * selector the CPU reports.
 */

        .text
        .globl  _start
        .type   _start, @function
_start:
        movq    %rsp, %rdx
        movl    %cs, %ecx
        movl    %ss, %r8d
        subq    $8, %rsp
        call    x32_probe_main
.Lhalt:
        jmp     .Lhalt
        .size   _start, .-_start

        .section .note.GNU-stack,"",@progbits
