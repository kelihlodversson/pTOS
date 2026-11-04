/* Minimal standard ARM CRT: call app_main, invoke the supplied finalizer twice
 * to test idempotence, check reverse destructor order, then Pterm (#693). */
        .arm
        .text
        .globl _start
        .type _start,%function
_start:
        mov     r4, r0                  @ the finalizer
        ldr     r0, [sp]                @ argc
        add     r1, sp, #4              @ argv
        add     r2, r1, r0, lsl #2
        add     r2, r2, #4              @ envp: after argv's terminator
        mov     r3, r4
        bl      app_main
        mov     r5, r0
        cmp     r4, #0
        beq     failed
        blx     r4
        blx     r4
        bl      app_after_fini
        cmp     r0, #0
        bne     exit
        mov     r0, r5
        b       exit
failed:
        mov     r0, #93
exit:
        mov     r1, r0                  @ Pterm(code)
        mov     r0, #0x4c
        svc     1
        b       .
        .size _start,.-_start
        .section .note.GNU-stack,"",%progbits
