/*
 * EmuCON2 assembler routines
 *
 * Copyright (C) 2013-2021 The EmuTOS development team
 *
 * Authors:
 *  RFB    Roger Burrows
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifdef STANDALONE_CONSOLE
/* The standalone build does not include the kernel configuration headers. */
#define C_SYM1(prefix, name) prefix ## name
#define C_SYM0(prefix, name) C_SYM1(prefix, name)
#define C_SYM(name) C_SYM0(__USER_LABEL_PREFIX__, name)
#else
#include "asmdefs.h"
#endif

/*
 * getwh()/getht() are called from C on both builds, but the two builds'
 * compilers disagree on whether a C symbol gets an assembly-visible leading
 * underscore.  C_SYM() in asmdefs.h uses GCC's own predefined macro for
 * whichever convention is actually active.
 */
        .globl  C_SYM(getwh),C_SYM(getht)

        .text

#ifndef STANDALONE_CONSOLE
/*
 * _coma_start / the GEMDOS-BIOS-XBIOS trap wrappers below are only used
 * by the ROM build's own built-in-EmuCON entry convention. The standalone
 * build (emucon2.tos) links against libcmini instead, whose own crt0
 * provides _start and whose <mint/osbind.h> provides every GEMDOS/BIOS/
 * XBIOS call cmd.h needs directly -- see cmd.h and ../cli/Makefile.
 */
        .globl  C_SYM(coma_start)
        .globl  C_SYM(jmp_gemdos),C_SYM(jmp_bios),C_SYM(jmp_xbios)
        .globl  C_SYM(environment)
        .extern C_SYM(cmdmain)

C_SYM(coma_start):
        move.l  4(sp),a5                // a5 -> basepage
        lea     stack,sp
        move.l  12(a5),d0               // calc memory required
        add.l   20(a5),d0
        add.l   28(a5),d0
        addi.l  #256,d0
        move.l  d0,-(sp)                // Mshrink()
        move.l  a5,-(sp)
        clr.w   -(sp)
        move.w  #74,-(sp)
        trap    #1
        lea     12(sp),sp

#ifdef __mcoldfire__
        move.l 44(a5),d0
        move.l  d0,C_SYM(environment)
#else
        move.l 44(a5),C_SYM(environment)
#endif

        jsr     C_SYM(cmdmain)

        clr.w   -(sp)                   // Pterm0()
        trap    #1
#endif /* !STANDALONE_CONSOLE */

C_SYM(getwh):
        move.l  a2,-(sp)                // not a scratch reg for gcc
#ifdef __mcoldfire__
        .dc.w   0xa920                  // lineA init
#else
        .dc.w   0xa000                  // lineA init
#endif
        move.l  -44(a0),d0              // v_cel_mx/v_cel_my
        movea.l (sp)+,a2
        rts

C_SYM(getht):
        move.l  a2,-(sp)                // not a scratch reg for gcc
#ifdef __mcoldfire__
        .dc.w   0xa920                  // lineA init
#else
        .dc.w   0xa000                  // lineA init
#endif
        moveq   #0,d0
        move.w  -46(a0),d0              // v_cel_ht
        movea.l (sp)+,a2
        rts

#ifndef STANDALONE_CONSOLE
/*
 * system call interface
 */
C_SYM(jmp_gemdos):
        move.l  (sp)+,ret_addr          // so parameters are in the right place
        move.l  a2,save_a2              // must save a2/d2 because someone may
        move.l  d2,save_d2              //  intercept the trap & clobber them
        trap    #1
        move.l  save_a2,a2
        move.l  save_d2,d2
        move.l  ret_addr,-(sp)
        rts

C_SYM(jmp_bios):
        move.l  (sp)+,ret_addr
        move.l  a2,save_a2
        move.l  d2,save_d2
        trap    #13
        move.l  save_a2,a2
        move.l  save_d2,d2
        move.l  ret_addr,-(sp)
        rts

C_SYM(jmp_xbios):
        move.l  (sp)+,ret_addr
        move.l  a2,save_a2
        move.l  d2,save_d2
        trap    #14
        move.l  save_a2,a2
        move.l  save_d2,d2
        move.l  ret_addr,-(sp)
        rts

        .bss
C_SYM(environment): // Environment string, from the BASEPAGE
        .ds.l   1
ret_addr:
        .ds.l   1
save_a2:
        .ds.l   1
save_d2:
        .ds.l   1

        .ds.l   1024
stack:
#endif /* !STANDALONE_CONSOLE */
