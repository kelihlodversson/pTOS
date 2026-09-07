/*
 * startup.S - EmuTOS startup module
 *
 * Copyright (C) 2001-2017 by the EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */



#include "asmdefs.h"
#include "header.h"

#if defined(TARGET_RPI4)
#define CORE0_MBOX3_SET             0xff80008C
#else
#define CORE0_MBOX3_SET             0x4000008C
#endif

#if CONF_WITH_RASPI_UART0
#if defined(TARGET_RPI1)
#define EARLY_UART0_BASE             0x20201000
#elif defined(TARGET_RPI4)
#define EARLY_UART0_BASE             0xfe201000
#else
#define EARLY_UART0_BASE             0x3f201000
#endif
#define EARLY_UART0_DR               (EARLY_UART0_BASE + 0x00)
#define EARLY_UART0_FR               (EARLY_UART0_BASE + 0x18)
#endif
// ==== Definitions ==========================================================

        .globl  _os_entry

// ==== References ===========================================================

        // TOS System variables
        .extern _stktop
#        // Memory
        .extern _raspi_vcmem_init  // initialize the memory size and system variables

        // BIOS C part
        .extern _biosmain     // BIOS C entry point

        // Overall memory map
        .extern __endvdibss   // end of VDI BSS

// ===========================================================================
// ==== TEXT segment (TOS image) =============================================
// ===========================================================================

        .text
#ifndef TARGET_RPI1
        .arch_extension sec
        .arch_extension virt    // To use the eret and msr ELR_hyp, lr instructions below
#endif

/*
 * OSHEADER
 */

        .globl  _main           // OS entry point
        .globl  _os_beg
        .globl  _os_magic
        .globl  _os_date
        .globl  _os_conf
        .globl  _os_dosdate
        .globl  _root
        .globl  _shifty
        .globl  _run



// Note that this difers from 68k TOS. We put the ARM exception vector table
// here instead of version information.
_os_entry:
.p2align 5 // The vector table needs to have all 5 lowest bits set to zero
arm_vectors:
    b   _main                   // os_entry, branch to _main
    b   _arm_dispatch_undef
    b   _arm_dispatch_svc
    b   _arm_dispatch_prefetch_abort
    b   _arm_dispatch_data_abort
    b   _arm_dispatch_undef
    b   _arm_dispatch_irq
    b   _arm_dispatch_fiq

os_version:
    .word   TOS_VERSION // os_version, TOS version
reseth:
    .word   _main       // reseth, pointer to reset handler
_os_beg:
    .word   _os_entry   // os_beg, base of os = _sysbase
os_end:
    .word   __endvdibss // os_end, end of VDI BSS
os_res1:
    .word   _main       // os_res1, reserved
_os_magic:
#if CONF_WITH_AES
    .word   _ui_mupb    // os_magic, pointer to GEM's MUPB
#else
    .word   0           // os_magic, pointer to GEM's MUPB
#endif
_os_date:
    .word   OS_DATE     // os_date, Date of system build
_os_conf:               // Flag for PAL version + country
#if CONF_MULTILANG
    .word   OS_CONF_MULTILANG
#else
    .word   (OS_COUNTRY << 1) + OS_PAL
#endif
_os_dosdate:
    .word   OS_DOSDATE  // os_date, Date of system build in GEMDOS format
os_root:
    .word   _root       // Pointer to the GEMDOS mem pool
os_kbshift:
    .word   _shifty     // Pointer to the keyboard shift keys states
os_run:
    .word   _run        // Pointer to a pointer to the actual basepage
os_dummy:
    .ascii  "CAMELTOS"


/*
 * Initialize system upon entry
 *
 */

.extern _raspi_screen_init
.extern _raspi_screen_debug
.balign 4
_main:

    /*
     * The boot loader hands us r0 = 0, r1 = the ARM Linux machine type and
     * r2 = the address of the ATAG list, or of a flattened device tree when
     * the firmware built one.  bios/bootargs.c reads this to recognize a
     * "ptos.lang=xx" parameter on the command line.  r0-r2 are reused a
     * few instructions below, so save them while they still mean
     * something.  Note that QEMU started with -bios does not set them up at
     * all, so a reader must not assume the pointer is valid.
     */
