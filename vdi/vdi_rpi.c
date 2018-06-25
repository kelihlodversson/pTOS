/*
 * entry.S - Front end of the screen driver and mouse stuff
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
#include "string.h"
extern void screen(void);


#define ptsin_size 512          // max. # of elements allowed for PTSIN array
#define ptsin_max  ptsin_size/2 // max. # of coordinate pairs for PTSIN array
#define W_1        2            // byte offset to first element of an array
#define W_3        6            // byte offset to third element of an array



// We copy the param block from the user into a global linea variable
extern VDIPB* local_pb;
static WORD lcl_ptsin[ptsin_size];

/* Standard callig conventions on ARM pass params in registers, so we don't
 * have to code GSX_ENTRY in assembler. */

int GSX_ENTRY(int op, VDIPB* paramblock)
{
    int i;
    /* Make a local copy of the array pointers in the user's parameter block. */
    local_pb->contrl = paramblock->contrl;
    local_pb->intin = paramblock->intin;
    local_pb->intout = paramblock->intout;
    local_pb->ptsout = paramblock->ptsout;
    local_pb->ptsin = lcl_ptsin;

    WORD save_ptsin_count = paramblock->contrl[1];
    WORD save_intin_count = paramblock->contrl[3];

    if (paramblock->contrl[1] <= 0)
    {
        paramblock->contrl[1] = 0;
    }
    else
    {
        if(paramblock->contrl[1] > ptsin_max)
        {
            paramblock->contrl[1] = ptsin_max;
        }
        for (i = 0; i < paramblock->contrl[1]*2; i++)
        {
            lcl_ptsin[i] = paramblock->ptsin[i];
        }
    }
    if (paramblock->contrl[3] <= 0)
    {
        paramblock->contrl[3] = 0;
    }

    /* Call screen which contains all the C routines for the SCREEN DRIVER. */
    screen();

    // Restore ptsin and intin counts to the unsanitized sizes
    paramblock->contrl[1] = save_ptsin_count;
    paramblock->contrl[3] = save_intin_count;

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

void mouse_int(void);
void wheel_int(void);
void mov_cur(WORD x, WORD y);

/*
 * _mouse_int - Mouse interrupt routine
 * TODO
 */
void mouse_int(void)
{

}

/*
 * _wheel_int - Mouse wheel interrupt routine
 * TODO
 */
void wheel_int(void)
{

}

/*
 * mov_cur - moves the mouse cursor to its new location
 *           unless the cursor is currently hidden.
 *
 * Inputs:
 *    r0 = new x-coordinate for mouse cursor
 *    r1 = new y-coordinate for mouse cursor
 *
 * Outputs:        None
 */
void mov_cur(WORD x, WORD y)
{
    if (!HIDE_CNT)
    {
        // TODO: disable interrupts around this
        newx = x;
        newy = y;
        draw_flag = TRUE;
    }
}
