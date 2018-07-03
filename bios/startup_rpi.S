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

#define CORE0_MBOX3_SET             0x4000008C
// ==== Definitions ==========================================================

        .globl  _os_entry

// ==== References ===========================================================

        // TOS System variables
        .extern resvalid
        .extern resvector
        .extern _stktop
#        // Memory
        .extern meminit       // initialize the memory and its controller

        // BIOS C part
        .extern _biosmain     // BIOS C entry point

        // Overall memory map
        .extern __etext       // end of text section
        .extern __edata       // end of data section
        .extern __endvdibss   // end of VDI BSS

/*
 * Helper macro to enter SVC mode cleanly and mask interrupts. reg is
 * a scratch register for the macro to overwrite.
 *
 * This macro is intended for forcing the CPU into SVC mode at boot time.
 * you cannot return to the original mode.
 *
 * Copied from the circle project: https://github.com/rsta2/circle
 */
	.macro safe_svcmode_maskall reg:req

	mrs	\reg , cpsr
	eor	\reg, \reg, #0x1A		/* test for HYP mode */
	tst	\reg, #0x1F
	bic	\reg , \reg , #0x1F		/* clear mode bits */
	orr	\reg , \reg , #0xC0 | 0x13	/* mask IRQ/FIQ bits and set SVC mode */
	bne	1f				/* branch if not HYP mode */
	orr	\reg, \reg, #0x100		/* mask Abort bit */
	adr	lr, 2f
	msr	spsr_cxsf, \reg
	.word	0xE12EF30E	/* msr ELR_hyp, lr */
	.word	0xE160006E	/* eret */
1:	msr	cpsr_c, \reg
2:

	.endm
	.text

// ===========================================================================
// ==== TEXT segment (TOS image) =============================================
// ===========================================================================

        .text


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
    b	_arm_dispatch_svc
    b	_arm_dispatch_prefetch_abort
    b	_arm_dispatch_data_abort
    b	_arm_dispatch_undef
    b	_arm_dispatch_irq
    b	_arm_dispatch_fiq

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
#if WITH_AES
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

.balign 4
_main:

	safe_svcmode_maskall r0

#ifndef TARGET_RPI1
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
#endif

    /* Note that we set all the stack pointers to point to the same place,
      as we actually only use the user and supervisor stack pointer. */
	cps	#0x11				/* set fiq mode */
	ldr	sp, =_stktop
	cps	#0x12				/* set irq mode */
	ldr	sp, =_stktop
	cps	#0x17				/* set abort mode */
	ldr	sp, =_stktop
	cps	#0x1B				/* set "undefined" mode */
	ldr	sp, =_stktop
	cps	#0x13				/* set supervior mode */
	ldr	sp, =_stktop

    #if 0
    /* Enable MMU */
    mrc     p15, 0, r1, c1, c0,  1      // Read AUX Control register
    orr     r1, #1 << 6     // restrict cache size to 16K on RPI1, SMP_Cace on RPI2+
    mcr     p15, 0, r1, c1, c0,  1

    ldr     r1, =ttbr0              // addr(TTBR0)

    ldr     r2, =0x0000040E
    mov     r3, #0                  // from 0x00000000
    mov     r4, #0x200              //   to 0x1FFFFFFF
    bl      set_pgtbl_entry

    ldr     r2, =0x00002416
    mov     r3, #0x200              // from 0x20000000 (incl. peripherals)
    mov     r4, #0x1000             //   to 0xFFFFFFFF
    bl      set_pgtbl_entry

    ldr     r2, =0x0000040E
    mov     r3, #0x480              // framebuffer at 0x48000000
    mov     r4, #0x490              // make 16 Mbyte cacheable
    bl      set_pgtbl_entry

    mov     r3, #3
    mcr     p15, #0, r3, c3, c0, #0 // set domain 0 to master

    mcr     p15, #0, r1, c2, c0, #0 // set TTBR0 (addr of ttbr0)  (ptblwlk inner non cacheable,
                                    // outer non-cacheable, not shareable memory)
    /* Start L1 Cache */
    mov     r3, #0
    mcr     p15, #0, r3, c7, c7, #0 /* Invalidate data cache and flush prefetch buffer */
    mcr     p15, #0, r3, c8, c7, #0 /* Invalidate TLB */
    mrc     p15, #0, r2, c1, c0, #0 /* Read Control Register Configuration Data */
