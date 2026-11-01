/* Minimal standard m68k CRT: call app_main, invoke the supplied finalizer twice
 * to test idempotence, check reverse destructor order, then Pterm (#694). */
        .text
        .globl _start
        .type _start,@function
_start:
        move.l  %d1,%d2                 | the finalizer (d2-d4 survive the calls)
        move.l  (%sp),%d0               | argc
        lea     4(%sp),%a0              | argv
        move.l  %d0,%d3
        lsl.l   #2,%d3
        lea     4(%a0,%d3.l),%a1        | envp: after argv's terminator
        move.l  %d2,-(%sp)              | app_main (argc, argv, envp, finalizer)
        move.l  %a1,-(%sp)
        move.l  %a0,-(%sp)
        move.l  %d0,-(%sp)
        bsr.l   app_main
        lea     16(%sp),%sp
        move.l  %d0,%d4
        tst.l   %d2
        jeq     failed
        move.l  %d2,%a0                 | (a0 is a scratch register of the C ABI: load it again)
        jsr     (%a0)
        move.l  %d2,%a0
        jsr     (%a0)
        bsr.l   app_after_fini
        tst.l   %d0
        jne     exit
        move.l  %d4,%d0
        jra     exit
failed:
        moveq   #93,%d0
exit:
        move.w  %d0,-(%sp)              | Pterm(code)
        move.w  #0x4c,-(%sp)
        trap    #1
        bra.s   .
        .size _start,.-_start
        .section .note.GNU-stack,"",@progbits
