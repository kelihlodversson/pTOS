/*
 * vdi_entry.c - Front end of the screen driver and mouse stuff
 *
 * Identical to vdi/arch/arm/vdi_entry.c: none of it is actually ARM
 * assembly or ARM-specific -- GSX_ENTRY() relies only on ordinary C
 * parameter passing (any calling convention that passes the first two
 * arguments as plain values works, SysV x86-64 included), and mouse_int()/
 * wheel_int() only touch arch-neutral linea_vars state. Kept as a
 * separate per-arch copy rather than moved to a generic location for the
 * same reason as util/arch/x86_64/memmove.c: nothing forces vdi_entry.o
 * to build from here specifically, but co-locating it under arch/x86_64/
 * documents that this arch reached the same "no arch-specific mouse/GSX
 * hardware yet" state ARM did, rather than looking like an oversight.
 *
 * mouse_int()/wheel_int() are registered as callbacks (vdi/vdi_mouse.c's
 * Initmous()/kbd_vectors->statvec) but never actually invoked on this
 * arch yet: there is no mouse or IKBD-equivalent driver calling them.
 * GSX_ENTRY() is likewise unreachable for now -- this arch's trap
 * dispatch (bios/arch/x86_64/trap.c) has no VDI/AES trap class the way
 * m68k/ARM's gemtrap()/bdos_trap2() do (CONF_WITH_AES is off for this
 * machine, and nothing else calls GSX_ENTRY() directly). Both still need
 * real, working implementations here (not stubs) so that whenever a
 * trap-2-style VDI entry point or a mouse driver does get built for this
 * arch, this file is already correct and needs no revisiting.
 *
 * Copyright 1999 Caldera, Inc. and Authors:
 * Copyright 2004-2017 The EmuTOS development team
 * Copyright      Steve Cavender
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */



#include "config.h"
#include "portab.h"
#include "intmath.h"
#include "vdi_defs.h"
#include "lineavars.h"
#include "string.h"
#include "kprint.h"

extern void screen(void);


#define ptsin_size 512          // max. # of elements allowed for PTSIN array
#define ptsin_max  ptsin_size/2 // max. # of coordinate pairs for PTSIN array
#define W_1        2            // byte offset to first element of an array
#define W_3        6            // byte offset to third element of an array



// We copy the param block from the user into a global linea variable
static WORD lcl_ptsin[ptsin_size];

/* Standard calling conventions pass params in registers on both ARM and
 * x86-64, so GSX_ENTRY does not need to be coded in assembler on either. */

int GSX_ENTRY(int op, VDIPB* paramblock)
{
    int i;
    /* Make a local copy of the array pointers in the user's parameter block. */
    linea_vars.local_pb.contrl = paramblock->contrl;
    linea_vars.local_pb.intin = paramblock->intin;
    linea_vars.local_pb.intout = paramblock->intout;
    linea_vars.local_pb.ptsout = paramblock->ptsout;
    linea_vars.local_pb.ptsin = lcl_ptsin;

    WORD save_ptsin_count = paramblock->contrl->nptsin;
    WORD save_intin_count = paramblock->contrl->nintin;

    if (paramblock->contrl->nptsin <= 0)
    {
        paramblock->contrl->nptsin = 0;
    }
    else
    {
        if(paramblock->contrl->nptsin > ptsin_max)
        {
            paramblock->contrl->nptsin = ptsin_max;
        }
        for (i = 0; i < paramblock->contrl->nptsin*2; i++)
        {
            lcl_ptsin[i] = paramblock->ptsin[i];
        }
    }
    if (paramblock->contrl->nintin <= 0)
    {
        paramblock->contrl->nintin = 0;
    }

    /* Call screen which contains all the C routines for the SCREEN DRIVER. */
    screen();

    // Restore ptsin and intin counts to the unsanitized sizes
    paramblock->contrl->nptsin = save_ptsin_count;
    paramblock->contrl->nintin = save_intin_count;

    return flip_y;
}

typedef struct {
    int x; int y;
} IntPoint;

static void inline scrn_clip(IntPoint* point)
{
    point->x = point->x < 0 ? 0 : point->x > xres ? xres : point->x ;
    point->y = point->y < 0 ? 0 : point->y > yres ? yres : point->y ;
}


/*
 * _mouse_int - Mouse interrupt routine
 * buf: address of mouse buffer from the IKBD/ACIA-equivalent driver
 * (none exists on this arch yet -- see this file's own header comment)
 */
void mouse_int(UBYTE *buf)
{
    WORD pressed, previous;
    BYTE delta_x, delta_y;
    IntPoint point;
    void (*user_but)(WORD) = linea_vars.user_but;
    void (*user_cur)(WORD,WORD) = linea_vars.user_cur;

    if(linea_vars.mouse_flag) // If we are in a show/hide operation
    {
        return;              // just exit.
    }
    if ((buf[0] & 0xf8) == 0xf8) // relative mouse packet header?
    {
        pressed = (buf[0] & 2) >> 1 | (buf[0] & 1) << 1;
        previous = linea_vars.cur_ms_stat & 3;
        if (pressed != previous)
        {
            pressed |= (linea_vars.MOUSE_BT & ~3); // keep additional mouse button states
            linea_vars.MOUSE_BT = pressed;
            user_but(pressed);
            pressed |= ((previous ^ pressed) << 6); // compute which buttons have changed and put deltas in bits 6 & 7
            linea_vars.cur_ms_stat = pressed;
        }

        delta_x = buf[1];
        delta_y = buf[2];
        if (delta_x || delta_y)
        {
            linea_vars.cur_ms_stat |= ~(1<<5);  // Set motion status
            point.x = linea_vars.GCURX + delta_x;
            point.y = linea_vars.GCURY + delta_y;
            scrn_clip(&point);
            /* user_mot takes the proposed x and y as arguments and returns
             * the (possibly modified) coordinates packed into a ULONG with x
             * in the high word and y in the low word -- an arch-neutral
             * convention any C calling convention supports, ARM AAPCS and
             * SysV x86-64 alike. */
            {
                ULONG result = linea_vars.user_mot((WORD)point.x, (WORD)point.y);
                point.x = (LONG)(WORD)HIWORD(result);
                point.y = (LONG)(WORD)LOWORD(result);
            }
            scrn_clip(&point);
            linea_vars.GCURX = point.x;
            linea_vars.GCURY = point.y;
            user_cur(point.x, point.y);         // call user to draw cursor
        }
        else
        {
            linea_vars.cur_ms_stat &= ~(1<<5); // Clear motion status
        }
    }
}

/*
 * _wheel_int - Mouse wheel interrupt routine
 * buf: address of IKBD status packet buffer
 * TODO
 */
void wheel_int(UBYTE *buf)
{
}
