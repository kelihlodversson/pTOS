/*
 * vdi_session.h - open the screen for a test program (VDI, and the AES if it is there)
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * Under the AES (the suite started from the desktop) the screen is the
 * AES's: a virtual workstation is opened on it, as gdos_answer does;
 * opening and closing the physical one would take it from the desktop.
 * Without an AES (EmuCON) the physical workstation is opened.  Built on
 * vdi_speedo.h's parameter arrays; for m68k and ARM only, like it.
 */

#ifndef VDI_SESSION_H
#define VDI_SESSION_H

#include "vdi_speedo.h"

/* the AES, for the case of a desktop running the suite */
static short aes_control[5], aes_global[15];
static short aes_there;
static long aes_addr[2];
static void *aespb[6] = { aes_control, aes_global, vs_intin, vs_intout, aes_addr, aes_addr };

/* an AES call: without an AES, trap #2 leaves everything as it was */
static void aes(short op, short nintin, short nintout)
{
    aes_control[0] = op;
    aes_control[1] = nintin;
    aes_control[2] = nintout;
    aes_control[3] = 0;
    aes_control[4] = 0;
#ifdef __mc68000__
    {
        register long d0 __asm__("d0") = 200;
        register void *d1 __asm__("d1") = aespb;

        __asm__ volatile (
            "trap #2"
            : "+d" (d0), "+d" (d1)
            :
            : "d2", "a0", "a1", "memory", "cc");
    }
#else
    {
        register long r0 __asm__("r0") = 200;
        register void *r1 __asm__("r1") = aespb;

        __asm__ volatile (
            "svc 2"
            : "+r" (r0), "+r" (r1)
            :
            : "r2", "r3", "r12", "lr", "memory", "cc");
    }
#endif
}


/* a workstation on the screen: its handle, or 0 */
static short vs_open_screen(void)
{
    short i, handle;

    aes_global[0] = 0;
    aes(10, 0, 1);                      /* appl_init() */
    aes_there = aes_global[0] != 0;
    for (i = 0; i < 10; i++)
        vs_intin[i] = 1;
    vs_intin[10] = 2;                   /* raster coordinates */
    if (aes_there)
    {
        short phys;

        aes(77, 0, 5);                  /* graf_handle() */
        phys = vs_intout[0];
        for (i = 0; i < 10; i++)
            vs_intin[i] = 1;
        vs_intin[10] = 2;
        vs_call(phys, 100, 0, 0, 11);   /* v_opnvwk() */
    }
    else
        vs_call(0, 1, 0, 0, 11);        /* v_opnwk(), the screen */
    handle = vs_contrl[6];
    if (handle <= 0)
    {
        if (aes_there)
            aes(19, 0, 1);              /* appl_exit() */
        return 0;
    }

    return handle;
}

static void vs_close_screen(short handle)
{
    if (aes_there)
    {
        vs_call(handle, 101, 0, 0, 0);  /* v_clsvwk() */
        aes(19, 0, 1);                  /* appl_exit() */
    }
    else
        vs_call(handle, 2, 0, 0, 0);    /* v_clswk() */
}

#endif /* VDI_SESSION_H */
