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
        mov r1, r0
        mov r0, sp
        b __libc_start_elf
        .size _start, . - _start
        .section .note.GNU-stack,"",%progbits
