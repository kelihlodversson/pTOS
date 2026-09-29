/*
 * gsx2.c - VDI (GSX) bindings
 *
 * Copyright (C) 2014-2019 The EmuTOS development team
 *
 * Authors:
 *  VRI   Vincent Rivière
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "gsx2.h"
#include "obdefs.h"
#include "gsxdefs.h"

#ifdef __x86_64__
#include "asm.h"        /* x86_64_kernel_trap() */
#endif

VDIPB vdipb;

void gsx2(void)
{
    vdipb.contrl = &contrl;

#ifdef __arm__
    register long _r1 __asm__("r1")=(long)(&vdipb);
    __asm__ volatile (
        "mov r0,#0x73; svc 2"
        :
        : "r"(_r1)
        : "r0", "r2", "r3", "r7", "r12", "lr",  "memory", "cc"
    );
#elif defined(__x86_64__)
    /*
     * x86-64 calling convention (bios/arch/x86_64/trap.h): trap class 2 is
     * X86_64_TRAP_GEM, packed into the high 32 bits the same way
     * xbiosbind.h's own x86_64 branches pack trap class 14; opcode 0x73
     * ("ordinary VDI call") is the low 32 bits, and &vdipb is the single
     * argument, matching every other arch's r1/d1.
     */
    x86_64_kernel_trap(((long)2 << 32) | 0x73, (long)&vdipb, 0, 0, 0);
#else

    __asm__ volatile
    (
        "move.l  %0,d1\n\t"
        "moveq   #0x73,d0\n\t"
        "trap    #2"
    :
    : "g"(&vdipb)
    : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc"
    );
#endif
}
