/* Position-independent CPU-bound handshake copied into a real private file
 * mapping. No trap, external reference, literal pool or voluntary dispatch. */
#define CAT_(a,b) a##b
#define CAT(a,b) CAT_(a,b)
#define C_SYM(name) CAT(__USER_LABEL_PREFIX__,name)
        .text
        .globl C_SYM(preempt_file_loop)
        .globl C_SYM(preempt_file_loop_end)
#if defined(__x86_64__)
C_SYM(preempt_file_loop):
        mov %edi,%r10d
        mov (%r10),%ecx
        mov 8(%r10),%eax
        mov 12(%r10),%edx
        xor %r8d,%r8d
1:      mov $1000000000,%r9d
2:      cmp %ecx,(%rax)
        je 3f
        dec %r9d
        jnz 2b
        movl $1,4(%r10)
        ret
3:      incl (%rdx,%rcx,4)
        mov %ecx,%esi
        xor $1,%esi
        mov %esi,(%rax)
        inc %r8d
        cmp $16,%r8d
        jl 1b
        ret
#elif defined(__arm__)
        .arm
        .align 2
C_SYM(preempt_file_loop):
        push {r4-r8,lr}
        mov r4,r0
        ldr r5,[r4]
        ldr r6,[r4,#8]
        ldr r7,[r4,#12]
        mov r8,#0
1:      movw r12,#0xca00
        movt r12,#0x3b9a
2:      ldr r1,[r6]
        cmp r1,r5
        beq 3f
        subs r12,r12,#1
        bne 2b
        mov r0,#1
        str r0,[r4,#4]
        b 4f
3:      ldr r0,[r7,r5,lsl #2]
        add r0,r0,#1
        str r0,[r7,r5,lsl #2]
        eor r0,r5,#1
        str r0,[r6]
        add r8,r8,#1
        cmp r8,#16
        blt 1b
4:      pop {r4-r8,pc}
#elif defined(__mc68000__)
C_SYM(preempt_file_loop):
        movea.l 4(%sp),%a0
        movem.l %d2-%d4/%a2,-(%sp)
        move.l (%a0),%d2
        lsl.l #2,%d2
        movea.l 8(%a0),%a1
        movea.l 12(%a0),%a2
        moveq #0,%d3
1:      move.l #1000000000,%d4
2:      move.l (%a1),%d0
        lsl.l #2,%d0
        cmp.l %d2,%d0
        jeq 3f
        subq.l #1,%d4
        jne 2b
        moveq #1,%d0
        move.l %d0,4(%a0)
        jra 4f
3:      addq.l #1,(%a2,%d2.l)
        moveq #4,%d0
        eor.l %d2,%d0
        lsr.l #2,%d0
        move.l %d0,(%a1)
        addq.l #1,%d3
        cmpi.l #16,%d3
        jlt 1b
4:      movem.l (%sp)+,%d2-%d4/%a2
        rts
#endif
C_SYM(preempt_file_loop_end):
        .section .note.GNU-stack,"",%progbits
