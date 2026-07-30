/*
 * startup.S - EmuTOS startup module for QEMU's ARM 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"
#include "header.h"

        .globl  _os_entry
        .extern _stktop
        .extern _biosmain
        .extern __endvdibss
        .extern _virt_mmu_bootstrap
        .extern _phystop

        .text

/*
 * OSHEADER -- identical in shape to bios/machine/raspi/startup.S; see
 * that file for a field-by-field description.
 */

        .globl  _main
        .globl  _os_beg
        .globl  _os_magic
        .globl  _os_date
        .globl  _os_conf
        .globl  _os_dosdate
        .globl  _root
        .globl  _shifty
        .globl  _run

_os_entry:
.p2align 5
arm_vectors:
    b   _main
    b   _arm_dispatch_undef
    b   _arm_dispatch_svc
    b   _arm_dispatch_prefetch_abort
    b   _arm_dispatch_data_abort
    b   _arm_dispatch_undef
    b   _arm_dispatch_irq
    b   _arm_dispatch_fiq

os_version:
    .word   TOS_VERSION
reseth:
    .word   _main
_os_beg:
    .word   _os_entry
os_end:
    .word   __endvdibss
os_res1:
    .word   _main
_os_magic:
#if CONF_WITH_AES
    .word   _ui_mupb
#else
    .word   0
#endif
_os_date:
    .word   OS_DATE
_os_conf:
#if CONF_MULTILANG
    .word   OS_CONF_MULTILANG
#else
    .word   (OS_COUNTRY << 1) + OS_PAL
#endif
_os_dosdate:
    .word   OS_DOSDATE
os_root:
    .word   _root
os_kbshift:
    .word   _shifty
os_run:
    .word   _run
os_dummy:
    .ascii  "CAMELTOS"

/*
 * _main -- two-phase boot: a physical-mode "hello" (proves the CPU
 * reached here and the UART works before the MMU is on), then the
 * MMU bring-up (bios/machine/virt-arm/virt_mmu.c) using only literal
 * physical constants, then a long jump into virtual addressing, then
 * a second "hello" plus a sysvars round-trip check using ordinary
 * symbol-based addressing to prove the low fixed-address TOS system
 * variables (tosvars.ld) are correctly mapped via the low MMU window.
 * Task 4 replaces the tail of this with a hand-off to _biosmain.
 */
.balign 4
_main:
    ldr sp, =0x47ff0000
    bl  _virt_uart0_init
    adr r4, hello_msg_phys     /* PC-relative: correct at the physical PC we're
                                 * actually executing at pre-MMU, unlike
                                 * "ldr r4, =symbol" which loads the linked
                                 * (virtual) address -- not valid yet. */
    bl  print_string

    /* MMU bring-up: literal physical constants only, no linked symbols */
    mov r0, #0x08000000     /* 128 MiB RAM window, matches -m 128 in the qemu invocation below */
    ldr r1, =0x47f00000     /* page table: fixed physical scratch address, top of a 128M RAM allocation */
    bl  _virt_mmu_bootstrap

    ldr pc, =virt_arm_post_mmu

virt_arm_post_mmu:
    ldr sp, =0x47ff0000     /* still the physical scratch stack; Task 4 switches to _stktop */
    ldr r4, =hello_msg_virt
    bl  print_string

    /* sysvars round-trip: _phystop is one of the fixed low sysvars
     * addresses (see tosvars.ld); if the low MMU window is wired
     * correctly this read/write pair works exactly like any other
     * global variable. */
    ldr r0, =_phystop
    ldr r1, =0x12345678
    str r1, [r0]
    ldr r2, [r0]
    cmp r1, r2
    ldreq r4, =sysvars_ok_msg
    ldrne r4, =sysvars_fail_msg
    bl  print_string

2:  b   2b

print_string:
    push {r4, lr}
1:  ldrb r0, [r4], #1
    cmp r0, #0
    beq 2f
    bl  _virt_uart0_write_byte
    b   1b
2:  pop {r4, lr}
    bx  lr

hello_msg_phys:
    .asciz "virt-arm: pre-MMU boot OK\r\n"
hello_msg_virt:
    .asciz "virt-arm: post-MMU boot OK\r\n"
