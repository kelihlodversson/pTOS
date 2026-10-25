/* Minimal standard x32 CRT: call main, invoke the supplied finalizer twice
 * to test idempotence, check reverse destructor order, then Pterm. */
        .text
        .globl _start
_start:
        movq %rdx, %r12
        movl (%rsp), %edi
        leal 4(%rsp), %esi
        leal 8(%rsp,%rdi,4), %edx
        movl %r12d, %ecx
        call app_main
        movl %eax, %ebx
        testq %r12, %r12
        jz failed
        call *%r12
        call *%r12
        call app_after_fini
        testl %eax, %eax
        jnz exit_error
        movl %ebx, %edi
        jmp exit
failed:
        movl $93, %edi
        jmp exit
exit_error:
        movl %eax, %edi
exit:
        movabsq $0x10000004c, %rax
        syscall
        ud2
        .section .note.GNU-stack,"",@progbits
