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
        .extern __bss
        .extern __ebss

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
    bne sysvars_check_failed

    ldr r4, =sysvars_ok_msg
    bl  print_string

    /*
     * The round-trip test above only proves the sysvars alias works; it
     * leaves _phystop holding the test pattern instead of a real value.
     * _phystop is not a scratch variable -- bios/biosmem.c's bmem_init()
     * reads it directly ("memtop = phystop") to size the entire free-
     * memory pool. Leaving it at 0x12345678 (~291MB) made every later
     * allocation (including the TPA for the first launched process)
     * wildly oversized, which is what actually produced the corrupt,
     * out-of-range stack pointers that crashed GEMDOS process dispatch.
     *
     * The real top of usable RAM is the top of the low MMU window built
     * by virt_mmu_bootstrap() (see the call above: 0x08000000 bytes,
     * hardcoded to match this file's own MMU size argument and the
     * qemu -m 128 invocation documented for this target), minus the 1MB
     * reserved at its very top for the MMU page table itself (see the
     * 0x47f00000 page-table address passed to virt_mmu_bootstrap: that is
     * VIRT_RAM_BASE + 0x07f00000, i.e. 127MB into the window) -- the same
     * "reserve the last megabyte" convention bios/machine/raspi/memory.c
     * uses for the same reason.
     */
    ldr r0, =_phystop
    ldr r1, =0x07f00000
    str r1, [r0]

    /* Clear the BSS segment now that virtual addressing is live. */
    ldr r0, =__bss
    ldr r1, =__ebss
    mov r2, #0
1:  cmp r0, r1
    bge 2f
    str r2, [r0], #4
    b   1b
2:

    /*
     * Install the exception vector table above.  Nothing has touched VBAR
     * up to this point, so it still holds its reset value of 0, and the
     * CPU vectors every exception into the low TOS system variables --
     * which are data, so the first exception of any kind (an SVC from
     * trap1(), or the generic timer IRQ) executes garbage and turns into
     * an endless Undefined Instruction loop at address 0x04.  The linked
     * address of arm_vectors is valid here because the low MMU window
     * maps virtual 0..RAM_SIZE onto physical VIRT_RAM_BASE..  See
     * bios/machine/raspi/startup.S, which does the same thing.
     */
    ldr r0, =arm_vectors
    mcr p15, 0, r0, c12, c0, 0

    ldr sp, =_stktop
    b   _biosmain

print_string:
    push {r4, lr}
1:  ldrb r0, [r4], #1
    cmp r0, #0
    beq 2f
    bl  _virt_uart0_write_byte
    b   1b
2:  pop {r4, lr}
    bx  lr

/*
 * If the low MMU window is wrong, _phystop's alias doesn't actually reach
 * the fixed low sysvars addresses everything from here on depends on.
 * Continuing (BSS clear, _biosmain) would silently corrupt or misread
 * low memory instead of failing loudly, so halt here instead.
 */
sysvars_check_failed:
    ldr r4, =sysvars_fail_msg
    bl  print_string
1:  b   1b

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

/* bios/kprint.c's dopanic() reads this, unconditionally, on every machine
 * built with __arm__.  This machine never enters HYP mode (see the comment
 * at the top of _main), so it is always 0. */
    .globl _start_in_hyp
_start_in_hyp: .word 0
