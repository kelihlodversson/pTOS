/* Test-only x32 Pexec transport: path, tail, environment -> child exit code. */
        .text
        .globl client_pexec
        .type client_pexec,@function
client_pexec:
        movq %rdx, %r10
        movq %rsi, %rdx
        movq %rdi, %rsi
        xorl %edi, %edi
        movabsq $0x10000004b, %rax
        syscall
        ret
        .size client_pexec,.-client_pexec
        .section .note.GNU-stack,"",@progbits
