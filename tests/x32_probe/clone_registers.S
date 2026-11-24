/* Real GP/x87/SSE preservation across cooperating sibling syscalls. */
    .text
    .globl x32_thread_registers
    .type x32_thread_registers,@function
x32_thread_registers:
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $544,%rsp
    fxsave (%rsp)
    movl %edi,%ebx
    movl %edi,%r12d
    movl %edi,%r13d
    notl %r13d
    movl %edi,%r14d
    addl $37,%r14d
    movl $4,%r15d
    fninit
    movd %ebx,%xmm0
    movl %ebx,512(%rsp)
    fildl 512(%rsp)
1:
    movabsq $0x1000000ff,%rax
    syscall
    cmpl %ebx,%r12d
    jne 2f
    movl %r13d,%eax
    notl %eax
    cmpl %ebx,%eax
    jne 2f
    leal -37(%r14),%eax
    cmpl %ebx,%eax
    jne 2f
    movd %xmm0,%eax
    cmpl %ebx,%eax
    jne 2f
    fistl 512(%rsp)
    cmpl %ebx,512(%rsp)
    jne 2f
    decl %r15d
    jnz 1b
    xorl %eax,%eax
    jmp 3f
2:
    movl $1,%eax
3:
    fxrstor (%rsp)
    addq $544,%rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    ret
    .size x32_thread_registers,.-x32_thread_registers
    .section .note.GNU-stack,"",@progbits
