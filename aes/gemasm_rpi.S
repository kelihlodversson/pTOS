/*
 * gemasm.S - assembler part of GEM
 *
 * Copyright 2002-2017, The EmuTOS development team
 *           1999, Caldera Thin Clients, Inc.
 *           1987, Digital Research Inc.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"
#include "asmstruct.h"
#include "asmpd.h"

        .globl  _psetup
        .globl  _gotopgm
        .globl  _dsptch
        .globl  _switchto

        .extern _rlr
        .extern _indisp
        .extern _disp

        .text


/*
 * psetup(PD *p, PFVOID codevalue)
 * arranges for codevalue to be pushed to the stack for process p,
 * "in preparation for an pop {pc} that will start this process executing".
 * That pop will occur at the end of switchto()
 */

_psetup:
        // PD* in r0, New process entry point in r1
        mrs     r3, cpsr        // current psr saved in r3
        cpsid   if              // disable interrupts

        ldr     r0, [r0, PD_UDA]
        ldr     r2, [r0, UDA_SPSUPER]
        str     r1, [r2, #-4]!  // Push entry point on the process stack
        str     r2, [r0, UDA_SPSUPER] // Update stack pointer in PD struct

        msr     cpsr_c, r3      // restore saved interupt level
        bx      lr              // return to caller


/*
 * gotopgm(void)
 * launches the top of rlr list
 *
 * This is the entry point for accessory processes.
 * It is called in supervisor mode, on the private AES stack of the new
 * accessory process. Its responsibility is to go to the start of the
 * accessory TEXT segment in user mode. The register a0 must contain the
 * address of the basepage. The value of the other registers is not
 * important.
 */

_gotopgm:
        // get the basepage for the top of rlr
        ldr     r0, =_rlr
        ldr     r0, [r0]
        ldr     r1, [r0, PD_UDA]
        ldr     r0, [r0, PD_LDADDR] // The basepage must stay in r0

        // *** WARNING ***
        // We are currently on the top of the private stack for the
        // new accessory process. On the next trap #2 for AES, the CPU
        // will use this stack when going to supervisor mode, and this
        // will be perfectly correct.
        // Unfortunately, the AES trap prologue always switch to
        // rlr->p_uda->u_spsuper even if it is already on that stack.
        // Thus, if we don't take care, the stack pointer may go up in
        // the next trap #2 and cause data corruption.
        // Since ARM does not push data to the current stack, unlike 68k,
        // we currently don't need to fix rlr->p_uda->u_spsuper.

        // You must keep an eye on the "trap #2" prologue for AES.
        // It is located in gemdosif_rpc.S, label "trapaes".
        // If some additional data is pushed on the stack before
        // the stack switch, you must add the equivalent room here.

        // mov  r2, sp  // Current stack
        // sub  r2, r2, #0 // additional space for the trap handler
        // str  r2, [r1, UDA_SPUSER]  // Will be the stack on next trap

        // Fake a return from exception to enter user mode executing
        // the new process. (This way, we don't have to execute anything in here in user mode.)
        mrs     r2, cpsr          // read current psr
        bic     r2, r2, #0x1f     // clear mode bits
        orr     r2, r2, #0x10     // set them to user mode
        msr     spsr_cfsx, r2     // overwrite the saved spsr with our mode
        ldr     lr, [r0, #P_TBASE] // Start of the TEXT segment
        movs    pc, lr            // jump to user process in user mode

/*
 * dsptch() is a machine (& compiler) dependent routine which:
 *      1) saves any flags that will be trashed
 *      2) if (indisp) restore flags, return to dsptch caller
 *      3) otherwise
 *              save machine state,
 *              jump to disp
 */

_dsptch:
        push    {r0-r3, lr}
        ldr     ip, =_indisp
        ldrb    r0, [ip]
        teq     r0, #0
        popne   {r0-r3, pc}

savestate:
        add     r0, r0, #1
        strb    r0, [ip]

        mrs     r3, cpsr        // current psr saved in r3
        cpsid   if              // disable interrupts

        ldr     r0, =_rlr       // Get pointer to rlr PD structure
        ldr     r0, [r0]

        ldr     r1, [r0, PD_UDA]        // Get pointer to UDA structure
        add     r2, r1, UDA_REGS + 4*4  // storage for regs r4-r11
        stmia   r2, {r4-r11}
        add     r2, r1, UDA_REGS    // storage for regs r0+
        pop     {r4-r7}             // copy the old values of r0-r3 to the save structure
        stmia   r2!, {r4-r7}        // (note: we leave the reurn address on the stack as it will be used by _switchto when swiching back to this process)

        cps     #0x1f
        str     sp, [r1, UDA_SPUSER]    // Save old usp in UDA struct
        cps     #0x13
        str     sp, [r1, UDA_SPSUPER]   // Save old ssp in UDA struct

        ldr     sp, =gemasm_stack
        msr     cpsr_cfxs, r3
        b       _disp


_switchto:
        // r0 points to UDA struct
        mrs     ip, cpsr        // current psr saved in lr
        cpsid   if              // disable interrupts
        mov     r1, #0
        ldr     r2, =_indisp
        strb    r1, [r2]

        cps     #0x1f
        ldr     sp, [r0, UDA_SPUSER] // get user stack pointer
        cps     #0x13
        ldr     sp, [r0, UDA_SPSUPER] // get new stack pointer

        // Now read the remaining registers stored in UDA_REGS
        add     r0, r0, UDA_REGS
        ldm     r0, {r0-r11}

        msr     cpsr_cfxs, ip
        ldr     pc, [sp], #4    // Switch to the next process now !

        .bss

        .ds.b    0x0280
gemasm_stack:
        .ds.b    4
