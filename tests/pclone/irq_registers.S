/* All-register ring-3 timer IRQ pattern, no syscall/yield during observation.
 * Does not demonstrate scheduling/preemption: a bounded busy loop only. */
#if defined(__x86_64__)
        .text
        .globl clone_irq_registers
        .type clone_irq_registers,@function
clone_irq_registers:
        push %rbx
        push %rbp
        push %r12
        push %r13
        push %r14
        push %r15
        sub $1032,%rsp
        fxsave (%rsp)
        fninit
        fld1
        ldmxcsr irq_mxcsr(%rip)
        movdqa irq_xmm(%rip),%xmm0
        movdqa %xmm0,%xmm1
        movdqa %xmm0,%xmm2
        movdqa %xmm0,%xmm3
        movdqa %xmm0,%xmm4
        movdqa %xmm0,%xmm5
        movdqa %xmm0,%xmm6
        movdqa %xmm0,%xmm7
        movdqa %xmm0,%xmm8
        movdqa %xmm0,%xmm9
        movdqa %xmm0,%xmm10
        movdqa %xmm0,%xmm11
        movdqa %xmm0,%xmm12
        movdqa %xmm0,%xmm13
        movdqa %xmm0,%xmm14
        movdqa %xmm0,%xmm15
        movabs $0x7130000000713001,%rax
        movabs $0x7130000000713002,%rbx
        movabs $0x7130000000713004,%rdx
        movabs $0x7130000000713005,%rsi
        movabs $0x7130000000713006,%rdi
        movabs $0x7130000000713007,%rbp
        movabs $0x7130000000713008,%r8
        movabs $0x7130000000713009,%r9
        movabs $0x7130000000713010,%r10
        movabs $0x7130000000713011,%r11
        movabs $0x7130000000713012,%r12
        movabs $0x7130000000713013,%r13
        movabs $0x7130000000713014,%r14
        mov $2000000,%ecx
        std /* IRQ entry must clear DF for C, then restore the original flags */
        movabs $0x7130000000713015,%r15
1:      pause
        loop 1b
        xor %r15d,%r15d /* invalidate test marker before inspecting registers */
        cmp irq_expected_rax(%rip),%rax
        jne 2f
        cmp irq_expected_rbx(%rip),%rbx
        jne 2f
        cmp irq_expected_rdx(%rip),%rdx
        jne 2f
        cmp irq_expected_rsi(%rip),%rsi
        jne 2f
        cmp irq_expected_rdi(%rip),%rdi
        jne 2f
        cmp irq_expected_rbp(%rip),%rbp
        jne 2f
        cmp irq_expected_r8(%rip),%r8
        jne 2f
        cmp irq_expected_r9(%rip),%r9
        jne 2f
        cmp irq_expected_r10(%rip),%r10
        jne 2f
        cmp irq_expected_r11(%rip),%r11
        jne 2f
        cmp irq_expected_r12(%rip),%r12
        jne 2f
        cmp irq_expected_r13(%rip),%r13
        jne 2f
        cmp irq_expected_r14(%rip),%r14
        jne 2f
        test %rcx,%rcx
        jne 2f
        pushfq
        pop %rdx
        test $0x400,%edx
        jz 2f
        cld
        fxsave 512(%rsp)
        cmpw $0x37f,512(%rsp)
        jne 2f
        cmpb $0x80,516(%rsp)
        jne 2f
        movabs $0x8000000000000000,%rdx
        cmp %rdx,544(%rsp)
        jne 2f
        cmpw $0x3fff,552(%rsp)
        jne 2f
        cmpl $0x1f80,536(%rsp)
        jne 2f
        lea 672(%rsp),%rdi
        movabs $0x7130713071307130,%rdx
        mov $32,%ecx
4:      cmp %rdx,(%rdi)
        jne 2f
        add $8,%rdi
        loop 4b
        xor %eax,%eax
        jmp 3f
2:      cld
        mov $1,%eax
