/* Interpreter entry: relocate ourselves before any pointer is used, link the
 * program, and jump to its entry with the finalizer in r0 (#693). */
        .arm
        .text
        .globl _start
        .hidden _start
        .type _start,%function
_start:
        mov     r4, sp                  @ the initial stack of the process
        mov     r0, sp
        bl      dl_bootstrap
        cmp     r0, #0
        bne     failed
        mov     r0, r4
        bl      dl_runtime
        cmp     r0, #0
        beq     failed
        mov     r5, r0                  @ the entry of the program
        ldr     r0, 1f                  @ the finalizer, through a word the bootstrap relocated
2:      ldr     r0, [pc, r0]
        mov     sp, r4
        mov     r1, #0
        mov     r2, #0
        mov     r3, #0
        mov     r4, #0
        bx      r5
failed:
        mov     r1, #127                @ Pterm(127)
        mov     r0, #0x4c
        svc     1
        b       .
1:      .word   dl_self_pointer - (2b + 8)
        .size _start,.-_start

        .section .note.GNU-stack,"",%progbits
        .section .data.rel.ro,"aw",%progbits
        .balign 8
        .globl dl_self_pointer64
        .hidden dl_self_pointer64
dl_self_pointer64:                      @ a 64-bit value, as the common code compares it
dl_self_pointer:
        .word   dl_runtime_fini
        .word   0
