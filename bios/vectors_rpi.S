/*
 * vectors_rpi.S - exception vectors
 * The ARM processor has a much leaner exception vector table.
 * In order to simplify porting of the OS (and eventually TSRs hooking into)
 * interrupts, we attempt to simulate the 68k setup by performing some initial
 * decoding in the native handlers and then fetch the address of a 68-k like
 * handler from the same offsets as defined on the 68000.
 *
 * Copyright (C) 2001-2017 by the EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"

// ==== Definitions ==========================================================

        .globl  _init_arm_vec
        .globl  _init_exc_vec
        .globl  _init_user_vec

        .globl  _gemtrap        // call VDI/AES
        .globl  _biostrap       // call BIOS from C
        .globl  _xbiostrap      // call XBIOS from C
        .globl  _xbios_unimpl

        .globl  _check_read_byte
        .globl  _int_vbl

        .globl  _int_illegal
        .globl  _int_priv

        .globl  _just_rts       // really _just_bxlr
        .globl  _just_rte       // really _just_movspc
        .globl  _bell
        .globl  _keyclick
        .globl  _trap_save_area

        .globl  _call_etv_critic
        .globl  _default_etv_critic

// ==== References ===========================================================

        // TOS System variables
        .extern _longframe
        .extern _vbclock
        .extern _frclock
        .extern _vblqueue
        .extern _swv_vec
        .extern _hz_200
        .extern etv_timer
        .extern etv_critic
        .extern _timer_ms
        .extern _v_bas_ad

        .extern _GSX_ENTRY
        .extern _blink          // conout.c - console output

// Note: this scheme is designed to print the exception number
// for vectors 2 to 63 even if working on a 32bit address bus.
// LVL.

        .extern _dopanic

        .extern _proc_lives
        .extern _proc_dregs
        .extern _proc_aregs
        .extern _proc_enum
        .extern _proc_usp
        .extern _proc_stk

        .text



/* basically initialize the 62 exception vectors. */
.balign 4
_init_exc_vec:
        mov     r2, #0
        ldr     r1, =_proc_lives
        str     r2, [r1]
        ldr     r1,	=any_vec // Default panic handler for unhandled exceptions
        mov     r2, #8
        mov     r0, #62
set_vec:
        str     r1, [r2], #+4
        subs    r0, #1
        bne     set_vec
        bx      lr

/* initialize the 192 user vectors. */

_init_user_vec:
        ldr     r1,	=user_vec // Default panic handler for unhandled user vectors
        mov     r2, #256
        mov     r0, #192
set_uvec:
        str     r1, [r2], #+4
        subs    r0, #1
        bne     set_uvec
        bx      lr

user_vec:


// A side effect of that we are using a dispatch routine to calculate the correct
// vector number, the ip register should contain the (simulated) vector address.

// we copy debug data in the structure at address 0x380, then go in dopanic()
any_vec:
        cpsid   if      // Disable interrupts
        push    {ip}    // Save ip register for later
        // dump the registers, including user space stack pointer
        // use the ip register as scratch register
        ldr     ip,	=_proc_dregs
        stmia   ip!, {r0-r11}
        pop     {r0}        // Get the original ip back (although already overritten by the dispatch routine)
        pop     {r1}        // The original lr is the last register pushed to the stack by the exception dispatcher
        stmia   ip!, {r0, r1, sp}
        stmia   ip, {lr}^   // Dump the user space lr register to the last entry in the register list
        ldr     r1,	=_proc_usp
        stmia   r1, {sp}^

        // compute and dump the exception number TODO: figure out if this is the best way to do it
        lsr     r0, #2
        ldr     r1,	=_proc_enum
        // dump 8 32 bit words from the stack
        ldmfd   sp, {r1-r8}
        ldr     r0,	=_proc_stk
        stmia   r0, {r1-r8}
        // magic value
        ldr     r0,	=_proc_lives
        ldr     r1, proc_magic
        str     r1, [r0]

        // restore a sane stack
        // this stack must be large enough for dopanic();
        ldr     sp,	=panic_stack
        b       _dopanic
proc_magic:
    .word 0x12345678


/*
 * _gemtrap - VDI trap dispatcher
 *
 * This functions checks for a trap-2 VDI call and jumps to the VDI dispatcher
 * then. It also provides a simple vq_gdos call and the "d0=-1" query function
 * that returns the dispatcher's address.
 *
 * Note that this function does not care about AES calls since the AES later
 * hooks into the trap-2 vector itself.
 *
 */

_gemtrap:
        cmp     r0, #0x73               // VDI call?
        beq     2f

no_vdi:
        cmn     r0, #2                  // vq_gdos call? (r0 == -2)
        beq     1f                      // r0 is already -2  -  no GDOS installed

        cmn     r0, #1                  //  query for jsr:able address? (r0 == -1?)
        ldreq   r0, =_GSX_ENTRY         // Return address of VDI dispatcher
1:
        movs    pc, lr                  // Return from exception
2:
        // save return state onto the SVC stack
        srsfd sp!, #0x13
        // Call VDI entry. Arguments are already in registers at the right place
        bl      _GSX_ENTRY
        // return to caller with saved state on stack
        rfefd   sp!