3:      fxrstor (%rsp)
        add $1032,%rsp
        pop %r15
        pop %r14
        pop %r13
        pop %r12
        pop %rbp
        pop %rbx
        ret
        .section .rodata
        .balign 16
irq_xmm: .quad 0x7130713071307130,0x7130713071307130
irq_expected_rax: .quad 0x7130000000713001
irq_expected_rbx: .quad 0x7130000000713002
irq_expected_rdx: .quad 0x7130000000713004
irq_expected_rsi: .quad 0x7130000000713005
irq_expected_rdi: .quad 0x7130000000713006
irq_expected_rbp: .quad 0x7130000000713007
irq_expected_r8: .quad 0x7130000000713008
irq_expected_r9: .quad 0x7130000000713009
irq_expected_r10: .quad 0x7130000000713010
irq_expected_r11: .quad 0x7130000000713011
irq_expected_r12: .quad 0x7130000000713012
irq_expected_r13: .quad 0x7130000000713013
irq_expected_r14: .quad 0x7130000000713014
irq_expected_r15: .quad 0x7130000000713015
irq_mxcsr: .long 0x1f80
#elif defined(__arm__) && defined(__ARM_NEON)
        .text
        .arm
        .balign 4
        .globl clone_irq_registers
clone_irq_registers:
        push {r4-r12,lr}
        vpush {d0-d15}
        vpush {d16-d31}
        vmrs r0,fpscr
        sub sp,sp,#8
        str r0,[sp]
        mov r0,#0x00400000
        vmsr fpscr,r0
        ldr r0,=0x71307130
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
        mov r2,r0
        mov r3,r0
        mov r4,r0
        mov r5,r0
        mov r6,r0
        mov r7,r0
        mov r8,r0
        mov r9,r0
        mov r10,r0
        mov r11,r0
        ldr lr,=0x7130713e
        ldr r1,=4000000
        ldr r12,=0x71307132
