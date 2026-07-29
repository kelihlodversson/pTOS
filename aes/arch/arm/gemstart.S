/*      GEMSTART.S */

/*
*       Copyright 1999, Caldera Thin Clients, Inc.
*                 2002-2017 The EmuTOS development team
*
*       This software is licenced under the GNU Public License.
*       Please see LICENSE.TXT for further information.
*
*                  Historical Copyright
*       -------------------------------------------------------------
*       GEM Application Environment Services              Version 3.0
*       Serial No.  XXXX-0000-654321              All Rights Reserved
*       Copyright (C) 1987                      Digital Research Inc.
*       -------------------------------------------------------------
*/

#include "asmdefs.h"
#include "asmstruct.h"

        .globl  _ui_start
        .globl  _accdesk_start
        .globl  _gemdos
        .globl  _dos_exec
        .globl  _ui_mupb

        .extern _ad_stail
        .extern _ad_envrn
        .extern _size_of_theglo
        .extern _D
        .extern _bzero
        .extern _justretf
        .extern _drwaddr
        .extern _tikcod
        .extern _tikaddr
        .extern _init_p0_stkptr
        .extern _gem_main
        .extern _gl_changerez
        .extern _run_accs_and_desktop
        .extern _disable_interrupts
        .extern _rlr
        .extern _enable_interrupts

        .text