#ifndef TARGET_RPI1
    /*
     * Only the boot core is handed meaningful values.  The ARM1176 of the
     * first generation Pi has no MPIDR, but it has no secondary cores
     * either, so the test is only needed here.
     */
    mrc p15, 0, r3, c0, c0, 5   /* read MPIDR into r3 */
    ands r3, r3, #3
    bne 1f
#endif
    ldr r3, =_arm_boot_regs
    stmia r3, {r0-r2}
1:

#ifndef TARGET_RPI1
    ldr r1, =_start_in_hyp
    mov r2, #0
    str r2, [r1]
    bl  switch_from_hyp

    /* with some firmware versions, for some reason we need to call into monitor mode and unset the NS bit */
    /* copy our temporary monitor vectors to the old vector base address. */
    mrc p15, 0, r1, c12, c0, 0 ;@ get vbar
    ldr r0,=_monitor_vectors
    ldmia r0!,{r2,r3,r4,r5,r6,r7}
    stmia r1!,{r2,r3,r4,r5,r6,r7}

    /* flush caches etc.*/
    mov r12,#0
    mcr p15, 0, r12, c7, c10, 1
    dsb
    mov r12, #0
    mcr p15, 0, r12, c7, c5, 0
    mov r12, #0
    mcr p15, 0, r12, c7, c5, 6
    dsb
    isb

    /* call into our newly inserted monitor handler to clear the NS bit */
    //smc #0

    /* Since TOS does not support SMP, a quick hack to tell remaining cores to
    start execution in _start_secondary */

    // On real hardware, only core 0 starts executing at 0x8000, but
    // Qemu seems to send all cores to this address.
    mrc p15,0,r0,c0,c0,5    // Read MPIDR into r0
    ands r0,#3              // Mask out core id
    bne  _start_secondary   // Make sure only core 0 continues from here

_core0:
    // On real hardware, the other cores are looping somewhere in low
    // memory waiting for someone to give them an address through a
    // mailbox mechanism. We'll send them to our own loop in case we
    // overwrite the memory they're currently at.

    ldr r1, =_start_secondary

    // Get mailbox for core 1
    ldr r0, = CORE0_MBOX3_SET + 0x10
    str r1, [r0]

    // Get mailbox for core 2
    ldr r0, = CORE0_MBOX3_SET + 0x20
    str r1, [r0]

    // Get mailbox for core 3
    ldr r0, = CORE0_MBOX3_SET + 0x30
    str r1, [r0]
    dsb
#endif

    /* Note that we set all the stack pointers to point to the same place,
      as we actually only use the user and supervisor stack pointer. */
    cps #0x11               /* set fiq mode */
    ldr sp, =_stktop
    cps #0x12               /* set irq mode */
    ldr sp, =_stktop
    cps #0x17               /* set abort mode */
    ldr sp, =_stktop
    cps #0x1B               /* set "undefined" mode */
    ldr sp, =_stktop
    cps #0x13               /* set supervisor mode */
    ldr sp, =_stktop

    ldr r0, =arm_vectors
    mcr p15, 0, r0, c12, c0, 0      /* set VBAR to our table at the start of the os */

    /* Enable VFP/NEON before the first C code runs.  On real hardware the
     * firmware does this; QEMU's raspi2b resets with CPACR = 0 and
     * FPEXC.EN = 0, so the first vldr in _raspi_vcmem_init would take an
     * undefined instruction exception before processor_init() gets the
     * chance to enable them. */
    mrc     p15, 0, ip, c1, c0, 2
    orr     ip, ip, #0xF00000       /* enable cp10 (single precision) and cp11 (double precision) */
    mcr     p15, 0, ip, c1, c0, 2
#ifdef TARGET_RPI1
    mov     ip, #0
    mcr     p15, 0, ip, c7, c5, 4   /* flush prefetch buffer (ARMv6 has no isb) */
#else
    isb                             /* make the CPACR change visible before the first CP10/11 access */
#endif
    mov     ip, #0x40000000         /* FPEXC.EN (bit 30) */
    mcr     p10, 7, ip, c8, c0, 0   /* VMSR FPEXC, ip */
#ifdef TARGET_RPI1
    mov     ip, #0
    mcr     p15, 0, ip, c7, c5, 4
#else
    isb
#endif

/*
 * memory configuration
 */

    bl     _raspi_vcmem_init         // ask the video core for the memory configuration (needs sp)