/*
 * _int_vbl - Int 0x70 - VBL interrupt
 *
 * no video resolution change is done.
 */

_int_vbl:
        push {r0-r1}
        ldr     r0, =_frclock           // increase num of happened ints
        ldrh    r1, [r0]
        add     r1, r1, #1
        strh    r1, [r0]

        ldr     r0,	=_vblsem            // check vbl semaphore (TODO: non-atomic)
        ldrh    r1, [r0]
        subs    r1, r1, #1
        strh    r1, [r0]
        bmi     vbl_end                 // if VBl routine disabled -> end

        push {r2-r12,r14}               // save remaining registers

        ldr     r0,	=_vbclock           // count number of VBL interrupts
        ldrh    r1, [r0]
        add     r1, r1, #1
        strh    r1, [r0]

        bl      _blink                  // blink cursor

#if CONF_WITH_FDC
        // flopvbl
        bl    _flopvbl
#endif

        // vblqueue
        ldr     r1,	=_nvbls
        ldrhs   r0, [r1]
        beq     vbl_no_queue
        sub     r0, r0, #1
        ldr     r1,	=_vblqueue
        ldr     r1, [r1]
vbl_queue_loop:
        ldr     r2, [r1], #4
        cmp     r2, #0
        beq     vbl_queue_next
        ldr     r2, [r2]
        blx     r2
vbl_queue_next:
        subs    r0, #1
        bpl     vbl_queue_loop
vbl_no_queue:

        // screen hardcopy
        // (not done)

        pop {r2-r12,r14}                // restore registers
vbl_end:
        ldr     r0,	=_vblsem            // release vbl semaphore (TODO: non-atomic)
        ldrh    r1, [r0]
        add     r1, r1, #1
        strh    r1, [r0]

        pop {r0-r1}                     // restore remaining registers
        bx lr


// ==== Critical error handler functions =====================================

/*
 * (1) Call the critical error handler from C
 *     As opposed to 68k, since eabi already passess arguments in r0-r4, there
 *     isn't much to do. We use the ip register to load the handler address,
 *     as we are allowed to muck around with that during procedure calls.
 */
_call_etv_critic:
        ldr     ip, =_etv_critic
        ldr     ip, [ip]            // read contents of the field
        // r0 and r1 already contain the correct values
        bx      ip                  // Direct branch, will cause the handler to return direct to the caller
/*
 * (2) The default character-mode critical error handler
 *     Just returns the error code from the arguments
 */
_default_etv_critic:
        // error code is already in r0 -- simply return
        bx      lr

// ==== Trap 13 - BIOS entry point ==========================================

_biostrap:
        // funcion number is in r7 and arguments in r0-rN
        // For normal eabi C functions, only ther first 4 args are in registers,
        // so we push r45-r6,r8-11 to the stack.

        // save caller return address and SPSR on stack
        srsfd sp!, #0x13

        push    {r7}
        push    {r4-r6,r8-r11}

        ldr     r5, =_bios_ent
        ldrh    r5, [r5]        // r5 is now number of entries in the table
        ldr     r4, =_bios_vecs // r4 is the start of the table
        b       1f

// ==== Trap 14 - XBIOS entry point =========================================

_xbiostrap:
        // funcion number is in r7 and arguments in r0-rN
        // For normal eabi C functions, only ther first 4 args are in registers,
        // so we push r4-r6,r8-11 to the stack.

        // save caller return address and SPSR on stack
        srsfd sp!, #0x13

        push    {r7}
        push    {r4-r6,r8-r11}

        ldr     r5, =_xbios_ent
        ldrh    r5, [r5]         // r5 is now number of entries in the table
        ldr     r4, =_xbios_vecs // r4 is the start of the table

// ==== Trap 13+14 handler ==================================================

1:
        // Difference from 68k tos, is that we rely on the stack to be reentrant.
        // Another difference is that arguments are in registers, so we don't
        // need to fiddle with the stack pointer for that.

        cmp     r7,r5           // Higher than highest number?
        movhi   r0,#0           // Return 0 when out of range
        bhi     2f

        ldr     ip, [r4, +r7, LSL #2]   // get the procedure's address
        blx     ip            //

2:
        pop   {r4-r6,r8-r11}       // restore stacked argument registers
        pop   {r7}                 // restore r7
        // return to caller with saved state on stack
        rfefd   sp!


/*
 * (X)BIOS unimplemented function handler.
 * These functions are called indirectly, ONLY from biosxbios.
 * The requested BIOS/XBIOS function number is in r7 upon entry, so we move that to r0.
 */

_xbios_unimpl:
        mov     r0, r7
        b       _xbios_do_unimpl // Chain into C funcion

/*
 * _just_rts / _just_rte - we just return back to whatever dispatched the handler
 */

buggy_jit_save_registers_before_bus_error:
_just_rts:
_just_rte:
        bx lr

_keyclick:
        ldr     ip, =_kcl_hook
        ldr     ip, [ip]
        bx      ip

// ==== Variables ============================================================

        .bss
        .balign 4

        .space    128*4           // this stack is only used by dopanic()
panic_stack:



// ===========================================================================
// ==== End ==================================================================
// ===========================================================================
