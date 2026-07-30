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
 * _main -- placeholder for this task only.  Later tasks replace the
 * body: Task 2 adds a physical-mode UART "hello", Task 3 adds the MMU
 * bring-up and the jump into virtual addressing, Task 4 hands off to
 * _biosmain.
 */
.balign 4
_main:
    ldr sp, =0x47ff0000     /* literal physical scratch address, well within a 128M -m allocation */
1:  b   1b

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
