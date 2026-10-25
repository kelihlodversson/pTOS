/* No libc: make successful entry depend on the interpreter's marker. */
        .text
        .globl _start
_start:
        movl $9, %edi
        cmpq $0x660, %r10
        jne exit
        testq $15, %rsp
        jne exit
        testq %rdx, %rdx
        jne exit
        movl $37, %edi
exit:
        movabsq $0x10000004c, %rax
        syscall
        ud2
        .section .note.GNU-stack,"",@progbits
