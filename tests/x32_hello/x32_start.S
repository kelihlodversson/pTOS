/*
 * x32_start.S - capture the process-entry state before a C prologue
 */

        .text
        .globl  _start
        .type   _start, @function
_start:
        movq    %rsp, %rdx
        call    x32_entry_probe
.Lhalt:
        jmp     .Lhalt
        .size   _start, .-_start
