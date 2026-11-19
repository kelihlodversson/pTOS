/* Interpreter entry: relocate ourselves before any pointer is used, link the
 * program, and jump to its entry with the finalizer in d1 as the m68k ELF ABI
 * has it (#694). */
        .text
        .globl _start
        .hidden _start
        .type _start,@function
_start:
        move.l  %sp,%a2                 | the initial stack of the process
        move.l  %a2,-(%sp)
        bsr.l   dl_bootstrap
        addq.l  #4,%sp
        tst.l   %d0
        jne     failed
        move.l  %a2,-(%sp)
        bsr.l   dl_runtime
        addq.l  #4,%sp
        tst.l   %d0
        jeq     failed
        move.l  %d0,%a0                 | the entry of the program
        move.l  %a2,%sp
        move.l  dl_self_pointer(%pc),%d1  | the finalizer, through a word the bootstrap relocated
        moveq   #0,%d0
        sub.l   %a2,%a2
        jmp     (%a0)
failed:
        move.w  #127,-(%sp)             | Pterm(127)
        move.w  #0x4c,-(%sp)
        trap    #1
        bra.s   .
        .size _start,.-_start

        .section .note.GNU-stack,"",@progbits
        .section .data.rel.ro,"aw",@progbits
        .balign 4
        .globl dl_self_pointer64
        .hidden dl_self_pointer64
dl_self_pointer64:                      | a 64-bit value, as the common code compares it
        .long   0
dl_self_pointer:
        .long   dl_runtime_fini