sysvars_ok_msg:
    .asciz "virt-arm: sysvars alias OK\r\n"
sysvars_fail_msg:
    .asciz "virt-arm: sysvars alias FAILED\r\n"
    .align 2

    .globl _arm_dispatch_undef
_arm_dispatch_undef:
    srsfd  sp!, #0x13
    cps    #0x13
    sub    sp, #4*15
    stmia  sp, {r0-r14}^
    ldr    r3, [sp, #+4*15]
    sub    r3, r3, #4
    ldr    r2, [r3]
    ldr    r0, =0xfff000f0
    ldr    r1, =0xe7f000f0
    and    r0, r0, r2
    cmp    r0, r1
    moveq  r0, #0x28
    movne  r0, #0x10
    mov    r1, sp
    ldr    ip, [r0]
    blx ip
    add    sp, #4*15
    ldmfd  sp, {r0-r14}^
    rfefd  sp!

    .globl _arm_dispatch_svc
_arm_dispatch_svc:
    ldr ip, [lr, #-4]
    and ip, ip, #0xF
    lsl ip, ip, #0x2
    ldr ip, [ip, #+0x80]
    bx  ip

    .globl _arm_dispatch_prefetch_abort
_arm_dispatch_prefetch_abort:
    sub    lr, lr, #4
    srsfd  sp!, #0x13
    cps    #0x13
    sub    sp, #4*15
    stmia  sp, {r0-r14}^
    mrc p15, 0, r2, c5, c0,  1
    mrc p15, 0, r3, c6, c0,  2
    mov r0, #0x08
    mov r1, sp
    ldr ip, [r0]
    blx ip
    add    sp, #4*15
    ldmfd  sp, {r0-r14}^
    rfefd  sp!

    .globl _arm_dispatch_data_abort
_arm_dispatch_data_abort:
    sub lr, lr, #8
    srsfd  sp!, #0x13
    cps    #0x13
    sub    sp, #4*15
    stmia  sp, {r0-r14}^
    mrc p15, 0, r2, c6, c0,  0
    mrc p15, 0, r3, c5, c0,  0
    mov r0, #0x0c
    mov r1, sp
    ldr ip, [r0]
    blx ip
    add    sp, #4*15
    ldmfd  sp, {r0-r14}^
    rfefd  sp!

    .extern _virt_int_handler
    .globl _arm_dispatch_irq
_arm_dispatch_irq:
    sub    lr, lr, #4
    srsfd  sp!, #0x13
    cps    #0x13
    stmfd  sp!, {r0-r3, ip, lr}
    bl     _virt_int_handler
    ldmfd  sp!, {r0-r3, ip, lr}
    rfefd  sp!

    .globl _arm_dispatch_fiq
_arm_dispatch_fiq:
    sub    lr, lr, #4
    srsfd  sp!, #0x13
    cps    #0x13
    rfefd  sp!

/*
 * Placeholders below -- needed only so that code shared with every other
 * ARM machine (bios/kprint.c's panic handler, bios/mfp.c's non-MFP timer
 * setup) and this file's own _arm_dispatch_irq link successfully. Nothing
 * in this task ever reaches them: IRQs are never unmasked and _biosmain is
 * never called. Task 5 (GICv2 driver and timer tick) replaces
 * _virt_int_handler with a real implementation in virt_pic.c and must
 * delete the stub below; task 4 (hand-off to _biosmain) may need to do the
 * same for _int_timerc once virt_timer_init() exists.
 */

/* bios/kprint.c's dopanic() reads this, unconditionally, on every machine
 * built with __arm__.  This machine never enters HYP mode (see the comment
 * at the top of _main), so it is always 0. */
    .globl _start_in_hyp
_start_in_hyp: .word 0

/* bios/mfp.c's init_system_timer() installs this as vector_5ms whenever
 * CONF_WITH_MFP is off, regardless of whether a real periodic tick has
 * been wired up yet. */
    .globl _int_timerc
_int_timerc:
    bx  lr

/* _arm_dispatch_irq above calls this; see the block comment above. */
    .globl _virt_int_handler
_virt_int_handler:
    bx  lr
