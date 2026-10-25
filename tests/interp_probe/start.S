/* A PIC interpreter with no self relocations: verify the kernel handoff,
 * then enter AT_ENTRY on the original stack with RDX=0. */
        .text
        .globl _start
        .hidden _start
_start:
        orq %rbx, %rax
        orq %rcx, %rax
        orq %rdx, %rax
        orq %rdi, %rax
        orq %rsi, %rax
        orq %rbp, %rax
        orq %r8, %rax
        orq %r9, %rax
        orq %r10, %rax
        orq %r11, %rax
        orq %r12, %rax
        orq %r13, %rax
        orq %r14, %rax
        orq %r15, %rax
        jz clean_registers
        movl $8, %eax
        jmp failed
clean_registers:
        movq %rsp, %r12
        movq %rsp, %rdi
        movq %rdx, %rsi
        subq $8, %rsp
        andq $-16, %rsp
        call interp_check
        cmpl $0x400000, %eax
        jb failed
        movl %eax, %r11d
        movq %r12, %rsp
        xorl %edi, %edi
        xorl %esi, %esi
        xorl %edx, %edx
        movl $0x660, %r10d
        jmp *%r11
failed:
        movl %eax, %edi
        movabsq $0x10000004c, %rax
        syscall
        ud2
        .section .note.GNU-stack,"",@progbits
