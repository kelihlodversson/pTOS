/*
 * startup.S - EmuTOS startup module for QEMU's ARM 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"
#include "header.h"
#include "virt_memmap.h"

        .globl  C_SYM(os_entry)
        .extern C_SYM(stktop)
        .extern C_SYM(biosmain)
        .extern __endvdibss
        .extern C_SYM(virt_mmu_bootstrap)
        .extern C_SYM(phystop)
        .extern __bss
        .extern __ebss

        .text

/*
 * OSHEADER -- identical in shape to bios/machine/raspi/startup.S; see
 * that file for a field-by-field description.
 */

        .globl  C_SYM(main)
        .globl  C_SYM(os_beg)
        .globl  C_SYM(os_magic)
        .globl  C_SYM(os_date)
        .globl  C_SYM(os_conf)
        .globl  C_SYM(os_dosdate)
        .globl  C_SYM(root)
        .globl  C_SYM(shifty)
        .globl  C_SYM(run)

C_SYM(os_entry):
.p2align 5
arm_vectors:
    b   C_SYM(main)
    b   C_SYM(arm_dispatch_undef)
    b   C_SYM(arm_dispatch_svc)
    b   C_SYM(arm_dispatch_prefetch_abort)
    b   C_SYM(arm_dispatch_data_abort)
    b   C_SYM(arm_dispatch_undef)
    b   C_SYM(arm_dispatch_irq)
    b   C_SYM(arm_dispatch_fiq)

os_version:
    .word   TOS_VERSION
reseth:
    .word   C_SYM(main)
C_SYM(os_beg):
    .word   C_SYM(os_entry)
os_end:
    .word   __endvdibss
os_res1:
    .word   C_SYM(main)
C_SYM(os_magic):
#if CONF_WITH_AES
    .word   C_SYM(ui_mupb)
#else
    .word   0
#endif
C_SYM(os_date):
    .word   OS_DATE
C_SYM(os_conf):
#if CONF_MULTILANG
    .word   OS_CONF_MULTILANG
#else
    .word   (OS_COUNTRY << 1) + OS_PAL
#endif
C_SYM(os_dosdate):
    .word   OS_DOSDATE
os_root:
    .word   C_SYM(root)
os_kbshift:
    .word   C_SYM(shifty)
os_run:
    .word   C_SYM(run)
os_dummy:
    .ascii  "CAMELTOS"

/*
 * _main -- two-phase boot: a physical-mode "hello" (proves the CPU
 * reached here and the UART works before the MMU is on), then the
 * MMU bring-up (bios/machine/virt-arm/virt_mmu.c) using only literal
 * physical constants, then a long jump into virtual addressing, then
 * a second "hello" plus a sysvars round-trip check using ordinary
 * symbol-based addressing to prove the low fixed-address TOS system
 * variables (tosvars.ld) are correctly mapped via the low MMU window,
 * then hands off to _biosmain.
 */
.balign 4
C_SYM(main):
    /*
     * The boot loader hands us r0 = 0, r1 = the ARM Linux machine type
     * (or 0xffffffff for "use the device tree") and r2 = the address of
     * the ATAG list or of a flattened device tree, per the standard ARM
     * Linux boot protocol -- see bios/machine/raspi/startup.S, which
     * does the same thing.  r0-r2 are needed for the MMU bring-up call
     * below and are not preserved across it, so stash them in
     * callee-saved registers (AAPCS guarantees virt_uart0_init() and
     * _virt_mmu_bootstrap() leave r8-r10 alone) until BSS has been
     * cleared and arm_boot_regs can actually be written to.
     */
    mov r8, r0
    mov r9, r1
    mov r10, r2

    /*
     * How much RAM there is: QEMU's -m, from the memory node of the device
     * tree in r2.  virt_fdt_ram_size() needs no stack, which matters here:
     * the early stack goes at the top of the RAM, so it can only be set
     * up once the size is known.  Without a device tree, assume the
     * 128 MiB the port was written for.  The size is also limited to
     * 128 MiB, because the low virtual window that maps the fixed-address
     * TOS system variables (see virt_mmu.c) stops where the devices start
     * -- RAM beyond that is not used -- and rounded down to whole
     * megabytes.  r11 (callee-saved) keeps it until the MMU is set up.
     */
    mov r0, r10
    bl  virt_fdt_ram_size
    cmp r0, #0
    ldreq r0, =VIRT_DEFAULT_RAM_SIZE
    ldr r1, =VIRT_MAX_RAM_SIZE
    cmp r0, r1
    movhi r0, r1
    lsr r0, r0, #20
    lsl r11, r0, #20

    ldr r1, =VIRT_RAM_BASE
    add r1, r1, r11             /* top of the RAM */
    sub sp, r1, #0x10000        /* early stack: its last 64 KiB */
    bl  C_SYM(virt_uart0_init)
    adr r4, hello_msg_phys     /* PC-relative: correct at the physical PC we're
                                 * actually executing at pre-MMU, unlike
                                 * "ldr r4, =symbol" which loads the linked
                                 * (virtual) address -- not valid yet. */
    bl  print_string

    /* MMU bring-up: literal physical constants only, no linked symbols */
    mov r0, r11                 /* the RAM window */
    ldr r1, =VIRT_RAM_BASE
    add r1, r1, r11
    sub r1, r1, #0x100000       /* page table: the last megabyte of the RAM */
    bl  C_SYM(virt_mmu_bootstrap)

    ldr pc, =virt_arm_post_mmu