/*
 * From now, the stack is allowed, and C code can be used.
 * The BIOS startup goes on in bios.c
 */

    b     _biosmain

/* Exit from HYP mode if the boot loader started there */
#ifndef TARGET_RPI1

switch_from_hyp:
    mrs r0 , cpsr
    /* test for HYP mode */
    eor r0, r0, #0x1A
    tst r0, #0x1F
    /* clear mode bits */
    bic r0 , r0 , #0x1F
    /* mask IRQ/FIQ bits and set SVC mode */
    orr r0 , r0 , #0xC0 | 0x13
    /* mask Abort bit */
    orr r0, r0, #0x100
    /* branch if not HYP mode */
    bne 1f

    ldr r1, =_start_in_hyp
    mov r2, #1
    str r2, [r1]

    msr spsr_cxsf, r0

    mrc     p15, 0, r2, c1, c0,  0
    bic     r2, r2, #(1 << 1)
    mcr     p15, 0, r2, c1, c0,  0

    @ Disable all traps, so we don't get any nasty surprise
    mov r7, #0
    mcr p15, 4, r7, c1, c1, 0   @ HCR
    mcr p15, 4, r7, c1, c1, 2   @ HCPTR
    mcr p15, 4, r7, c1, c1, 3   @ HSTR
    mcr p15, 4, r7, c1, c0, 0   @ HSCTLR
    mrc p15, 4, r7, c1, c1, 1   @ HDCR
    and r7, r7, #0x1f   @ Preserve HPMN
    mcr p15, 4, r7, c1, c1, 1   @ HDCR

    msr ELR_hyp, lr
    eret
1:  msr cpsr_cxsf, r0
    bx lr

    .globl  _start_secondary
_start_secondary:
    dsb
    cpsid ifa
1:  wfi
    b   1b

#endif

    .globl _arm_dispatch_undef
