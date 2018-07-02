/*
 * gsx2.c - VDI (GSX) bindings
 *
 * Copyright (C) 2014 The EmuTOS development team
 *
 * Authors:
 *  VRI   Vincent Rivière
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "gsx2.h"
#include "obdefs.h"
#include "gsxdefs.h"

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