//////////////////////////////////////////////////////////////////////////////
// AES start-up code
//////////////////////////////////////////////////////////////////////////////
_ui_start:
        // our basepage address is in r0. Copy it to r11 so it won't get clobbered by other calls below
        mov     r11, r0

        // set our temporary user stack
        ldr     sp, =gemusp

        // ad_stail is the address to the cmdline buffer in our basepage
        ldr     ip, =_ad_stail
        add     r2, r11, #0x80
        str     r2, [ip]

        // mshrink to the needed size for TEXT+DATA+BSS plus 256 bytes.
        ldr     r3, [r11, #PD_p_tlen]
        ldr     r2, [r11, #PD_p_dlen]
        add     r3, r3, r2
        ldr     r2, [r11, #PD_p_blen]
        add     r3, r3, r2
        add     r3, r3, #SIZEOF_PD  // Size of base page
        mov     r2, r11
        mov     r1, #0
        mov     r0, #0x4a           // MSrink(0, basepage, size)
        svc     1

        // old_gem_ssp = super(0)
        mov     r1, #0
        mov     r0, #0x20           // Super(0)
        svc     1                   // Switch to supervisor mode
        ldr     ip, =old_gem_ssp
        str     r0, [ip]

        // ad_envrn = the pointer to the environ string
        ldr     ip, =save_ad_envrn
        ldr     r0, [r11, #PD_p_env]
        str     r0, [ip]

aes_restart:
        // set our temporary stack again, so we can call bzero()
        // after an AES restart
        ldr     sp, =gemusp

        // restore original environment pointer
        ldr     ip, =save_ad_envrn
        ldr     r0, [ip]

        ldr     ip, =_ad_envrn
        str     r0, [ip]

        // clear the 'global memory' zone
        ldr     r1, =SIZEOF_THEGLO
        ldr     r0, =_D         // address
        bl      _bzero          // clear it

        // _drwaddr ??? set to just_rts
        ldr     r0, =_justretf
        ldr     ip, =_drwaddr
        str     r0, [ip]

        // _tikaddr ??? set to tikcod
        ldr     r0, =_tikcod
        ldr     ip, =_tikaddr
        str     r0, [ip]

        // call C routine to init the SSP in the UDA & return its offset from _D
        // KH: Odd choice to return the offset, since we add the base to it immediately
        bl      _init_p0_stkptr

        ldr     sp, =_D // D is a badly named global variable containing all the global gem vars
        add     sp, sp, r0

        // go in C
        bl      _gem_main

        // Check for resolution change - if so start over again
        ldr     ip, =_gl_changerez
        ldrh    r0, [ip]
        teq     r0, #0
        bne     aes_restart

        // Super() to go back to user mode
        ldr     r1, =old_gem_ssp
        ldr     r1, [r1]
        mov     r0, #0x20
        svc     1                   // Super(old_gem_ssp)

        // Pterm0()
        mov     r0, #0
        svc     1


//////////////////////////////////////////////////////////////////////////////
// Run accessories and desktop
//////////////////////////////////////////////////////////////////////////////
_accdesk_start:
        // basepage address is in r0. Copy it to r11 so it won't get clobbered by other calls below
        mov     r11, r0
        ldr     sp, =_D
        ldr     sp, [sp, UDA_SPSUPER]   // use AES process 0 stack


        ldr     r3, [r11, #PD_p_tlen]       // calc memory required
        ldr     r2, [r11, #PD_p_dlen]
        add     r3, r3, r2
        ldr     r2, [r11, #PD_p_blen]
        add     r3, r3, r2
        add     r3, r3, #SIZEOF_PD  // Size of base page
        mov     r2, r11
        mov     r1, #0
        mov     r0, #0x4a           // MSrink(0, basepage, size)
        svc     1

        // Switch to supervisor mode
        mov     r1, #0
        mov     r0, #0x20           // Super(0)
        svc     1

        bl      _run_accs_and_desktop

        // Pterm0()
        mov     r0, #0
        svc     1


//////////////////////////////////////////////////////////////////////////////
// Execute a GEMDOS call (see gemdos.c). // TODO: inline this
//////////////////////////////////////////////////////////////////////////////
_gemdos:
        push    {r4, lr}
        ldr     r4, [sp, #+2*4]

        svc     1
        pop     {r4, pc}


//////////////////////////////////////////////////////////////////////////////
// A wrapper around Pexec. We can't use the above function _gemdos for this
// since _gemdos is not reentrant.
//////////////////////////////////////////////////////////////////////////////
_dos_exec:

        // *** WARNING ***
        // We are here on the private supervisor stack of the AES process 0.
        // We are going to call Pexec() from the supervisor mode.
        // The GEMDOS will backup some data on our stack, then it will
        // switch to its private supervisor stack. Then it will switch
        // to the child program (desktop or other) which will call the AES
        // through a trap #2.
        // The AES trap prologue always switch to rlr->p_uda->u_spsuper.
        // That variable currently points to somewhere on our stack.
        // Thus, if we don't take care, the stack pointer may go up in
        // the next trap #2 and cause data corruption.
        // In other words, there is a reentrency problem. The AES context
        // will not be switched when entering the child program, so this
        // AES process will reenter itself. The workaround is to set
        // rlr->p_uda->u_spsuper to a value low enough on the current stack
        // to avoid a stack smashing. We have to estimate the amount of
        // data that will be stored on the stack between here and the next
        // stack switch in trap #2.
        // From the rlr->p_uda->u_spsuper backup below to the next trap #2
        // stack switch, the following data will be pushed on the stack:
        // {{TODO: this is for m68k, recalculate this for __ARM__}}
        //   - the Pexec() arguments (16 bytes)
        //   - the trap #1 stack frame (0 byte, popped by trap #1 prologue)
        //   - the backup of registers in the trap #1 prologue (50 bytes)
        // Currently, this sums to 66 bytes. The stack has to be adjusted
        // by this value, or more.
        // You must keep an eye on the trap #1 prologue for GEMDOS,
        // when entering in supervisor mode and calling a function
        // other than 0x20. It is located in bdos/rwa.S, label "systk".
        // If some additional data is pushed on the stack before
        // the stack switch, you must add the equivalent room here.


        // note, r5 and r6 are pushed to reserve space for
        // UDA_SPSUPER and the result from Pexec on the stack.
        // Only r4, r7 and the return adress is actually preserved.
        push   {r0-r4, r5-r6, r7, lr}
        bl     _disable_interrupts

        ldr     r7, =_rlr
        ldr     r7, [r7]
        ldr     r7, [r7, PD_UDA]
        ldr     ip, [r7, UDA_SPSUPER]
        str     ip, [sp, #+4*5]         // Save rlr->p_uda->u_spsuper where we saved r5 earlier

        sub     ip, sp, #0x50           // Adjust the stack (see above)
        str     ip, [r7, UDA_SPSUPER]

        bl      _enable_interrupts

        // refetch parameter registers (shifted up by one)
        pop     {r1-r4}
        mov     r0,  #0x4B
        svc     1                      // ... and execute the Pexec call!
        ldr     r4, [sp], #4
        str     r0, [sp,  #4]          // save the result from being stomped on
        bl      _disable_interrupts

        ldr     r7, =_rlr
        ldr     r7, [r7]
        ldr     r7, [r7, PD_UDA]
        ldr     ip, [sp], #4           // read back the old value for UDA_SPSUPER now at the top of the stack
        str     ip, [r7, UDA_SPSUPER]  // Restore rlr->p_uda->u_spsuper

        bl      _enable_interrupts
        pop     {r0, r7, pc}

.bss


old_gem_ssp:
        .ds.l   1



// Space for the user stack:
        .ds.b   256
gemusp:


saveregs:
        .ds.l   2

// pointer to original environment, restored on AES restart
save_ad_envrn:
        .ds.l   1
