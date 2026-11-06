/* Fresh kernel-task bootstrap, reached with IRQs disabled. */
#include "config.h"
#include "asmdefs.h"
#include "../../../bios/arch/x86_64/gdt.h"
        .text
        .globl C_SYM(native_sched_enter)
C_SYM(native_sched_enter):
        movq %rdi, %rsp
        jmp *%rsi

        .globl C_SYM(x86_64_enter_prepared_user)
C_SYM(x86_64_enter_prepared_user):
        /* rdi = immutable kernel image; offsetof assertions live in C. */
        movq %rdi, %r15
        movq 144(%r15), %rax
        movq %rax, %cr3
        movl $0xc0000100, %ecx /* FSBASE; TLS unsupported */
        xorl %eax, %eax
        xorl %edx, %edx
        wrmsr
        movw $0x23, %ax
        movw %ax, %ds
        movw %ax, %es
        xorl %eax, %eax
        movw %ax, %fs
        movw %ax, %gs
        movl $0xc0000101, %ecx /* user GSBASE; hidden kernel GS stays intact */
        xorl %eax, %eax
        xorl %edx, %edx
        wrmsr
        pushq 160(%r15)
        pushq 128(%r15)
        pushq 136(%r15)
        pushq 152(%r15)
        pushq 120(%r15)
        fxrstor 224(%r15)
        movq 0(%r15), %rax
        movq 8(%r15), %rbx
        movq 16(%r15), %rcx
        movq 24(%r15), %rdx
        movq 32(%r15), %rsi
        movq 40(%r15), %rdi
        movq 48(%r15), %rbp
        movq 56(%r15), %r8
        movq 64(%r15), %r9
        movq 72(%r15), %r10
        movq 80(%r15), %r11
        movq 88(%r15), %r12
        movq 96(%r15), %r13
        movq 104(%r15), %r14
        movq 112(%r15), %r15
        iretq

#if CONF_WITH_X86_64_MEMTEST
        /* Position-independent user probe, copied into an owned RX page. */
        .globl C_SYM(clone_probe_begin)
        .globl C_SYM(clone_probe_end)
C_SYM(clone_probe_begin):
        pushfq
        testq %rax,%rax
        jnz .Lclone_fail
        testq %rbx,%rbx
        jnz .Lclone_fail
        testq %rcx,%rcx
        jnz .Lclone_fail
        testq %rdx,%rdx
        jnz .Lclone_fail
        testq %rsi,%rsi
        jnz .Lclone_fail
        testq %rbp,%rbp
        jnz .Lclone_fail
        testq %r8,%r8
        jnz .Lclone_fail
        testq %r9,%r9
        jnz .Lclone_fail
        testq %r10,%r10
        jnz .Lclone_fail
        testq %r11,%r11
        jnz .Lclone_fail
        testq %r12,%r12
        jnz .Lclone_fail
        testq %r13,%r13
        jnz .Lclone_fail
        testq %r14,%r14
        jnz .Lclone_fail
        testq %r15,%r15
        jnz .Lclone_fail
        popq %rax
        cmpl $0x202,%eax
        jne .Lclone_fail
        movq %rdi,%rax
        shrq $32,%rax
        jnz .Lclone_fail
        movq %rsp,%rax
        andl $15,%eax
        cmpl $8,%eax
        jne .Lclone_fail
        movw %ss,%ax
        cmpw $X86_64_USER_DATA_SEL,%ax
        jne .Lclone_fail
        movw %cs,%ax
        cmpw $X86_64_USER_CODE_SEL,%ax
        jne .Lclone_fail
        movw %fs,%ax
        testw %ax,%ax
        jnz .Lclone_fail
        movw %gs,%ax
        testw %ax,%ax
        jnz .Lclone_fail
        subq $520,%rsp
        fxsave (%rsp) /* entry RSP=8 mod16, now aligned */
        cmpw $0x037f,0(%rsp)
        jne .Lclone_fail
        cmpl $0x1f80,24(%rsp)
        jne .Lclone_fail
        cmpb $0,4(%rsp)
        jne .Lclone_fail
        xorq %rax,%rax
        movl $32,%ecx
.Lclone_initial_xmm:
        cmpq $0,160(%rsp,%rax,8)
        jne .Lclone_fail
        incq %rax
        loop .Lclone_initial_xmm
        addq $520,%rsp
        movq %rdi,%r12
        movabsq $0x10000010b,%rax /* Pgetpid */
        syscall
        cmpl 16(%r12),%eax
        jne .Lclone_fail
        movabsq $0x10000010c,%rax /* Pgetppid */
        syscall
        cmpl 8(%r12),%eax
        jne .Lclone_fail
        movl 12(%r12),%edi
        movabsq $0x100000049,%rax /* Mfree(active user stack) must be denied */
        syscall
        testq %rax,%rax
        jz .Lclone_fail
        movl $0x12345678,%ebx
        movl $0x5aa5a55a,%ebp
        movl $0x6b5c4d3e,%r13d
        movl $0x13579bdf,%r14d
        movl $0x76543210,%r15d
        subq $24,%rsp
        movl $0x3f80,(%rsp)
        ldmxcsr (%rsp)
        fld1
        pcmpeqd %xmm0,%xmm0
        movdqa %xmm0,%xmm15
        movl $3,%r10d
.Lclone_yields:
        movabsq $0x1000000ff,%rax /* Syield */
        syscall
        testq %rax,%rax
        jnz .Lclone_fail
        cmpq $0x12345678,%rbx
        jne .Lclone_fail
        cmpq $0x5aa5a55a,%rbp
        jne .Lclone_fail
        cmpq $0x6b5c4d3e,%r13
        jne .Lclone_fail
        cmpq $0x13579bdf,%r14
        jne .Lclone_fail
        cmpq $0x76543210,%r15
        jne .Lclone_fail
        stmxcsr 4(%rsp)
        cmpl $0x3f80,4(%rsp)
        jne .Lclone_fail
        pcmpeqd %xmm13,%xmm13
        movdqa %xmm0,%xmm12
        pcmpeqb %xmm13,%xmm12
        pmovmskb %xmm12,%eax
        cmpl $65535,%eax
        jne .Lclone_fail
        movdqa %xmm15,%xmm12
        pcmpeqb %xmm13,%xmm12
        pmovmskb %xmm12,%eax
        cmpl $65535,%eax
        jne .Lclone_fail
        decl %r10d
        jnz .Lclone_yields
        fstpl 8(%rsp)
        movabsq $0x3ff0000000000000,%rax
        cmpq %rax,8(%rsp)
        jne .Lclone_fail
        addq $24,%rsp
        movl $0x600dcafe,4(%r12)
        movl (%r12),%eax
        cmpl $2,%eax
        je .Lclone_fault
        testl %eax,%eax
        jnz .Lclone_pterm
        movl $0x2468,%eax
        ret
.Lclone_pterm:
        movl $0x3579,%edi
        movabsq $0x10000004c,%rax
        syscall
.Lclone_fault:
        ud2
.Lclone_fail:
        movl $0x7bad,%edi
        movabsq $0x10000004c,%rax
        syscall
        ud2
C_SYM(clone_probe_end):
#endif