virt_arm_post_mmu:
    ldr r1, =VIRT_RAM_BASE
    add r1, r1, r11
    sub sp, r1, #0x10000    /* physical scratch stack, until the switch to C_SYM(stktop) below */
    ldr r4, =hello_msg_virt
    bl  print_string

    /* sysvars round-trip: _phystop is just an ordinary global on ARM
     * now (bios/arch/arm/tosvars.c -- see kelihlodversson/pTOS#219), so
     * this no longer proves anything specific to sysvars, but it's a
     * convenient, already-linked address to sanity-check that the low
     * MMU window actually maps virtual low addresses onto real RAM
     * before anything else here relies on memory access working at
     * all. */
    ldr r0, =C_SYM(phystop)
    ldr r1, =0x12345678
    str r1, [r0]
    ldr r2, [r0]
    cmp r1, r2
    bne sysvars_check_failed

    ldr r4, =sysvars_ok_msg
    bl  print_string

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
     * _phystop lives in that BSS range now (bios/arch/arm/tosvars.c),
     * so it must be set to its real value *after* clearing BSS, not
     * before -- otherwise the clear loop above would immediately
     * zero it back out. bios/biosmem.c's bmem_init() reads it
     * directly ("memtop = phystop") to size the entire free-memory
     * pool; leaving it at 0 (or the 0x12345678 test pattern, before
     * this was reordered to run after the clear) produced corrupt,
     * out-of-range stack pointers that crashed GEMDOS process
     * dispatch.
     *
     * The real top of usable RAM is the top of the low MMU window built
     * by virt_mmu_bootstrap() (the RAM size found from the device tree
     * above, in r11), minus the 1MB reserved at its very top for the MMU
     * page table itself -- the same "reserve the last megabyte"
     * convention bios/machine/raspi/memory.c uses for the same reason.
     */
    ldr r0, =C_SYM(phystop)
    sub r1, r11, #0x100000
    str r1, [r0]

    /*
     * Now that BSS is cleared (arm_boot_regs lives there, like every
     * other global) and virtual addressing is live, store the boot
     * loader registers saved in r8-r10 above.  Mirrors
     * bios/machine/raspi/startup.S.
     */
    ldr r3, =C_SYM(arm_boot_regs)
    stmia r3, {r8-r10}

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

    ldr sp, =C_SYM(stktop)
    b   C_SYM(biosmain)

/*
 * virt_fdt_ram_size -- the size of the RAM, from a flattened device tree
 *
 * In:  r0 = address of the device tree
 * Out: r0 = the size in bytes found in the "reg" property of the first
 *      memory node ("memory" or "memory@..."), or 0 if there is no valid
 *      tree or no such node.  r1-r7 and r12 are used; there is no stack,
 *      since this runs before the RAM size, and so the stack, is known.
 *
 * Just enough of the format (see the Devicetree Specification, "Flattened
 * Devicetree (DTB) Format") to do that.  The header and the structure block
 * are big-endian 32-bit words.  The structure block is a sequence of tokens:
 * 1 = begin node (then the node's name, a NUL terminated string padded to a
 * multiple of 4), 2 = end node, 3 = property (then the length and the offset
 * of its name in the strings block, and the data padded to a multiple of 4),
 * 4 = nop, 9 = end.  Everything is read in aligned words or bytes, as the
 * MMU is not on yet.  "reg" has 2 address and 2 size cells on this board
 * (4 words, the size being the last two), or 1 and 1 (2 words).
 */