1:      subs r1,r1,#1
        bne 1b
        str r12,[sp,#4] /* preserve marker for post-return comparison */
        mov r12,#0 /* marker invalid before scratch comparisons */
        ldr r12,=0x71307130
        cmp r0,r12
        bne 2f
        cmp r2,r12
        bne 2f
        cmp r3,r12
        bne 2f
        cmp r4,r12
        bne 2f
        cmp r5,r12
        bne 2f
        cmp r6,r12
        bne 2f
        cmp r7,r12
        bne 2f
        cmp r8,r12
        bne 2f
        cmp r9,r12
        bne 2f
        cmp r10,r12
        bne 2f
        cmp r11,r12
        bne 2f
        cmp r1,#0
        bne 2f
        ldr r1,[sp,#4]
        ldr r12,=0x71307132
        cmp r1,r12
        bne 2f
        ldr r12,=0x7130713e
        cmp lr,r12
        bne 2f
        vmrs r0,fpscr
        cmp r0,#0x00400000
        bne 2f
        sub sp,sp,#256
        mov r2,sp
        vstmia r2!,{d0-d15}
        vstmia r2!,{d16-d31}
        mov r1,#64
        mov r2,sp
        ldr r3,=0x71307130
4:      ldr r0,[r2],#4
        cmp r0,r3
        bne 5f
        subs r1,r1,#1
        bne 4b
        add sp,sp,#256
        mov r0,#0
        b 3f
5:      add sp,sp,#256
2:      mov r0,#1
3:      ldr r1,[sp]
        vmsr fpscr,r1
        add sp,sp,#8
        vpop {d16-d31}
        vpop {d0-d15}
        pop {r4-r12,pc}
#elif defined(__mc68000__)
#define IRQ_CAT_(a,b) a##b
#define IRQ_CAT(a,b) IRQ_CAT_(a,b)
#define IRQ_SYM(name) IRQ_CAT(__USER_LABEL_PREFIX__,name)
        .cpu 68040
        .text
        .balign 2
        .globl IRQ_SYM(clone_irq_registers)
IRQ_SYM(clone_irq_registers):
        movem.l %d2-%d7/%a2-%a6,-(%sp)
#ifndef __FASTCALL__
        move.l 48(%sp),%d0
#endif
        lea -336(%sp),%sp
        move.l %d0,324(%sp)
        tst.l %d0
        jeq 6f
        fmovem.x %fp0-%fp7,228(%sp)
        fmovem.l %fpcr/%fpsr/%fpiar,216(%sp)
        clr.l -(%sp)
        clr.l -(%sp)
        clr.l -(%sp)
        fmovem.l (%sp)+,%fpcr/%fpsr/%fpiar
        move.l #0x71307130,%d0
        fmove.l %d0,%fp0
        fmove.l %d0,%fp1
        fmove.l %d0,%fp2
        fmove.l %d0,%fp3
        fmove.l %d0,%fp4
        fmove.l %d0,%fp5
        fmove.l %d0,%fp6
irq_fpu_last:
        fmove.l %d0,%fp7
        lea irq_fpu_last(%pc),%a6
        move.l %a6,%d0
        fmove.l %d0,%fpiar /* explicit nonzero control-state pattern */
        fmove.l %fpiar,%d0
        movea.l %d0,%a6 /* actual FPIAR: QEMU may model only zero */
        jra 8f
6:      suba.l %a6,%a6
8:      move.l #0x71307130,%d1
        move.l %d1,%d2
        move.l %d1,%d3
        move.l %d1,%d4
        move.l %d1,%d5
        move.l %d1,%d6
        movea.l %d1,%a0
        movea.l %d1,%a1
        movea.l %d1,%a2
        movea.l %d1,%a3
        movea.l %d1,%a4
        movea.l %d1,%a5
        move.l #4000000,%d0
        move.l #0x71307137,%d7
1:      subq.l #1,%d0
        jne 1b
        move.l %d7,328(%sp)
        move.l #0x71307130,%d7
        tst.l %d0
        jne 2f
        cmp.l %d1,%d7
        jne 2f
        cmp.l %d2,%d7
        jne 2f
        cmp.l %d3,%d7
        jne 2f
        cmp.l %d4,%d7
        jne 2f
        cmp.l %d5,%d7
        jne 2f
        cmp.l %d6,%d7
        jne 2f
        cmp.l %a0,%d7
        jne 2f
        cmp.l %a1,%d7
        jne 2f
        cmp.l %a2,%d7
        jne 2f
        cmp.l %a3,%d7
        jne 2f
        cmp.l %a4,%d7
        jne 2f
        cmp.l %a5,%d7
        jne 2f
        cmpi.l #0x71307137,328(%sp)
        jne 2f
        tst.l 324(%sp)
        jeq 9f
        fmove.l %fpiar,%d0
        cmp.l %a6,%d0
        jne 2f
        fmove.l %fp0,%d0
        cmp.l %d7,%d0
        jne 2f
        fmove.l %fp1,%d0
        cmp.l %d7,%d0
        jne 2f
        fmove.l %fp2,%d0
        cmp.l %d7,%d0
        jne 2f
        fmove.l %fp3,%d0
        cmp.l %d7,%d0
        jne 2f
        fmove.l %fp4,%d0
        cmp.l %d7,%d0
        jne 2f
        fmove.l %fp5,%d0
        cmp.l %d7,%d0
        jne 2f
        fmove.l %fp6,%d0
        cmp.l %d7,%d0
        jne 2f
        fmove.l %fp7,%d0
        cmp.l %d7,%d0
        jne 2f
9:      moveq #0,%d0
        jra 3f
2:      moveq #1,%d0
3:      tst.l 324(%sp)
        jeq 4f
        fmovem.x 228(%sp),%fp0-%fp7
        fmovem.l 216(%sp),%fpcr/%fpsr/%fpiar
4:      lea 336(%sp),%sp
        movem.l (%sp)+,%d2-%d7/%a2-%a6
        rts
#endif
        .section .note.GNU-stack,"",%progbits
