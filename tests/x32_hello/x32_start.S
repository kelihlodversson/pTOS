/*
 * x32_start.S - capture the process-entry state before a C prologue
 */

        .text
        .globl  _start
        .type   _start, @function
_start:
        movq    %rsp, %rdx
        testq   %rdi, %rdi
        jz      .Lcall
        subq    $8, %rsp
.Lcall:
        call    x32_entry_probe
.Lhalt:
        jmp     .Lhalt
        .size   _start, .-_start

        .section .note.GNU-stack,"",@progbits