_arm_dispatch_undef:
    srsfd  sp!, #0x13      /* push lr and SPSR onto supervisor stack */
    cps    #0x13           /* switch to supervisor mode */

    sub    sp, #4*15
    stmia  sp, {r0-r14}^   /* store user registers r0-r14 (unbanked) */

    // get the offending instruction
    ldr    r3, [sp, #+4*15]
    sub    r3, r3, #4
    ldr    r2, [r3]

    ldr    r0, =0xfff000f0
    ldr    r1, =0xe7f000f0
    and    r0, r0, r2
    cmp    r0, r1
    moveq  r0, #0x28       // VEC_LINEA - we use e7f...f. as the lineA emulation
    movne  r0, #0x10       // VEC_ILLEGAL - other illegal instructions will simulate an illegal instruction vector
    mov    r1, sp          // Stack pointer points to the exception frame
    ldr    ip, [r0]        // load handler address the illegal instruction will be passed in as the first argument
    blx ip                 // jump to it. The signature is void (*)(int type, exception_frame_t* frame, u32 instruction, u32 pc)

    add    sp, #4*15
    ldmfd  sp, {r0-r14}^   /* restore user registers */
    rfefd  sp!             /* load pc and CPSR from stack */

    .globl _arm_dispatch_svc
_arm_dispatch_svc:
    // Taking the svc exception itself already masked IRQ (architectural,
    // not our choice). Unlike m68k TRAP #n, which never raised the
    // interrupt priority level, that means any handler below which blocks
    // waiting for an interrupt -- e.g. bconin2()'s stop_until_interrupt()
    // loop in bios/ikbd.c waiting on kb_timerc_int() -- would deadlock:
    // the interrupt it needs can't fire while it's masked here. A call
    // that returns normally is unaffected (rfefd restores CPSR from the
    // pre-trap SPSR_svc), so this only bit us on the first blocking
    // console read a process makes (#185).
    cpsie i
    ldr ip, [lr, #-4]      // load svc instruction from code
    and ip, ip, #0xF       // mask out low nybble of instruction (we emulate trap #0 - #15)
    lsl ip, ip, #0x2       // multiply index by 4
    ldr ip, [ip, #+0x80]   // load handler address at 80 + ip
    bx  ip                 // jump to it. It will use the registers as arguments.

    .globl _arm_dispatch_prefetch_abort
_arm_dispatch_prefetch_abort:

    sub    lr, lr, #4      /* lr: correct PC of aborted program */
    srsfd  sp!, #0x13      /* push lr and SPSR onto supervisor stack */
    cps    #0x13           /* switch to supervisor mode */

    sub    sp, #4*15
    stmia  sp, {r0-r14}^   /* store user registers r0-r14 (unbanked) */
    mrc p15, 0, r2, c5, c0,  1  // FSR in r2
    mrc p15, 0, r3, c6, c0,  2  // FAR in r3


    mov r0, #0x08   // VEC_BUS_ERROR
    mov r1, sp      // Stack pointer points to the exception frame
    ldr ip, [r0]    // load handler address
    blx ip          // jump to it. The signature is void (*)(int type, exception_frame_t* frame, u32 fsr, u32 far)

    add    sp, #4*15
    ldmfd  sp, {r0-r14}^   /* restore user registers */
    rfefd  sp!             /* load pc and CPSR from stack */

    .globl _arm_dispatch_data_abort
_arm_dispatch_data_abort:
    sub lr, lr, #8         /* lr: correct PC of aborted program */
    srsfd  sp!, #0x13      /* push lr and SPSR onto supervisor stack */
    cps    #0x13           /* switch to supervisor mode */

    sub    sp, #4*15
    stmia  sp, {r0-r14}^   /* store user registers r0-r14 (unbanked) */
    mrc p15, 0, r2, c5, c0,  0  // FSR in r2
    mrc p15, 0, r3, c6, c0,  0  // FAR in r3


    mov r0, #0x0c   // VEC_ADDRESS_ERROR
    mov r1, sp      // Stack pointer points to the exception frame
    ldr ip, [r0]    // load handler address
    blx ip          // jump to it. The signature is void (*)(int type, exception_frame_t* frame, u32 fsr, u32 far)

    add    sp, #4*15
    ldmfd  sp, {r0-r14}^   /* restore user registers */
    rfefd  sp!             /* load pc and CPSR from stack */

    .extern _raspi_int_handler
    .globl _arm_dispatch_irq
_arm_dispatch_irq:
    sub    lr, lr, #4      /* lr: correct PC of aborted program */
    srsfd  sp!, #0x13      /* push lr and SPSR onto supervisor stack */
    cps    #0x13           /* switch to supervisor mode */
    stmfd  sp!, {r0-r3, ip, lr} /* store registers not saved by the C handler (link register is important in case the interrupt interrupted while the processor was in SVC mode.)*/
    bl     _raspi_int_handler
    ldmfd  sp!, {r0-r3, ip, lr}
    rfefd  sp!             /* load pc and CPSR from stack */


    .globl _arm_dispatch_fiq
_arm_dispatch_fiq:
    sub    lr, lr, #4      /* lr: correct PC of aborted program */
    srsfd  sp!, #0x13      /* push lr and SPSR onto supervisor stack */
    cps    #0x13           /* switch to supervisor mode */
    // TODO: fixme
    rfefd  sp!             /* load pc and CPSR from stack */

// Helper exception vectors to get the processor into secure mode:
#ifndef TARGET_RPI1
_monitor_vectors:
    .word 0 /* reset */
    .word 0 /* undef */
    adr pc, _secure_monitor
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0 /* pad */

.align 5
_secure_monitor:
    mrc     p15, 0, r1, c1, c1, 0   @ read SCR
    bic     r1, r1, #0x4e           @ clear IRQ, FIQ, EA, nET bits
    orr     r1, r1, #0x31           @ enable NS, AW, FW bits
    mcr     p15, 0, r1, c1, c1, 0   @ write SCR (with NS bit set)

    // Allow unaligned access
    // mrc     p15, 0, r2, c1, c0,  0
    // bic     r2, r2, #(1 << 1)
    // mcr     p15, 0, r2, c1, c0,  0

    movs    pc, lr @ return to non-secure SVC
#endif

.bss
.global _start_in_hyp
_start_in_hyp: .ds.l 1

/*
 * r0, r1 and r2 as saved at the top of _main.  This is BSS, so like
 * _start_in_hyp above it has to be carried across the bzero() in
 * raspi_vcmem_init().  The layout matches arm_boot_regs_t in
 * include/arch/arm/arm_boot.h.
 */
.global _arm_boot_regs
_arm_boot_regs: .ds.l 3
