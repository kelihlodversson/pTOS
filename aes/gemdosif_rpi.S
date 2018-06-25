/*
 * gemdosif.S - Assembler part of the GEMDOS interface
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

        .globl  _justretf
        .globl  _unset_aestrap
        .globl  _set_aestrap
        .globl  _aestrap_intercepted
        .globl  _far_mcha
        .globl  _aes_wheel
        .globl  _far_bcha
        .globl  _drawrat
        .globl  _aestrap

        .globl  _tikaddr
        .globl  _tiksav
        .globl  _CMP_TICK
        .globl  _NUM_TICK
        .globl  _drwaddr
        .globl  _tikcod

        .extern _eralert
        .extern _rlr
        .extern _super
        .extern _b_click
        .extern _mchange
        .extern _forkq
        .extern _wheel_change
        .extern _tchange
        .extern _b_delay
        .extern _os_beg

        .extern  _disable_interrupts
        .extern  _enable_interrupts
        .extern  _giveerr
        .extern  _takeerr
        .extern  _retake
        .extern  _savetrap2

// **** Trap entry: ****
_aestrap:
        // opcode 0 means exit
        teq r0, #0
        beq _back

        // c8 and c9 are handled by us
        cmp r0, #0xC8
        cmpne r0, #0xC9
        beq trapaes

        // Chain to old trap handler in case op codes don't match
        ldr ip, =_savetrap2
        ldr ip, [ip]
        bx  ip

_back:
        // save return state onto the SVC stack
        srsfd sp!, #0x13

        // call gemdos(0x4c, 0)
        mov r0, #0
        mov r7, #0x4C
        svc 1

        // return from SVC trap
        rfefd sp!


trapaes:
        srsfd   sp!,    #0x1f // save return state onto the user stack
        cpsid   if,     #0x1f // mask out interrupts and jump to system mode

        // now sp is the user stack
        push    {r4-r11}                // put registers to user stack
        mov     r4,     sp              // copy user stack to r4

        // Go back to SVC mode, interruppts are still disabled
        cps #0x13

        // Save info on the current process
        ldr     r6,     =_rlr
        ldr     r6,     [r6]
        ldr     r6,     [r6, PD_UDA]
        mov     r3,     #1
        strh    r3,     [r6, UDA_INSUPER]     // Flag that we are entering _super
        str     r4,     [r6, UDA_SPUSER]      // Save the current user stack pointer
        str     sp,     [r6, UDA_OLDSPSUPER]  // Save supervisor stack pointer

        // Switch to supervisor stack pointer from the structure
        ldr     sp,     [r6, UDA_SPSUPER]

        cpsie   if                      // Enable interrupts
        blx     _super                  // Calling super (opcode and paramblock parameters are already in r0 and r1)
        cpsid   if                      // mask out interrupts

        // r6 is callee saved, and therefore still points to PD_UDA from above
        mov     r3,     #0
        strh    r3,     [r6, UDA_INSUPER]     // Flag that we have left _super
        str     sp,     [r6, UDA_SPSUPER]     // Save current stack pointer
        ldr     sp,     [r6, UDA_OLDSPSUPER]  // Get back previous super stack

        // switch to SYSTEM mode
        cps     #0x1f

        // now sp is the user stack -- restore it from the saved data
        ldr     sp,     [r6, UDA_SPUSER]      // Read back user stack
        pop     {r4-r11}                      // read back registers

        // return back to caller by popping off lr and spsr from the user stack
        // this will also restore intterrupts to the previous value
        rfefd   sp!


.macro push_privstack newstack
        mov     ip, sp          // tmp copy of the old stack pointer
        ldr     sp, =\newstack  // switch to the new stack pointer
        str     ip, [sp, #-4]!  // push the old sp to the top of the stack
.endm

.macro pop_privstack
        ldr     sp, [sp]
.endm

.text

_far_bcha:
        push_privstack gstksave
        push    {r0-r3,lr}
        bl      _b_click
        pop     {r0-r3,lr}
        pop_privstack
        bx      lr

_far_mcha:
        push_privstack gstksave
        push    {r0-r3,lr}
        ldmfd   sp, {r1,r2} // shift arg 1 and 2 up by one
        ldr     r0, =_mchange
        bl      _forkq
        pop     {r0-r3,lr}
        pop_privstack
        bx      lr

#if CONF_WITH_VDI_EXTENSIONS
/* AES mouse wheel handler called by the VDI */
_aes_wheel:
        push_privstack gstksave
        push    {r0-r3,lr}
        ldmfd   sp, {r1,r2} // shift arg 1 and 2 up by one
        ldr     r0, =_wheel_change
        bl      _forkq
        pop     {r0-r3,lr}
        pop_privstack
        bx      lr
#endif /* CONF_WITH_VDI_EXTENSIONS */


/*
*/
_drawrat:
        ldr     ip, =_drwaddr
        ldr     ip, [ip]
        bx      ip


_justretf:
        bx      lr


_tikcod:
        push_privstack tstack
        push    {r0-r4,lr}
        ldr     r4, =_NUM_TICK
        ldmia   r4, {r1,r2} // r1 <- *NUM_TICK; r2 <- *CMP_TICK
        teq     r2, #0      // if (*CMP_TICK != 0)
        beq     1f          // {
        add     r1, #1      //      *NUM_TICK ++;
        subs    r2, #1      //      *CMP_TICK --;
        stmia   r4, {r1,r2} //      save back to memory
        bne     1f          //      if (*CMP_TICK == 0) {
        ldr     r0, =_tchange
        bl      _forkq      //          forkq(tchange, *NUM_TICK);
                            //      }
1:                          // }

        mov     r0, #1
        bl      _b_delay    // b_delay(1);

        pop     {r0-r4,lr}
        pop_privstack

        b        _tiksav    // Chain to vector stored in _tiksav


.bss

_drwaddr:
        .ds.l    1

_tikaddr:
        .ds.l    1
gstksave:
        .ds.l    1

tstksave:
        .ds.l    1

_tiksav:
        .ds.l    1
_NUM_TICK:
        .ds.l    1
_CMP_TICK:
        .ds.l    1

/*
 * miscellaneous stacks
 */
        .balign 4

        .space  0x400
gstack:                         // gsx stack for mouse

        .balign 4
        .space  0x400
tstack:                         // tick stack
        .space  4
