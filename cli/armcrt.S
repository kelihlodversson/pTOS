/*
 * Entry for static ARM EmuCON with native vector launch enabled.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
        .syntax unified
        .arm
        .text
        .align 2
        .globl _start
        .type _start, %function
_start:
        /* Legacy boot supplies the basepage. Static ELF supplies a null
         * finalizer in r0 and its argc/argv/envp/auxv block on the stack. */
        cmp r0, #0
        beq 1f
        ldr r2, =_base
        str r0, [r2]
        cmp r1, #1                      @ ARM_ENTRY_ACCESSORY
        beq _acc_main
        b _crtinit_noargs
1:
        mov r1, r0
        mov r0, sp
        b __libc_start_elf
        .size _start, . - _start
        .section .note.GNU-stack,"",%progbits