virt_fdt_ram_size:
    ldr r1, [r0]
    rev r1, r1
    ldr r2, =0xd00dfeed         /* FDT_MAGIC */
    cmp r1, r2
    bne fdt_none
    ldr r1, [r0, #8]            /* off_dt_struct */
    rev r1, r1
    add r1, r0, r1              /* r1: the next token */
    ldr r3, [r0, #12]           /* off_dt_strings */
    rev r3, r3
    add r3, r0, r3              /* r3: the strings block */
    mov r4, #0                  /* r4: depth of the node we are in */
    mov r5, #0                  /* r5: depth of the memory node, 0 if not in it */
fdt_token:
    ldr r2, [r1], #4
    rev r2, r2
    cmp r2, #1
    beq fdt_begin
    cmp r2, #2
    beq fdt_end
    cmp r2, #3
    beq fdt_prop
    cmp r2, #4
    beq fdt_token               /* nop */
fdt_none:
    mov r0, #0                  /* the end of the tree, or garbage */
    bx  lr

fdt_begin:
    add r4, r4, #1
    cmp r5, #0
    bne fdt_skip_name           /* already inside the memory node */
    ldr r6, [r1]                /* is the name "memory" + NUL or '@'? */
    ldr r7, =0x6f6d656d         /* "memo" */
    cmp r6, r7
    bne fdt_skip_name
    ldrh r6, [r1, #4]
    ldr r7, =0x7972             /* "ry" */
    cmp r6, r7
    bne fdt_skip_name
    ldrb r6, [r1, #6]
    cmp r6, #0
    cmpne r6, #'@'
    moveq r5, r4
fdt_skip_name:
    ldrb r6, [r1], #1
    cmp r6, #0
    bne fdt_skip_name
    add r1, r1, #3
    bic r1, r1, #3
    b   fdt_token

fdt_end:
    cmp r5, r4
    moveq r5, #0                /* leaving the memory node */
    sub r4, r4, #1
    b   fdt_token

fdt_prop:
    ldr r6, [r1], #4            /* length of the data */
    rev r6, r6
    ldr r7, [r1], #4            /* offset of the name */
    rev r7, r7
    cmp r5, #0
    beq fdt_next_prop
    cmp r5, r4
    bne fdt_next_prop           /* not a property of the memory node itself */
    add r7, r3, r7              /* its name: is it "reg"? */
    ldrb r12, [r7]
    cmp r12, #'r'
    bne fdt_next_prop
    ldrb r12, [r7, #1]
    cmp r12, #'e'
    bne fdt_next_prop
    ldrb r12, [r7, #2]
    cmp r12, #'g'
    bne fdt_next_prop
    ldrb r12, [r7, #3]
    cmp r12, #0
    bne fdt_next_prop
    cmp r6, #16
    ldreq r0, [r1, #8]          /* 2+2 cells: the size is the last two words */
    ldreq r12, [r1, #12]
    beq fdt_have_size
    cmp r6, #8
    bne fdt_next_prop
    mov r0, #0                  /* 1+1 cells: a single word */
    ldr r12, [r1, #4]
fdt_have_size:
    rev r0, r0                  /* the high word, big-endian */
    rev r12, r12                /* the low word */
    cmp r0, #0
    mvnne r12, #0               /* 4 GiB or more: say 4 GiB - 1 */
    mov r0, r12
    bx  lr
fdt_next_prop:
    add r6, r6, #3
    bic r6, r6, #3
    add r1, r1, r6
    b   fdt_token

print_string:
    push {r4, lr}
1:  ldrb r0, [r4], #1
    cmp r0, #0
    beq 2f
    bl  C_SYM(virt_uart0_write_byte)
    b   1b
2:  pop {r4, lr}
    bx  lr

/*
 * If the low MMU window is wrong, ordinary memory access doesn't
 * actually reach real RAM the way everything from here on depends on.
 * Continuing (BSS clear, _biosmain) would silently corrupt or misread
 * memory instead of failing loudly, so halt here instead.
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

    .globl C_SYM(arm_dispatch_undef)
C_SYM(arm_dispatch_undef):
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

    .globl C_SYM(arm_dispatch_svc)
C_SYM(arm_dispatch_svc):
    // See the matching comment in bios/machine/raspi/startup.S: taking the
    // svc exception already masked IRQ (architectural), and unlike m68k
    // TRAP #n this would leave any handler that blocks waiting for an
    // interrupt (e.g. bconin2()'s stop_until_interrupt() loop) unable to
    // ever be woken (#185).
    cpsie i
    ldr ip, [lr, #-4]
    and ip, ip, #0xF
    lsl ip, ip, #0x2
    ldr ip, [ip, #+0x80]
    bx  ip

    .globl C_SYM(arm_dispatch_prefetch_abort)
C_SYM(arm_dispatch_prefetch_abort):
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

    .globl C_SYM(arm_dispatch_data_abort)
C_SYM(arm_dispatch_data_abort):
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

    .extern C_SYM(virt_int_handler)
    .globl C_SYM(arm_dispatch_irq)
C_SYM(arm_dispatch_irq):
    sub    lr, lr, #4
    srsfd  sp!, #0x13
    cps    #0x13
    stmfd  sp!, {r0-r3, ip, lr}
    bl     C_SYM(virt_int_handler)
    ldmfd  sp!, {r0-r3, ip, lr}
    rfefd  sp!

    .globl C_SYM(arm_dispatch_fiq)
C_SYM(arm_dispatch_fiq):
    sub    lr, lr, #4
    srsfd  sp!, #0x13
    cps    #0x13
    rfefd  sp!

/* bios/kprint.c's dopanic() reads this, unconditionally, on every machine
 * built with __arm__.  This machine never enters HYP mode (see the comment
 * at the top of _main), so it is always 0. */
    .globl C_SYM(start_in_hyp)
C_SYM(start_in_hyp): .word 0

/*
 * r0, r1 and r2 as saved near the top of _main.  This is BSS, so it is
 * cleared by the loop above _before_ _main stores into it (unlike
 * bios/machine/raspi/startup.S, which has to preserve it across a
 * bzero() called much later from C).  The layout matches arm_boot_regs_t
 * in include/arch/arm/arm_boot.h.
 */
.bss
.balign 4
.global C_SYM(arm_boot_regs)
C_SYM(arm_boot_regs): .ds.l 3
