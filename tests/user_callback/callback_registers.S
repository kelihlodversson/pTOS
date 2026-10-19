/* Check an actual PIT interruption preserves GP/SSE state and the red zone. */
        .text
        .globl callback_register_checks
callback_register_checks:
        pushq %r12
        movabsq $0x123456789abcdef0,%r12
        movq %r12,-8(%rsp)
        movq %r12,%xmm15
        movl $100000000,%ecx
1:      cmpl $0,(%rdi)
        jne 2f
        pause
        loop 1b
        movl $1,%eax
        jmp 3f
2:      movq %xmm15,%rax
        cmpq %r12,%rax
        jne 4f
        cmpq %r12,-8(%rsp)
        jne 4f
        xorl %eax,%eax
3:      popq %r12
        ret
4:      movl $2,%eax
        jmp 3b
        .globl callback_badreturn
callback_badreturn:
        movq $0,8(%rsp)
        ret
        .globl callback_badstack
callback_badstack:
        movl $0x30000000,%esp
5:      pause
        jmp 5b
        .globl callback_lazy_exec
callback_lazy_exec:
        movl %esi,%edx          /* Pexec tail */
        movl %edi,%esi          /* Pexec file */
        xorl %edi,%edi          /* PE_LOADGO */
        xorl %r10d,%r10d
        movabsq $0x10000004b,%rax
        movl $0x7ffff040,%esp   /* callback frame falls in the lazy page below */
        syscall
        ud2                    /* the quarantined parent must never resume */
        /* Distinct executable WORD -> void handlers for quota regressions. */
        .balign 16
        .globl callback_quota_handlers,callback_quota_handlers_end
callback_quota_handlers:
        .rept 128
        ret
        .balign 16,0x90
        .endr
callback_quota_handlers_end:
        .section .note.GNU-stack,"",@progbits
