/* Shared-VM continuation proof. Child never returns through a C frame. */
#define CAT_(a,b) a##b
#define CAT(a,b) CAT_(a,b)
#define C_SYM(name) CAT(__USER_LABEL_PREFIX__,name)
#if defined(TEST_WITH_FORK_RETURN_TEST)
        .text
#if defined(__x86_64__)
        .globl C_SYM(fork_return_native)
C_SYM(fork_return_native):
        pushq %rbx
        pushq %rbp
        pushq %r12
        pushq %r13
        pushq %r14
        pushq %r15
        subq $520,%rsp
        fxsave (%rsp)
        movq %rsp,%r12
        movl %esi,%r13d
        leal -256(%edi),%esp
        movl $0x12345678,%ebx
        movl $0x23456789,%ebp
        movl $0x3456789a,%r8d
        movl $0x456789ab,%r9d
        pcmpeqd %xmm0,%xmm0
        movdqa %xmm0,%xmm15
        fld1
        movl $0x3f80,-16(%rsp)
        ldmxcsr -16(%rsp)
        movw $0x077f,-12(%rsp)
        fldcw -12(%rsp)
        movl $0x895f0000,%esi
        orl %r13d,%esi
        xorl %edi,%edi
        movabsq $0x100000190,%rax
        stc
        syscall
        movq %rax,%r14
        pushfq
        popq %rdx
        testl $1,%edx
        jz .Lx_bad
        cmpl $0x12345678,%ebx
        jne .Lx_bad
        cmpl $0x23456789,%ebp
        jne .Lx_bad
        cmpl $0x3456789a,%r8d
        jne .Lx_bad
        cmpl $0x456789ab,%r9d
        jne .Lx_bad
        movq %rax,%r14
        pmovmskb %xmm0,%eax
        cmpl $65535,%eax
        jne .Lx_bad
        pmovmskb %xmm15,%eax
        cmpl $65535,%eax
        jne .Lx_bad
        stmxcsr -16(%rsp)
        cmpl $0x3f80,-16(%rsp)
        jne .Lx_bad
        fnstcw -12(%rsp)
        cmpw $0x077f,-12(%rsp)
        jne .Lx_bad
        fstpl -8(%rsp)
        movabsq $0x3ff0000000000000,%rax
        cmpq %rax,-8(%rsp)
        jne .Lx_bad
        movq %r14,%rax
        testl %eax,%eax
        jnz .Lx_parent
#if defined(TEST_WITH_PRIVATE_CLONE)
        testl $0x100,%r13d
        jnz .Lx_private
#endif
        movl %esp,C_SYM(fork_return_child_sp)(%rip)
        movabsq $0x10000010b,%rax
        syscall
        movl %eax,C_SYM(fork_return_child_pid)(%rip)
        movabsq $0x100000190,%rax
        xorl %edi,%edi
        movl $0x895ffffe,%esi
        syscall
        movl %eax,C_SYM(fork_return_child_tid)(%rip)
        movl $73,%edi
        jmp .Lx_exit
#if defined(TEST_WITH_PRIVATE_CLONE)
.Lx_private:
        movabsq $0x100000190,%rax
        xorl %edi,%edi
        movl $0x895ffffd,%esi
        syscall
        cmpl C_SYM(fork_private_root)(%rip),%eax
        je .Lx_private_bad
        movabsq $0x100000190,%rax
        movl $0x895ffffe,%esi
        syscall
        cmpl C_SYM(fork_private_tid)(%rip),%eax
        je .Lx_private_bad
        movabsq $0x10000010b,%rax
        syscall
        cmpl C_SYM(fork_private_pid)(%rip),%eax
        je .Lx_private_bad
        cmpl $0x11223344,C_SYM(fork_private_cell)(%rip)
        jne .Lx_private_bad
        movl C_SYM(fork_private_heap)(%rip),%edx
        cmpb $0x31,17(%rdx)
        jne .Lx_private_bad
        cmpb $0x42,4127(%rdx)
        jne .Lx_private_bad
        cmpb $0,8237(%rdx)
        jne .Lx_private_bad
        movl $0x66778899,C_SYM(fork_private_cell)(%rip)
        movb $0x71,17(%rdx)
        movb $0x81,4127(%rdx)
        movb $0x91,8237(%rdx)
        movl $73,%edi
        jmp .Lx_exit
