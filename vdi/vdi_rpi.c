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
extern VDIPB local_pb;
static WORD lcl_ptsin[ptsin_size];

/* Standard callig conventions on ARM pass params in registers, so we don't
 * have to code GSX_ENTRY in assembler. */

int GSX_ENTRY(int op, VDIPB* paramblock)
{
    int i;
    /* Make a local copy of the array pointers in the user's parameter block. */
    local_pb.contrl = paramblock->contrl;
    local_pb.intin = paramblock->intin;
    local_pb.intout = paramblock->intout;
    local_pb.ptsout = paramblock->ptsout;
    local_pb.ptsin = lcl_ptsin;

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
 * buf: address of mouse buffer from aciavecs.S
 * TODO
 */
void mouse_int(UBYTE *buf)
{

}

/*
 * _wheel_int - Mouse wheel interrupt routine
 * buf: address of IKBD status packet buffer from aciavecs.S
 * TODO
 */
void wheel_int(UBYTE *buf)
{
}