#ifdef TARGET_RPI1
    orr     r2, #1 << 23         /* EXTENDED_PAGE_TABLE */
#endif
    orr     r2, #1 << 12         /* L1_INSTRUCTION_CACHE */
    orr     r2, #1 << 11         /* BRANCH_PREDICTION */
    orr     r2, #1 << 2          /* L1_CACHE */
    bic     r2, #1 << 1          /* Allow unaligned access */
    orr     r2, #1 << 0          /* MMU enable */
    mcr     p15, #0, r2, c1, c0, #0 /* Write Control Register Configuration Data */
    #endif

	ldr	r0, =arm_vectors
	mcr	p15, 0, r0, c12, c0, 0		/* set VBAR to our table at the start of the os */

/*
 * memory configuration
 */

    bl     meminit         // do/test memory config and clear it (needs sp)



/*
 * From now, the stack is allowed, and C code can be used.
 * The BIOS startup goes on in bios.c
 */

    b     _biosmain

set_pgtbl_entry:
    lsl     r0, r3, #20             // = r3 * 0x100000 (1M)
    orr     r0, r2
    str     r0, [r1, r3, lsl #2]
    add     r3, #1
    cmp     r3, r4
    bne     set_pgtbl_entry
    mov     pc, lr

#ifndef TARGET_RPI1

	.globl	_start_secondary
_start_secondary:
	dsb
    cpsid ifa
1:	wfi
	b	1b

#endif

    .globl _arm_dispatch_undef
_arm_dispatch_undef:
    srsfd  sp!, #0x13      /* push lr and SPSR onto supervior stack */
    cps    #0x13           /* switch to supervior mode */

    sub    sp, #4*15
	stmia  sp, {r0-r14}^   /* store user registers r0-r14 (unbanked) */

    // get the offending instruction
    ldr    r3, [sp, #+4*15]
    sub    r3, r3, #4
    ldr    r2, [r3]

    ldr    r0, =#0xfff000f0
    ldr    r1, =#0xe7f000f0
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
    ldr ip, [lr, #-4]      // load svc instruction from code
    and ip, ip, #0xF       // mask out low nybble of instruction (we emulate trap #0 - #15)
    lsl ip, ip, #0x2       // multiply index by 4
    ldr ip, [ip, #+0x80]   // load handler address at 80 + ip
    bx  ip                 // jump to it. It will use the registers as arguments.

    .globl _arm_dispatch_prefetch_abort
_arm_dispatch_prefetch_abort:

    sub	   lr, lr, #4      /* lr: correct PC of aborted program */
    srsfd  sp!, #0x13      /* push lr and SPSR onto supervior stack */
    cps    #0x13           /* switch to supervior mode */

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
	sub	lr, lr, #8         /* lr: correct PC of aborted program */
    srsfd  sp!, #0x13      /* push lr and SPSR onto supervior stack */
    cps    #0x13           /* switch to supervior mode */

    sub    sp, #4*15
	stmia  sp, {r0-r14}^   /* store user registers r0-r14 (unbanked) */
    mrc p15, 0, r2, c6, c0,  0  // FAR in r2
    mrc p15, 0, r3, c5, c0,  0  // FSR in r3


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
	sub	   lr, lr, #4      /* lr: correct PC of aborted program */
    srsfd  sp!, #0x13      /* push lr and SPSR onto supervior stack */
    cps    #0x13           /* switch to supervior mode */
    stmfd  sp!, {r0-r3, ip, lr} /* store registers not saved by the C handler (link register is important in case the interrupt interrupted while the processor was in SVC mode.)*/
    bl     _raspi_int_handler
    ldmfd  sp!, {r0-r3, ip, lr}
    rfefd  sp!             /* load pc and CPSR from stack */


    .globl _arm_dispatch_fiq
_arm_dispatch_fiq:
	sub	   lr, lr, #4      /* lr: correct PC of aborted program */
    srsfd  sp!, #0x13      /* push lr and SPSR onto supervior stack */
    cps    #0x13           /* switch to supervior mode */
    // TODO: fixme
    rfefd  sp!             /* load pc and CPSR from stack */

.section .data

    .align 14
ttbr0:
    .space  4 << 12                        // 4 bytes * 4096 entries
