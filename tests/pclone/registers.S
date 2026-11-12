/* Native ring-3 register preservation through real cooperative dispatch. */
#define CAT_(a,b) a##b
#define CAT(a,b) CAT_(a,b)
#define C_SYM(name) CAT(__USER_LABEL_PREFIX__,name)
#if defined(TEST_WITH_NATIVE_SCHED) && defined(TEST_WITH_PCLONE)
#if defined(__x86_64__)
        .text
        .globl C_SYM(clone_abi_entry)
C_SYM(clone_abi_entry):
        movq %rdi,C_SYM(clone_abi_argument)(%rip)
        jmp C_SYM(clone_abi_entry_body)
#elif defined(__arm__)
        .text
        .arm
        .align 2
        .globl C_SYM(clone_initial_entry)
C_SYM(clone_initial_entry):
        /* First user instructions: no C prologue may hide entry state. */
        orr r1,r1,r2
        orr r1,r1,r3
        orr r1,r1,r4
        orr r1,r1,r5
        orr r1,r1,r6
        orr r1,r1,r7
        orr r1,r1,r8
        orr r1,r1,r9
        orr r1,r1,r10
        orr r1,r1,r11
        orr r1,r1,r12
        cmp r1,#0
        bne 9f
        tst sp,#7
        bne 9f
        cmp lr,#0
        beq 9f
        mrs r1,cpsr
        and r1,r1,#0x1f
        cmp r1,#0x10
        bne 9f
#ifdef __ARM_NEON
        vmrs r1,fpscr
        cmp r1,#0
        bne 9f
        .irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
        vmov r1,r2,d\reg
        orrs r1,r1,r2
        bne 9f
        .endr