.Lx_private_bad:
        movl $74,%edi
        jmp .Lx_exit
#endif
.Lx_bad:
        movl $1,C_SYM(fork_return_bad)(%rip)
        testl %r14d,%r14d
        jnz .Lx_parent
        movl $74,%edi
.Lx_exit:
        movabsq $0x10000004c,%rax
        syscall
        ud2
.Lx_parent:
        movq %r14,%rax
        movl %esp,C_SYM(fork_return_parent_sp)(%rip)
        movq %r12,%rsp
        fxrstor (%rsp)
        addq $520,%rsp
        popq %r15
        popq %r14
        popq %r13
        popq %r12
        popq %rbp
        popq %rbx
        ret
#elif defined(__arm__)
        .arm
        .align 2
        .globl C_SYM(fork_return_native)
C_SYM(fork_return_native):
        push {r4-r11,r12,lr}
        vpush {d8-d15}
        vmrs r2,fpscr
        push {r2,r3}
        mov r4,sp
        sub sp,r0,#256
        mov r5,r1
        mov r6,#0x66
        mov r7,#0x77
        mov r8,#0x88
        mov r9,#0x99
        mov r10,#0xaa
        mov r11,#0xbb
        mov r12,#0xcc
        mov r2,#0x00400000
        vmsr fpscr,r2
        vmov d0,r6,r7
        vmov d31,r8,r9
        ldr r2,=0x895f0000
        orr r2,r2,r5
        mov r1,#0
        mov r0,#0x190
        cmp r0,#0 /* C=1, Z=0 */
        svc 1
        mrs r3,cpsr
        tst r3,#0x20000000
        beq .La_bad
        cmp r6,#0x66
        cmpeq r7,#0x77
        cmpeq r8,#0x88
        cmpeq r9,#0x99
        cmpeq r10,#0xaa
        cmpeq r11,#0xbb
        cmpeq r12,#0xcc
        bne .La_bad
        vmrs r2,fpscr
        cmp r2,#0x00400000
        bne .La_bad
        vmov r2,r3,d0
        cmp r2,r6
        cmpeq r3,r7
        bne .La_bad
        vmov r2,r3,d31
        cmp r2,r8
        cmpeq r3,r9
        bne .La_bad
        cmp r0,#0
        bne .La_parent
#if defined(TEST_WITH_PRIVATE_CLONE)
        tst r5,#0x100
        bne .La_private
#endif
        ldr r2,=C_SYM(fork_return_child_sp)
        str sp,[r2]
        mov r0,#0x10b
        svc 1
        ldr r2,=C_SYM(fork_return_child_pid)
        str r0,[r2]
        mov r0,#0x190
        mov r1,#0
        ldr r2,=0x895ffffe
        svc 1
        ldr r2,=C_SYM(fork_return_child_tid)
        str r0,[r2]
        mov r1,#73
        b .La_exit
