/*
 * x32_probe_start.S - capture the process-entry state before any C code runs
 *
 * ELF enters on a 16-byte aligned stack with RDI clear. The kernel test
 * harness can also enter through the basepage ABI with RSP + 8 aligned.
 * Preserve the original RSP and selector values for the C probe.
 */

        .text
        .globl  _start
        .type   _start, @function
_start:
        movq    %rsp, %rdx
        movl    %cs, %ecx
        movl    %ss, %r8d
        testq   %rdi, %rdi
        jz      .Lcall
        subq    $8, %rsp
.Lcall:
        call    x32_probe_main
.Lhalt:
        jmp     .Lhalt
        .size   _start, .-_start

        .section .note.GNU-stack,"",@progbits