#endif
        str sp,[r0,#4]
        mov r1,#1
        str r1,[r0]
        bx lr
9:      mov r1,#2
        str r1,[r0]
        bx lr
        .globl C_SYM(clone_registers)
C_SYM(clone_registers):
        push {r4-r11,r12,lr}
#ifdef __ARM_PCS_VFP
        vpush {d8-d15}
#endif
        sub sp,sp,#8
        str r0,[sp]
#ifdef __ARM_PCS_VFP
        vmrs r1,fpscr
        str r1,[sp,#4]
        mov r1,#0x00400000
        vmsr fpscr,r1
        vmov d0,r0,r0
        vmov d1,r0,r0
        vmov d2,r0,r0
        vmov d3,r0,r0
        vmov d4,r0,r0
        vmov d5,r0,r0
        vmov d6,r0,r0
        vmov d7,r0,r0
        vmov d8,r0,r0
        vmov d9,r0,r0
        vmov d10,r0,r0
        vmov d11,r0,r0
        vmov d12,r0,r0
        vmov d13,r0,r0
        vmov d14,r0,r0
        vmov d15,r0,r0
#ifdef __ARM_NEON
        vmov d16,r0,r0
        vmov d17,r0,r0
        vmov d18,r0,r0
        vmov d19,r0,r0
        vmov d20,r0,r0
        vmov d21,r0,r0
        vmov d22,r0,r0
        vmov d23,r0,r0
        vmov d24,r0,r0
        vmov d25,r0,r0
        vmov d26,r0,r0
        vmov d27,r0,r0
        vmov d28,r0,r0
        vmov d29,r0,r0
        vmov d30,r0,r0
        vmov d31,r0,r0
#endif
#endif
        mov r4,r0
        mov r5,r0
        mov r6,r0
        mov r7,r0
        mov r8,r0
        mov r9,r0
        mov r10,r0
        mov r11,r0
        mov r0,#255
        svc #1
        ldr r0,[sp]
        cmp r4,r0
        bne clone_registers_bad
        cmp r5,r0
        bne clone_registers_bad
        cmp r6,r0
        bne clone_registers_bad
        cmp r7,r0
        bne clone_registers_bad
        cmp r8,r0
        bne clone_registers_bad
        cmp r9,r0
        bne clone_registers_bad
        cmp r10,r0
        bne clone_registers_bad
        cmp r11,r0
        bne clone_registers_bad
#ifdef __ARM_PCS_VFP
        vmov r1,r2,d0
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d1
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d2
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d3
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d4
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d5
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d6
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d7
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d8
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d9
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d10
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d11
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d12
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d13
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d14
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d15
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
#ifdef __ARM_NEON
        vmov r1,r2,d16
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d17
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d18
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d19
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d20
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d21
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d22
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d23
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d24
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d25
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d26
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d27
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d28
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d29
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d30
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
        vmov r1,r2,d31
        cmp r1,r0
        cmpeq r2,r0
        bne clone_registers_bad
#endif
        vmrs r1,fpscr
        cmp r1,#0x00400000
        bne clone_registers_bad
#endif
        mov r0,#0
        b clone_registers_done
clone_registers_bad:
        mov r0,#1
clone_registers_done:
#ifdef __ARM_PCS_VFP
        ldr r1,[sp,#4]
        vmsr fpscr,r1
#endif
        add sp,sp,#8
#ifdef __ARM_PCS_VFP
        vpop {d8-d15}
#endif
        pop {r4-r11,r12,pc}
#elif defined(__mc68000__)
        .cpu 68040
        .text
        .globl C_SYM(clone_registers)
C_SYM(clone_registers):
#ifndef __FASTCALL__
        move.l 4(%sp),%d0
#endif
        movem.l %d2-%d7/%a2-%a6,-(%sp)
        move.l %d0,-(%sp)
        move.l %d0,%d2
        move.l %d0,%d3
        move.l %d0,%d4
        move.l %d0,%d5
        move.l %d0,%d6
        move.l %d0,%d7
        move.l %d0,%a2
        move.l %d0,%a3
        move.l %d0,%a4
        move.l %d0,%a5
        move.l %d0,%a6
        move.w #255,-(%sp)
        trap #1
        addq.l #2,%sp
        move.l (%sp)+,%d0
        cmp.l %d2,%d0
        bne clone_registers_bad
        cmp.l %d3,%d0
        bne clone_registers_bad
        cmp.l %d4,%d0
        bne clone_registers_bad
        cmp.l %d5,%d0
        bne clone_registers_bad
        cmp.l %d6,%d0
        bne clone_registers_bad
        cmp.l %d7,%d0
        bne clone_registers_bad
        cmpa.l %d0,%a2
        bne clone_registers_bad
        cmpa.l %d0,%a3
        bne clone_registers_bad
        cmpa.l %d0,%a4
        bne clone_registers_bad
        cmpa.l %d0,%a5
        bne clone_registers_bad
        cmpa.l %d0,%a6
        bne clone_registers_bad
        moveq #0,%d0
        bra clone_registers_done
clone_registers_bad:
        moveq #1,%d0
clone_registers_done:
        movem.l (%sp)+,%d2-%d7/%a2-%a6
        rts
        .globl C_SYM(clone_initial_entry)
C_SYM(clone_initial_entry):
        or.l %d1,%d0
        or.l %d2,%d0
        or.l %d3,%d0
        or.l %d4,%d0
        or.l %d5,%d0
        or.l %d6,%d0
        or.l %d7,%d0
        bne 9f
        .irp reg,1,2,3,4,5,6
        cmpa.w #0,%a\reg
        bne 9f
        .endr
#ifdef TEST_WITH_KERNEL_MFASTCALL
        cmpa.l 4(%sp),%a0
        bne 9f
#else
        move.l %a0,%d0
        bne 9f
#endif
        move.l 4(%sp),%a0
        move.l %sp,4(%a0)
        tst.l 8(%a0)
        beq 8f
        bsr C_SYM(clone_fpu_initial)
        tst.l %d0
        bne 7f
8:      moveq #1,%d0
        move.l %d0,(%a0)
        rts
9:      move.l 4(%sp),%a0
7:      moveq #2,%d0
        move.l %d0,(%a0)
        rts
        .globl C_SYM(clone_fpu_initial)
C_SYM(clone_fpu_initial):
        fmove.l %fpcr,%d0
        tst.l %d0
        bne clone_fpu_initial_bad
        fmove.l %fpsr,%d0
        tst.l %d0
        bne clone_fpu_initial_bad
        fmove.l %fpiar,%d0
        tst.l %d0
        bne clone_fpu_initial_bad
        fcmp.l #0,%fp0
        fbne clone_fpu_initial_bad
        fcmp.l #0,%fp1
        fbne clone_fpu_initial_bad
        fcmp.l #0,%fp2
        fbne clone_fpu_initial_bad
        fcmp.l #0,%fp3
        fbne clone_fpu_initial_bad
        fcmp.l #0,%fp4
        fbne clone_fpu_initial_bad
        fcmp.l #0,%fp5
        fbne clone_fpu_initial_bad
        fcmp.l #0,%fp6
        fbne clone_fpu_initial_bad
        fcmp.l #0,%fp7
        fbne clone_fpu_initial_bad
        moveq #0,%d0
        rts
clone_fpu_initial_bad:
        moveq #1,%d0
        rts
        .globl C_SYM(clone_fpu_registers)
C_SYM(clone_fpu_registers):
#ifdef __FASTCALL__
        move.l %d0,%d1
#else
        move.l 4(%sp),%d1
#endif
        fmovem.x %fp0-%fp7,-(%sp)
        fmovem.l %fpcr/%fpsr/%fpiar,-(%sp)
        move.l %d1,-(%sp)
        move.l (%sp),%d0
        fmove.l %d0,%fp0
        addq.l #1,%d0
        fmove.l %d0,%fp1
        addq.l #1,%d0
        fmove.l %d0,%fp2
        addq.l #1,%d0
        fmove.l %d0,%fp3
        addq.l #1,%d0
        fmove.l %d0,%fp4
        addq.l #1,%d0
        fmove.l %d0,%fp5
        addq.l #1,%d0
        fmove.l %d0,%fp6
        addq.l #1,%d0
        fmove.l %d0,%fp7
        moveq #16,%d0
        fmove.l %d0,%fpcr
        move.l #0x04000000,%d0
        fmove.l %d0,%fpsr
        move.l #0x12345678,%d0
        fmove.l %d0,%fpiar
        fmove.l %fpiar,%d0
        move.l %d0,C_SYM(clone_fpu_expected_iar)
        move.w #255,-(%sp)
        trap #1
        addq.l #2,%sp
        fmove.l %fpcr,%d0
        cmp.l #16,%d0
        bne clone_fpu_bad_cr
        fmove.l %fpsr,%d0
        cmp.l #0x04000000,%d0
        bne clone_fpu_bad_sr
        fmove.l %fpiar,%d0
        move.l %d0,C_SYM(clone_fpu_observed)
        cmp.l C_SYM(clone_fpu_expected_iar),%d0
        bne clone_fpu_bad_iar
        move.l (%sp),%d0
        fcmp.l %d0,%fp0
        fbne clone_fpu_bad
        move.l (%sp),%d0
        addq.l #1,%d0
        fcmp.l %d0,%fp1
        fbne clone_fpu_bad
        move.l (%sp),%d0
        addq.l #2,%d0
        fcmp.l %d0,%fp2
        fbne clone_fpu_bad
        move.l (%sp),%d0
        addq.l #3,%d0
        fcmp.l %d0,%fp3
        fbne clone_fpu_bad
        move.l (%sp),%d0
        addq.l #4,%d0
        fcmp.l %d0,%fp4
        fbne clone_fpu_bad
        move.l (%sp),%d0
        addq.l #5,%d0
        fcmp.l %d0,%fp5
        fbne clone_fpu_bad
        move.l (%sp),%d0
        addq.l #6,%d0
        fcmp.l %d0,%fp6
        fbne clone_fpu_bad
        move.l (%sp),%d0
        addq.l #7,%d0
        fcmp.l %d0,%fp7
        fbne clone_fpu_bad
        moveq #0,%d0
        bra clone_fpu_done
clone_fpu_bad_cr:
        moveq #1,%d0
        bra clone_fpu_done
clone_fpu_bad_sr:
        moveq #2,%d0
        bra clone_fpu_done
clone_fpu_bad_iar:
        moveq #3,%d0
        bra clone_fpu_done
clone_fpu_bad:
        moveq #4,%d0
clone_fpu_done:
        addq.l #4,%sp
        fmovem.l (%sp)+,%fpcr/%fpsr/%fpiar
        fmovem.x (%sp)+,%fp0-%fp7
        rts
#endif
#endif
        .section .note.GNU-stack,"",%progbits