#if defined(TEST_WITH_PRIVATE_CLONE)
.La_private:
        mov r0,#0x190
        mov r1,#0
        ldr r2,=0x895ffffd
        svc 1
        ldr r3,=C_SYM(fork_private_root)
        ldr r3,[r3]
        cmp r0,r3
        beq .La_private_bad
        mov r0,#0x190
        ldr r2,=0x895ffffe
        svc 1
        ldr r3,=C_SYM(fork_private_tid)
        ldr r3,[r3]
        cmp r0,r3
        beq .La_private_bad
        mov r0,#0x10b
        svc 1
        ldr r3,=C_SYM(fork_private_pid)
        ldr r3,[r3]
        cmp r0,r3
        beq .La_private_bad
        ldr r3,=C_SYM(fork_private_cell)
        ldr r0,[r3]
        ldr r2,=0x11223344
        cmp r0,r2
        bne .La_private_bad
        ldr r2,=C_SYM(fork_private_heap)
        ldr r2,[r2]
        ldrb r0,[r2,#17]
        cmp r0,#0x31
        bne .La_private_bad
        ldr r6,=4127
        ldrb r0,[r2,r6]
        cmp r0,#0x42
        bne .La_private_bad
        ldr r7,=8237
        ldrb r0,[r2,r7]
        cmp r0,#0
        bne .La_private_bad
        ldr r0,=0x66778899
        str r0,[r3]
        mov r0,#0x71
        strb r0,[r2,#17]
        mov r0,#0x81
        strb r0,[r2,r6]
        mov r0,#0x91
        strb r0,[r2,r7]
        mov r1,#73
        b .La_exit
.La_private_bad:
        mov r1,#74
        b .La_exit
#endif
.La_bad:
        ldr r2,=C_SYM(fork_return_bad)
        mov r3,#1
        str r3,[r2]
        cmp r0,#0
        bne .La_parent
        mov r1,#74
.La_exit:
        mov r0,#0x4c
        svc 1
        udf #0
.La_parent:
        ldr r2,=C_SYM(fork_return_parent_sp)
        str sp,[r2]
        mov sp,r4
        pop {r2,r3}
        vmsr fpscr,r2
        vpop {d8-d15}
        pop {r4-r11,r12,pc}
#else
        .globl C_SYM(fork_return_native)
C_SYM(fork_return_native):
        movem.l %d2-%d7/%a2-%a6,-(%sp)
        move.l 48(%sp),%a0
        move.l 52(%sp),%d1
        tst.l C_SYM(fork_return_use_fpu)
        jeq .Lm_no_save
        fmovem.x %fp0-%fp7,-(%sp)
        fmovem.l %fpcr/%fpsr/%fpiar,-(%sp)
.Lm_no_save:
        move.l %sp,%a4
        lea -256(%a0),%sp
        move.l #0x11223344,%a2
        move.l #0x22334455,%a3
        move.l #0x33445566,%a5
        move.l #0x44556677,%a6
        move.l #0x12345678,%d2
        move.l #0x23456789,%d3
        move.l #0x3456789a,%d4
        move.l #0x456789ab,%d5
        move.l #0x56789abc,%d6
        move.l #0x6789abcd,%d7
        tst.l C_SYM(fork_return_use_fpu)
        jeq .Lm_no_seed
        fmove.l #1,%fp0
        fmove.l #7,%fp7
        clr.l -(%sp)
        fmove.l (%sp)+,%fpcr
        move.l #0x12345678,-(%sp)
        fmove.l (%sp)+,%fpiar
        fmove.l %fpiar,C_SYM(fork_return_expected_iar)
        fmove.l %fpsr,C_SYM(fork_return_expected_fpsr)
.Lm_no_seed:
        or.l #0x895f0000,%d1
        move.l %d1,-(%sp)
        clr.l -(%sp)
        move.w #0x190,-(%sp)
        ori #1,%ccr
        trap #1
        move.w %ccr,-(%sp)
        btst #0,1(%sp)
        jeq .Lm_bad_ccr
        addq.l #2,%sp
        cmp.l 6(%sp),%d1
        jne .Lm_bad
        cmpa.l #0x11223344,%a2
        jne .Lm_bad
        cmpa.l #0x22334455,%a3
        jne .Lm_bad
        cmpa.l #0x33445566,%a5
        jne .Lm_bad
        cmpa.l #0x44556677,%a6
        jne .Lm_bad
        cmp.l #0x12345678,%d2
        jne .Lm_bad
        cmp.l #0x23456789,%d3
        jne .Lm_bad
        cmp.l #0x3456789a,%d4
        jne .Lm_bad
        cmp.l #0x456789ab,%d5
        jne .Lm_bad
        cmp.l #0x56789abc,%d6
        jne .Lm_bad
        cmp.l #0x6789abcd,%d7
        jne .Lm_bad
        tst.l C_SYM(fork_return_use_fpu)
        jeq .Lm_no_check
        fmove.l %fpiar,%d1
        cmp.l C_SYM(fork_return_expected_iar),%d1
        jne .Lm_bad
        fmove.l %fpsr,%d1
        cmp.l C_SYM(fork_return_expected_fpsr),%d1
        jne .Lm_bad
        fmove.l %fpcr,%d1
        tst.l %d1
        jne .Lm_bad
        fmove.l %fp0,%d1
        cmp.l #1,%d1
        jne .Lm_bad
        fmove.l %fp7,%d1
        cmp.l #7,%d1
        jne .Lm_bad
.Lm_no_check:
        tst.l %d0
        jne .Lm_parent
#if defined(TEST_WITH_PRIVATE_CLONE)
        move.l 6(%sp),%d1
        btst #8,%d1
        jne .Lm_private
#endif
        move.l %sp,C_SYM(fork_return_child_sp)
        move.w #0x10b,-(%sp)
        trap #1
        addq.l #2,%sp
        move.l %d0,C_SYM(fork_return_child_pid)
        move.l #0x895ffffe,-(%sp)
        clr.l -(%sp)
        move.w #0x190,-(%sp)
        trap #1
        lea 10(%sp),%sp
        move.l %d0,C_SYM(fork_return_child_tid)
        move.w #73,-(%sp)
        bra .Lm_exit
#if defined(TEST_WITH_PRIVATE_CLONE)
.Lm_private:
        move.l #0x895ffffd,-(%sp)
        clr.l -(%sp)
        move.w #0x190,-(%sp)
        trap #1
        lea 10(%sp),%sp
        cmp.l C_SYM(fork_private_root),%d0
        jeq .Lm_private_bad
        move.l #0x895ffffe,-(%sp)
        clr.l -(%sp)
        move.w #0x190,-(%sp)
        trap #1
        lea 10(%sp),%sp
        cmp.l C_SYM(fork_private_tid),%d0
        jeq .Lm_private_bad
        move.w #0x10b,-(%sp)
        trap #1
        addq.l #2,%sp
        cmp.l C_SYM(fork_private_pid),%d0
        jeq .Lm_private_bad
        cmpi.l #0x11223344,C_SYM(fork_private_cell)
        jne .Lm_private_bad
        move.l C_SYM(fork_private_heap),%a0
        cmpi.b #0x31,17(%a0)
        jne .Lm_private_bad
        cmpi.b #0x42,4127(%a0)
        jne .Lm_private_bad
        cmpi.b #0,8237(%a0)
        jne .Lm_private_bad
        move.l #0x66778899,C_SYM(fork_private_cell)
        move.b #0x71,17(%a0)
        move.b #0x81,4127(%a0)
        move.b #0x91,8237(%a0)
        move.w #73,-(%sp)
        bra .Lm_exit
.Lm_private_bad:
        move.w #74,-(%sp)
        bra .Lm_exit
#endif
.Lm_bad_ccr:
        addq.l #2,%sp
.Lm_bad:
        move.l #1,C_SYM(fork_return_bad)
        tst.l %d0
        jne .Lm_parent
        move.w #74,-(%sp)
.Lm_exit:
        move.w #0x4c,-(%sp)
        trap #1
        illegal
.Lm_parent:
        move.l %sp,C_SYM(fork_return_parent_sp)
        move.l %a4,%sp
        tst.l C_SYM(fork_return_use_fpu)
        jeq .Lm_no_restore
        fmovem.l (%sp)+,%fpcr/%fpsr/%fpiar
        fmovem.x (%sp)+,%fp0-%fp7
.Lm_no_restore:
        movem.l (%sp)+,%d2-%d7/%a2-%a6
        rts
#endif
#endif
        .section .note.GNU-stack,"",%progbits
