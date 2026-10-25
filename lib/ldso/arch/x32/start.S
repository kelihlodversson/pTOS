/* Bootstrap before any relocated data/function pointer may be used. */
        .text
        .globl _start
        .hidden _start
_start:
        movq %rsp, %r12
        movq %rsp, %rdi
        call dl_bootstrap
        testl %eax, %eax
        jnz failed
        movq %r12, %rdi
        call dl_runtime
        testl %eax, %eax
        jz failed
        movl %eax, %r11d
        movq %r12, %rsp
        leal dl_runtime_fini(%rip), %edx
        xorl %edi, %edi
        xorl %esi, %esi
        jmp *%r11
failed:
        movl $127, %edi
        movabsq $0x10000004c, %rax
        syscall
        ud2
        .section .note.GNU-stack,"",@progbits
        .section .data.rel.ro,"aw",@progbits
        .balign 8
        .globl dl_self_pointer64
        .hidden dl_self_pointer64
dl_self_pointer64:
        .quad dl_runtime_fini
