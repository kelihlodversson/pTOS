/*
 * vdi_col.c - VDI color palette functions and tables.
 *
 * Copyright (C) 2005-2016 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "intmath.h"
#include "vdi_defs.h"
#include "string.h"
#include "../bios/machine.h"
#include "xbiosbind.h"
#include "vdi_col.h"
#include "../bios/lineavars.h"
#include "../bios/screen.h"
#include "vdi_backend.h"

#define EXTENDED_PALETTE (CONF_WITH_VIDEL || CONF_WITH_TT_SHIFTER || defined(MACHINE_RPI) \
    || CONF_WITH_VDI_TRUECOLOR32_TEST)

#if EXTENDED_PALETTE
#define MAXCOLOURS  256
#else
#define MAXCOLOURS  16
#endif

/* Some color mapping tables */
WORD MAP_COL[MAXCOLOURS];       /* maps vdi pen -> hardware register */
WORD REV_MAP_COL[MAXCOLOURS];   /* maps hardware register -> vdi pen */

static const WORD MAP_COL_ROM[] =
    { 0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13 };

/*
 * the following factors are used in adjust_mono_values()
 */
#define STE_MONO_FUDGE_FACTOR   0x43
#define ST_MONO_FUDGE_FACTOR    0x8e

#if EXTENDED_PALETTE
/* req_col2 contains the VDI color palette entries 16 - 255 for vq_color().
 * To stay compatible with the line-a variables, only entries > 16 are
 * stored in this array, the first 16 entries are stored in REQ_COL */
static WORD req_col2[240][3];
#endif

/* Initial color palettes */
static const WORD st_palette[16][3] =
{
    { 1000, 1000, 1000 },
    { 0, 0, 0 },
    { 1000, 0, 0 },
    { 0, 1000, 0 },
    { 0, 0, 1000 },
    { 0, 1000, 1000 },
    { 1000, 1000, 0 },
    { 1000, 0, 1000 },
    { 714, 714, 714 },
    { 428, 428, 428 },
    { 1000, 428, 428 },
    { 428, 1000, 428 },
    { 428, 428, 1000 },
    { 428, 1000, 1000 },
    { 1000, 1000, 428 },
    { 1000, 428, 1000 }
};
#if CONF_WITH_TT_SHIFTER
static const WORD tt_palette1[16][3] =
{
    { 1000, 1000, 1000 }, { 0, 0, 0 }, { 1000, 0, 0 }, { 0, 1000, 0 },
    { 0, 0, 1000 }, { 0, 1000, 1000 }, { 1000, 1000, 0 }, { 1000, 0, 1000 },
    { 667, 667, 667 }, { 400, 400, 400 }, { 1000, 600, 600 }, { 600, 1000, 600 },
    { 600, 600, 1000 }, { 600, 1000, 1000 }, { 1000, 1000, 600 }, { 1000, 600, 1000 }
};
static const WORD tt_palette2[240][3] =
{
    { 1000, 1000, 1000 }, { 933, 933, 933 }, { 867, 867, 867 }, { 800, 800, 800 },
    { 733, 733, 733 }, { 667, 667, 667 }, { 600, 600, 600 }, { 533, 533, 533 },
    { 467, 467, 467 }, { 400, 400, 400 }, { 333, 333, 333 }, { 267, 267, 267 },
    { 200, 200, 200 }, { 133, 133, 133 }, { 67, 67, 67 }, { 0, 0, 0 },
    { 1000, 0, 0 }, { 1000, 0, 67 }, { 1000, 0, 133 }, { 1000, 0, 200 },
    { 1000, 0, 267 }, { 1000, 0, 333 }, { 1000, 0, 400 }, { 1000, 0, 467 },
    { 1000, 0, 533 }, { 1000, 0, 600 }, { 1000, 0, 667 }, { 1000, 0, 733 },
    { 1000, 0, 800 }, { 1000, 0, 867 }, { 1000, 0, 933 }, { 1000, 0, 1000 },
    { 933, 0, 1000 }, { 867, 0, 1000 }, { 800, 0, 1000 }, { 733, 0, 1000 },
    { 667, 0, 1000 }, { 600, 0, 1000 }, { 533, 0, 1000 }, { 467, 0, 1000 },
    { 400, 0, 1000 }, { 333, 0, 1000 }, { 267, 0, 1000 }, { 200, 0, 1000 },
    { 133, 0, 1000 }, { 67, 0, 1000 }, { 0, 0, 1000 }, { 0, 67, 1000 },
    { 0, 133, 1000 }, { 0, 200, 1000 }, { 0, 267, 1000 }, { 0, 333, 1000 },
    { 0, 400, 1000 }, { 0, 467, 1000 }, { 0, 533, 1000 }, { 0, 600, 1000 },
    { 0, 667, 1000 }, { 0, 733, 1000 }, { 0, 800, 1000 }, { 0, 867, 1000 },
    { 0, 933, 1000 }, { 0, 1000, 1000 }, { 0, 1000, 933 }, { 0, 1000, 867 },
    { 0, 1000, 800 }, { 0, 1000, 733 }, { 0, 1000, 667 }, { 0, 1000, 600 },
    { 0, 1000, 533 }, { 0, 1000, 467 }, { 0, 1000, 400 }, { 0, 1000, 333 },
    { 0, 1000, 267 }, { 0, 1000, 200 }, { 0, 1000, 133 }, { 0, 1000, 67 },
    { 0, 1000, 0 }, { 67, 1000, 0 }, { 133, 1000, 0 }, { 200, 1000, 0 },
    { 267, 1000, 0 }, { 333, 1000, 0 }, { 400, 1000, 0 }, { 467, 1000, 0 },
    { 533, 1000, 0 }, { 600, 1000, 0 }, { 667, 1000, 0 }, { 733, 1000, 0 },
    { 800, 1000, 0 }, { 867, 1000, 0 }, { 933, 1000, 0 }, { 1000, 1000, 0 },
    { 1000, 933, 0 }, { 1000, 867, 0 }, { 1000, 800, 0 }, { 1000, 733, 0 },
    { 1000, 667, 0 }, { 1000, 600, 0 }, { 1000, 533, 0 }, { 1000, 467, 0 },
    { 1000, 400, 0 }, { 1000, 333, 0 }, { 1000, 267, 0 }, { 1000, 200, 0 },
    { 1000, 133, 0 }, { 1000, 67, 0 }, { 733, 0, 0 }, { 733, 0, 67 },
    { 733, 0, 133 }, { 733, 0, 200 }, { 733, 0, 267 }, { 733, 0, 333 },
    { 733, 0, 400 }, { 733, 0, 467 }, { 733, 0, 533 }, { 733, 0, 600 },
    { 733, 0, 667 }, { 733, 0, 733 }, { 667, 0, 733 }, { 600, 0, 733 },
    { 533, 0, 733 }, { 467, 0, 733 }, { 400, 0, 733 }, { 333, 0, 733 },
    { 267, 0, 733 }, { 200, 0, 733 }, { 133, 0, 733 }, { 67, 0, 733 },
    { 0, 0, 733 }, { 0, 67, 733 }, { 0, 133, 733 }, { 0, 200, 733 },
    { 0, 267, 733 }, { 0, 333, 733 }, { 0, 400, 733 }, { 0, 467, 733 },
    { 0, 533, 733 }, { 0, 600, 733 }, { 0, 667, 733 }, { 0, 733, 733 },
    { 0, 733, 667 }, { 0, 733, 600 }, { 0, 733, 533 }, { 0, 733, 467 },
    { 0, 733, 400 }, { 0, 733, 333 }, { 0, 733, 267 }, { 0, 733, 200 },
    { 0, 733, 133 }, { 0, 733, 67 }, { 0, 733, 0 }, { 67, 733, 0 },
    { 133, 733, 0 }, { 200, 733, 0 }, { 267, 733, 0 }, { 333, 733, 0 },
    { 400, 733, 0 }, { 467, 733, 0 }, { 533, 733, 0 }, { 600, 733, 0 },
    { 667, 733, 0 }, { 733, 733, 0 }, { 733, 667, 0 }, { 733, 600, 0 },
    { 733, 533, 0 }, { 733, 467, 0 }, { 733, 400, 0 }, { 733, 333, 0 },
    { 733, 267, 0 }, { 733, 200, 0 }, { 733, 133, 0 }, { 733, 67, 0 },
    { 467, 0, 0 }, { 467, 0, 67 }, { 467, 0, 133 }, { 467, 0, 200 },
    { 467, 0, 267 }, { 467, 0, 333 }, { 467, 0, 400 }, { 467, 0, 467 },
    { 400, 0, 467 }, { 333, 0, 467 }, { 267, 0, 467 }, { 200, 0, 467 },
    { 133, 0, 467 }, { 67, 0, 467 }, { 0, 0, 467 }, { 0, 67, 467 },
    { 0, 133, 467 }, { 0, 200, 467 }, { 0, 267, 467 }, { 0, 333, 467 },
    { 0, 400, 467 }, { 0, 467, 467 }, { 0, 467, 400 }, { 0, 467, 333 },
    { 0, 467, 267 }, { 0, 467, 200 }, { 0, 467, 133 }, { 0, 467, 67 },
    { 0, 467, 0 }, { 67, 467, 0 }, { 133, 467, 0 }, { 200, 467, 0 },
    { 267, 467, 0 }, { 333, 467, 0 }, { 400, 467, 0 }, { 467, 467, 0 },
    { 467, 400, 0 }, { 467, 333, 0 }, { 467, 267, 0 }, { 467, 200, 0 },
    { 467, 133, 0 }, { 467, 67, 0 }, { 267, 0, 0 }, { 267, 0, 67 },
    { 267, 0, 133 }, { 267, 0, 200 }, { 267, 0, 267 }, { 200, 0, 267 },
    { 133, 0, 267 }, { 67, 0, 267 }, { 0, 0, 267 }, { 0, 67, 267 },
    { 0, 133, 267 }, { 0, 200, 267 }, { 0, 267, 267 }, { 0, 267, 200 },
    { 0, 267, 133 }, { 0, 267, 67 }, { 0, 267, 0 }, { 67, 267, 0 },
    { 133, 267, 0 }, { 200, 267, 0 }, { 267, 267, 0 }, { 267, 200, 0 },
    { 267, 133, 0 }, { 267, 67, 0 }, { 1000, 1000, 1000 }, { 0, 0, 0 }
};
#endif
#if CONF_WITH_VIDEL
static const WORD videl_palette1[16][3] =
{
    { 1000, 1000, 1000 }, { 0, 0, 0 }, { 1000, 0, 0 }, { 0, 1000, 0 },
    { 0, 0, 1000 }, { 0, 1000, 1000 }, { 1000, 1000, 0 }, { 1000, 0, 1000 },
    { 733, 733, 733 }, { 533, 533, 533 }, { 667, 0, 0 }, { 0, 667, 0 },
    { 0, 0, 667 }, { 0, 667, 667 }, { 667, 667, 0 }, { 667, 0, 667 }
};
static const WORD videl_palette2[240][3] =
{
    { 1000, 1000, 1000 }, { 933, 933, 933 }, { 867, 867, 867 }, { 800, 800, 800 },
    { 733, 733, 733 }, { 667, 667, 667 }, { 600, 600, 600 }, { 533, 533, 533 },
    { 467, 467, 467 }, { 400, 400, 400 }, { 333, 333, 333 }, { 267, 267, 267 },
    { 200, 200, 200 }, { 133, 133, 133 }, { 67, 67, 67 }, { 0, 0, 0 },
    { 1000, 0, 0 }, { 1000, 0, 67 }, { 1000, 0, 133 }, { 1000, 0, 200 },
    { 1000, 0, 267 }, { 1000, 0, 333 }, { 1000, 0, 400 }, { 1000, 0, 467 },
    { 1000, 0, 533 }, { 1000, 0, 600 }, { 1000, 0, 667 }, { 1000, 0, 733 },
    { 1000, 0, 800 }, { 1000, 0, 867 }, { 1000, 0, 933 }, { 1000, 0, 1000 },
    { 933, 0, 1000 }, { 867, 0, 1000 }, { 800, 0, 1000 }, { 733, 0, 1000 },
    { 667, 0, 1000 }, { 600, 0, 1000 }, { 533, 0, 1000 }, { 467, 0, 1000 },
    { 400, 0, 1000 }, { 333, 0, 1000 }, { 267, 0, 1000 }, { 200, 0, 1000 },
    { 133, 0, 1000 }, { 67, 0, 1000 }, { 0, 0, 1000 }, { 0, 67, 1000 },
    { 0, 133, 1000 }, { 0, 200, 1000 }, { 0, 267, 1000 }, { 0, 333, 1000 },
    { 0, 400, 1000 }, { 0, 467, 1000 }, { 0, 533, 1000 }, { 0, 600, 1000 },
    { 0, 667, 1000 }, { 0, 733, 1000 }, { 0, 800, 1000 }, { 0, 867, 1000 },
    { 0, 933, 1000 }, { 0, 1000, 1000 }, { 0, 1000, 933 }, { 0, 1000, 867 },
    { 0, 1000, 800 }, { 0, 1000, 733 }, { 0, 1000, 667 }, { 0, 1000, 600 },
    { 0, 1000, 533 }, { 0, 1000, 467 }, { 0, 1000, 400 }, { 0, 1000, 333 },
    { 0, 1000, 267 }, { 0, 1000, 200 }, { 0, 1000, 133 }, { 0, 1000, 67 },
    { 0, 1000, 0 }, { 67, 1000, 0 }, { 133, 1000, 0 }, { 200, 1000, 0 },
    { 267, 1000, 0 }, { 333, 1000, 0 }, { 400, 1000, 0 }, { 467, 1000, 0 },
    { 533, 1000, 0 }, { 600, 1000, 0 }, { 667, 1000, 0 }, { 733, 1000, 0 },
    { 800, 1000, 0 }, { 867, 1000, 0 }, { 933, 1000, 0 }, { 1000, 1000, 0 },
    { 1000, 933, 0 }, { 1000, 867, 0 }, { 1000, 800, 0 }, { 1000, 733, 0 },
    { 1000, 667, 0 }, { 1000, 600, 0 }, { 1000, 533, 0 }, { 1000, 467, 0 },
    { 1000, 400, 0 }, { 1000, 333, 0 }, { 1000, 267, 0 }, { 1000, 200, 0 },
    { 1000, 133, 0 }, { 1000, 67, 0 }, { 733, 0, 0 }, { 733, 0, 67 },
    { 733, 0, 133 }, { 733, 0, 200 }, { 733, 0, 267 }, { 733, 0, 333 },
    { 733, 0, 400 }, { 733, 0, 467 }, { 733, 0, 533 }, { 733, 0, 600 },
    { 733, 0, 667 }, { 733, 0, 733 }, { 667, 0, 733 }, { 600, 0, 733 },
    { 533, 0, 733 }, { 467, 0, 733 }, { 400, 0, 733 }, { 333, 0, 733 },
    { 267, 0, 733 }, { 200, 0, 733 }, { 133, 0, 733 }, { 67, 0, 733 },
    { 0, 0, 733 }, { 0, 67, 733 }, { 0, 133, 733 }, { 0, 200, 733 },
    { 0, 267, 733 }, { 0, 333, 733 }, { 0, 400, 733 }, { 0, 467, 733 },
    { 0, 533, 733 }, { 0, 600, 733 }, { 0, 667, 733 }, { 0, 733, 733 },
    { 0, 733, 667 }, { 0, 733, 600 }, { 0, 733, 533 }, { 0, 733, 467 },
    { 0, 733, 400 }, { 0, 733, 333 }, { 0, 733, 267 }, { 0, 733, 200 },
    { 0, 733, 133 }, { 0, 733, 67 }, { 0, 733, 0 }, { 67, 733, 0 },
    { 133, 733, 0 }, { 200, 733, 0 }, { 267, 733, 0 }, { 333, 733, 0 },
    { 400, 733, 0 }, { 467, 733, 0 }, { 533, 733, 0 }, { 600, 733, 0 },
    { 667, 733, 0 }, { 733, 733, 0 }, { 733, 667, 0 }, { 733, 600, 0 },
    { 733, 533, 0 }, { 733, 467, 0 }, { 733, 400, 0 }, { 733, 333, 0 },
    { 733, 267, 0 }, { 733, 200, 0 }, { 733, 133, 0 }, { 733, 67, 0 },
    { 467, 0, 0 }, { 467, 0, 67 }, { 467, 0, 133 }, { 467, 0, 200 },
    { 467, 0, 267 }, { 467, 0, 333 }, { 467, 0, 400 }, { 467, 0, 467 },
    { 400, 0, 467 }, { 333, 0, 467 }, { 267, 0, 467 }, { 200, 0, 467 },
    { 133, 0, 467 }, { 67, 0, 467 }, { 0, 0, 467 }, { 0, 67, 467 },
    { 0, 133, 467 }, { 0, 200, 467 }, { 0, 267, 467 }, { 0, 333, 467 },
    { 0, 400, 467 }, { 0, 467, 467 }, { 0, 467, 400 }, { 0, 467, 333 },
    { 0, 467, 267 }, { 0, 467, 200 }, { 0, 467, 133 }, { 0, 467, 67 },
    { 0, 467, 0 }, { 67, 467, 0 }, { 133, 467, 0 }, { 200, 467, 0 },
    { 267, 467, 0 }, { 333, 467, 0 }, { 400, 467, 0 }, { 467, 467, 0 },
    { 467, 400, 0 }, { 467, 333, 0 }, { 467, 267, 0 }, { 467, 200, 0 },
    { 467, 133, 0 }, { 467, 67, 0 }, { 267, 0, 0 }, { 267, 0, 67 },
    { 267, 0, 133 }, { 267, 0, 200 }, { 267, 0, 267 }, { 200, 0, 267 },
    { 133, 0, 267 }, { 67, 0, 267 }, { 0, 0, 267 }, { 0, 67, 267 },
    { 0, 133, 267 }, { 0, 200, 267 }, { 0, 267, 267 }, { 0, 267, 200 },
    { 0, 267, 133 }, { 0, 267, 67 }, { 0, 267, 0 }, { 67, 267, 0 },
    { 133, 267, 0 }, { 200, 267, 0 }, { 267, 267, 0 }, { 267, 200, 0 },
    { 267, 133, 0 }, { 267, 67, 0 }, { 1000, 1000, 1000 }, { 0, 0, 0 }
};
#endif


/* Create an ST color value from VDI color */
static int vdi2st(int col)
{
    return (col + 72) / 143;
}


/* Create a VDI color value from ST color
 *
 * we use a lookup table for speed and space savings
 */
#define st2vdi(col) st2vdi_lookup_table[(col)&0x07]
/*
 * this table implements the following calculation, as used by ST TOS:
 *      VDI value = (ST palette hardware value * 1000) / 7
 */
static const WORD st2vdi_lookup_table[8] =
    { 0, 142, 285, 428, 571, 714, 857, 1000 };


#if CONF_WITH_STE_SHIFTER
/* Create an STe color value from VDI color */
static int vdi2ste(int col)
{
    col = (col * 3 + 100) / 200;
    col = ((col & 1) << 3) | (col >> 1);  /* Shift lowest bit to top */

    return col;
}


/* Create a VDI color value from STe color
 *
 * we use a lookup table for speed and space savings
 */
#define ste2vdi(col) ste2vdi_lookup_table[(col)&0x0f]
/*
 * this table implements the following calculation, as used by STe TOS:
 *      VDI value = (STe palette hardware value * 1000 + 7) / 15
 *
 * the sequence of numbers in the table reflects the arrangement of bits
 * in the STe palette hardware: 0 3 2 1
 */
static const WORD ste2vdi_lookup_table[16] =
    { 0, 133, 267, 400, 533, 667, 800, 933,     /* values 0,2 ... 14 */
      67, 200, 333, 467, 600, 733, 867, 1000 }; /* values 1,3 ... 15 */
#endif


#if CONF_WITH_TT_SHIFTER
/* Create a TT color value from VDI color */
static int vdi2tt(int col)
{
    return (col * 15 + 500) / 1000;
}


/* Create a VDI color value from TT color
 *
 * we use a lookup table for speed & space savings
 */
#define tt2vdi(col) tt2vdi_lookup_table[(col)&0x0f]
/*
 * this table implements the following calculation, as used by TT TOS:
 *      VDI value = (TT palette hardware value * 1000 + 7) / 15
 */
static const WORD tt2vdi_lookup_table[16] =
    { 0, 67, 133, 200, 267, 333, 400, 467,
      533, 600, 667, 733, 800, 867, 933, 1000 };


/* Return adjusted VDI color number for TT systems
 *
 * This ensures that we access the right save area
 * (REQ_COL or req_col2) if bank switching is in effect
 */
static WORD adjust_tt_colnum(WORD colnum)
{
    UWORD tt_shifter, rez, bank;

    tt_shifter = EgetShift();
    rez = (tt_shifter>>8) & 0x07;
    switch(rez) {
    case ST_LOW:
    case ST_MEDIUM:
    case TT_MEDIUM:
        bank = tt_shifter & 0x000f;
        colnum += bank * 16;
    }

    return colnum;
}


/* Set an entry in the TT hardware palette
 *
 * TT video hardware has several obscure features which complicate
 * the VDI handler; we try to be TOS3-compatible
 *
 * Input is VDI-style: colnum is VDI pen#, rgb[] entries are 0-1000
 */
static void set_tt_color(WORD colnum, WORD *rgb)
{
    WORD r, g, b;
    WORD hwreg, hwvalue;
    UWORD tt_shifter, rez, bank, mask;

    /*
     * first we determine which h/w palette register to update
     */
    hwreg = MAP_COL[colnum];    /* default, can be modified below */

    tt_shifter = EgetShift();
    rez = (tt_shifter>>8) & 0x07;
    bank = tt_shifter & 0x000f;
    mask = linea_vars.DEV_TAB[13] - 1;

    switch(rez) {
    case ST_LOW:
    case ST_MEDIUM:
    case TT_MEDIUM:
        hwreg &= mask;      /* mask out unwanted bits */
        hwreg += bank * 16; /* allow for bank number */
        break;
    case ST_HIGH:   /* also known as duochrome on a TT */
        /*
         * set register 254 or 255 depending on the VDI pen#
         * and the invert bit in palette register 0
         */
        hwvalue = EsetColor(0,-1);
        if (hwvalue & TT_DUOCHROME_INVERT)
            hwreg = 255 - colnum;
        else hwreg = 254 + colnum;
        break;
    case TT_HIGH:
        return;
    }

    /*
     * then we determine what value to put in it
     */
    r = rgb[0];     /* VDI values */
    g = rgb[1];
    b = rgb[2];

    if (tt_shifter & TT_HYPER_MONO)
    {
        /* we do what TOS3 does: first, derive a weighted value 0-1000
         * based on input RGB values; then, scale it to a value 0-255
         * (which the h/w applies to all 3 guns)
         */
        hwvalue = mul_div(30,r,100) + mul_div(59,g,100) + mul_div(11,b,100);
        hwvalue = mul_div(255,hwvalue,1000);
    }
    else
    {
        hwvalue = (vdi2tt(r) << 8) | (vdi2tt(g) << 4) | vdi2tt(b);
    }
    EsetColor(hwreg, hwvalue);
}


/*
 * Return VDI values for hyper mono mode
 *
 * In hyper mono mode, the original VDI values are not even approximately
 * preserved in the hardware palette registers.  So we do what TOS3 does:
 * we fake the return values based on the previously saved values.
 */
static void get_tt_hyper_mono(WORD colnum,WORD *retval)
{
    WORD *save, i, vditemp;

    save = (colnum < 16) ? linea_vars.REQ_COL[colnum] : req_col2[colnum-16];

    for (i = 0; i < 3; i++, save++, retval++)
    {
        /* first clamp the raw values */
        if (*save > 1000)
            vditemp = 1000;
        else if (*save < 0)
            vditemp = 0;
        else vditemp = *save;

        /*
         * convert to h/w value then back to the equivalent VDI value,
         * just as if we had actually written them in a normal mode
         */
        *retval = tt2vdi(vdi2tt(vditemp));
    }
}


/* Query an entry in the TT hardware palette
 *
 * TT video hardware has several obscure features which complicate
 * the VDI handler; we try to be TOS3-compatible
 *
 * Input colnum is VDI pen#, returned values are nominally 0-1000
 */
static void query_tt_color(WORD colnum,WORD *retval)
{
    WORD hwreg, hwvalue;
    UWORD tt_shifter, rez, bank, mask;

    /*
     * first we determine which h/w palette register to read
     */
    hwreg = MAP_COL[colnum];    /* default, can be modified below */

    tt_shifter = EgetShift();
    rez = (tt_shifter>>8) & 0x07;
    bank = tt_shifter & 0x000f;
    mask = linea_vars.DEV_TAB[13] - 1;

    switch(rez) {
    case ST_LOW:
    case ST_MEDIUM:
    case TT_MEDIUM:
        hwreg &= mask;      /* mask out unwanted bits */
        hwreg += bank * 16; /* allow for bank number */
        break;
    case ST_HIGH:   /* also known as duochrome on a TT */
        /*
         * set register 254 or 255 depending on the VDI pen#
         * and the invert bit in palette register 0
         */
        hwvalue = EsetColor(0,-1);
        if (hwvalue & TT_DUOCHROME_INVERT)
            hwreg = 255 - colnum;
        else hwreg = 254 + colnum;
        break;
    case TT_HIGH:
        retval[0] = retval[1] = retval[2] = colnum ? 0 : 1000;
        return;
    }

    if (tt_shifter & TT_HYPER_MONO)
    {
        get_tt_hyper_mono(colnum,retval);
    }
    else
    {
        hwvalue = EsetColor(hwreg,-1);
        retval[0] = tt2vdi(hwvalue >> 8);
        retval[1] = tt2vdi(hwvalue >> 4);
        retval[2] = tt2vdi(hwvalue);
    }
}
#endif


#if CONF_WITH_VIDEL
/* Create videl colour value from VDI colour */
static LONG vdi2videl(WORD col)
{
    return ((LONG)col * 255 + 500) / 1000;              /* scale 1000 -> 255 */
}


/* Create VDI colour value from videl colour */
static WORD videl2vdi(LONG col)
{
    return (WORD)(((col & 0xff) * 1000 + 128) / 255);   /* scale 255 -> 1000 */
}
#endif


/*
 * Monochrome screens get special handling because they don't use the
 * regular palette setup; instead, bit 0 of h/w palette register 0
 * controls whether the screen background is white (bit0=0) or black
 * (bit0=1).
 *
 * Also, from a VDI standpoint, you would expect that setting pen 0 to
 * (1000,1000,1000) would get white and setting pen 0 to (0,0,0) would
 * get black; but what should happen with intermediate values?
 *
 * Atari TOS handles this as follows:
 * 1. each RGB value less than a fudge factor F is converted to 0 and
 *    each value greater than or equal to 1000-F is converted to 1000
 * 2. if the sum of the values is neither 0 nor 3000, nothing is done
 * 3. if asked to change pen 1, it sets pen 0 to white, irrespective of
 *    RGB values
 * 4. if changing pen 0, it respects the RGB values
 *
 * We do the same ...
 */
static WORD adjust_mono_values(WORD colnum,WORD *rgb,WORD fudge)
{
    WORD i, sum;

    for (i = 0, sum = 0; i < 3; i++)
    {
        if (rgb[i] < fudge)
            rgb[i] = 0;
        else if (rgb[i] >= (1000-fudge))
            rgb[i] = 1000;
        sum += rgb[i];
    }

    if ((sum > 0) && (sum < 3000))
        return -1;      /* 'do nothing' indicator */

    if (colnum == 1)
    {
        colnum = 0;     /* set pen 0 to white */
        rgb[0] = rgb[1] = rgb[2] = 1000;
    }

    return colnum;
}


/*
 * planar_set_*_color/planar_get_*_color - vdi_backend_ops.set_color/
 * get_color for one specific planar hardware palette family (issue #173).
 * Each is what vdi_backend_select() patches into planar_backend_ops's
 * set_color/get_color slots for the selected family (see
 * vdi_backend_planar.c/vdi_backend.c), selected once per workstation open
 * via SCREEN_MODE_DESC.shifter -> vdi_backend_select() (see
 * planar_mode_desc(), bios/screen.c) rather than re-testing has_videl/
 * has_tt_shifter/has_ste_shifter on every vs_color()/vq_color() call the way
 * the pre-#173 set_color()/planar_get_color() did.
 *
 * vwk is unused -- unlike the truecolor backend's per-workstation pseudo-
 * palette, this backend writes real hardware colour registers, which are
 * necessarily shared across every workstation. pen is the raw VDI pen
 * number; each resolves it to a hardware register through MAP_COL[] itself,
 * matching vdi_truecolor_set_color()/vdi_truecolor_get_color()'s own
 * MAP_COL[]-mapped index convention (see screen_set_color()/
 * screen_get_color() below).
 */
#if CONF_WITH_VIDEL
void planar_set_videl_color(Vwk *vwk, WORD pen, WORD *rgb)
{
    WORD hwreg = MAP_COL[pen];
    LONG videlrgb;

    (void)vwk;
    videlrgb = (vdi2videl(rgb[0]) << 16) | (vdi2videl(rgb[1]) << 8) | vdi2videl(rgb[2]);
    VsetRGB(hwreg,1,(LONG)&videlrgb);
}

void planar_get_videl_color(const Vwk *vwk, WORD pen, WORD *rgb)
{
    WORD hwreg = MAP_COL[pen];
    LONG videlrgb;

    (void)vwk;
    VgetRGB(hwreg,1,(LONG)&videlrgb);
    rgb[0] = videl2vdi(videlrgb >> 16);
    rgb[1] = videl2vdi(videlrgb >> 8);
    rgb[2] = videl2vdi(videlrgb);
}
#endif

#if CONF_WITH_TT_SHIFTER
void planar_set_tt_color(Vwk *vwk, WORD pen, WORD *rgb)
{
    (void)vwk;
    set_tt_color(pen, rgb);
}

void planar_get_tt_color(const Vwk *vwk, WORD pen, WORD *rgb)
{
    (void)vwk;
    query_tt_color(pen, rgb);
}
#endif

#if CONF_WITH_STE_SHIFTER
void planar_set_ste_color(Vwk *vwk, WORD pen, WORD *rgb)
{
    WORD r, g, b;
    WORD hwreg = MAP_COL[pen];

    (void)vwk;
    r = rgb[0];
    g = rgb[1];
    b = rgb[2];

    if (linea_vars.v_planes == 1)  /* special handling for monochrome screens */
    {
        hwreg = adjust_mono_values(hwreg, rgb, STE_MONO_FUDGE_FACTOR);  /* may update rgb[] */
        if (hwreg < 0)                          /* 'do nothing' */
            return;
        r = rgb[0];
        g = rgb[1];
        b = rgb[2];
    }

    Setcolor(hwreg, (vdi2ste(r) << 8) | (vdi2ste(g) << 4) | vdi2ste(b));
}

void planar_get_ste_color(const Vwk *vwk, WORD pen, WORD *rgb)
{
    WORD hwreg = MAP_COL[pen];
    WORD c;

    (void)vwk;
    c = Setcolor(hwreg, -1);
    rgb[0] = ste2vdi(c >> 8);
    rgb[1] = ste2vdi(c >> 4);
    rgb[2] = ste2vdi(c);
}
#endif

/* plain ST shifter -- the unconditional fallback family */
void planar_set_st_color(Vwk *vwk, WORD pen, WORD *rgb)
{
    WORD r, g, b;
    WORD hwreg = MAP_COL[pen];

    (void)vwk;
    r = rgb[0];
    g = rgb[1];
    b = rgb[2];

    if (linea_vars.v_planes == 1)  /* special handling for monochrome screens */
    {
        hwreg = adjust_mono_values(hwreg, rgb, ST_MONO_FUDGE_FACTOR);  /* may update rgb[] */
        if (hwreg < 0)                          /* 'do nothing' */
            return;
        r = rgb[0];
        g = rgb[1];
        b = rgb[2];
    }

    Setcolor(hwreg, (vdi2st(r) << 8) | (vdi2st(g) << 4) | vdi2st(b));
}

void planar_get_st_color(const Vwk *vwk, WORD pen, WORD *rgb)
{
    WORD hwreg = MAP_COL[pen];
    WORD c;

    (void)vwk;
    c = Setcolor(hwreg, -1);
    rgb[0] = st2vdi(c >> 8);
    rgb[1] = st2vdi(c >> 4);
    rgb[2] = st2vdi(c);
}

#if CONF_WITH_TT_SHIFTER
/*
 * hw_adjust_colnum - resolved-once counterpart (issue #173, secondary scope
 * item) to the has_tt_shifter check vdi_vs_color()/vdi_vq_color() used to
 * repeat on every call before adjust_tt_colnum() (see above): identity on
 * every non-TT family, adjust_tt_colnum itself once TT is confirmed present.
 *
 * Needed in every build with CONF_WITH_TT_SHIFTER, regardless of dispatch
 * state: the bank-adjusted colnum it produces feeds REQ_COL/req_col2
 * storage in vdi_vs_color()/vdi_vq_color(), bookkeeping that is shared
 * across backends, not planar hardware I/O -- unlike hw_set_color/
 * hw_get_color below, it has no dispatch-table equivalent to defer to.
 *
 * No initializer, like hw_set_color/hw_get_color below: see the comment
 * there for why these need .bss, not .data.
 *
 * hw_adjust_colnum's call sites (vdi_vs_color()/vdi_vq_color()) dispatch
 * through it unconditionally, with no "has it been resolved yet" check:
 * every reachable path to a VDI opcode handler runs through vdi_v_opnwk()
 * first, which calls init_colors() (and so select_colnum_adjust()) before
 * any workstation exists to make such a call on. A lazy self-init guard
 * here would just be defending against a call order this codebase's
 * control flow doesn't allow -- the same per-call runtime check issue
 * #173 exists to remove, reintroduced for a case that can't happen.
 */
static WORD identity_colnum(WORD colnum)
{
    return colnum;
}

static WORD (*hw_adjust_colnum)(WORD colnum);

static void select_colnum_adjust(void)
{
    hw_adjust_colnum = has_tt_shifter ? adjust_tt_colnum : identity_colnum;
}
#endif

#if !CONF_WITH_VDI_BACKEND_DISPATCH && !CONF_WITH_VDI_BACKEND_TRUECOLOR
/*
 * planar_set_color/planar_get_color - the direct-call entry points
 * screen_set_color()/screen_get_color() use when neither dispatch nor the
 * truecolor backend is built in (issue #171): a pure single-planar-renderer
 * build has no SCREEN_MODE_DESC/vdi_backend_select() step at all (see
 * vdi_v_opnwk(), vdi_control.c), so there is no descriptor-driven table to
 * pick the right planar_*_color family from the way vdi_backend_select()
 * does for a dispatch build (issue #173). These resolve the same
 * has_videl/has_tt_shifter/has_ste_shifter family choice once instead,
 * cached in hw_set_color/hw_get_color, and call straight through to the
 * same per-family functions above -- so there is exactly one implementation
 * of each family's palette I/O, just two different "pick it once"
 * mechanisms for the two build configurations.
 *
 * This whole block -- statics, select_color_family() and the two entry
 * points -- only exists in that one configuration: a dispatch build always
 * reaches palette I/O through backend->set_color/get_color (planar_backend_
 * ops's patched set_color/get_color slots for a planar screen, never
 * through here -- see vdi_backend_planar.c/vdi_backend.c), and a
 * truecolor-only build never touches planar code at all. Compiling this in
 * for either would be exactly the dispatch overhead that can only ever
 * resolve one way that issue #138 already excludes elsewhere (see
 * vdi_backend.h).
 *
 * hw_set_color/hw_get_color below are deliberately declared with no
 * initializer, so they zero-init into .bss instead of .data: on the m68k
 * targets, .data shares emutos.ld's read-only ROM region with .text (see
 * its FIXME comment, and the "DATA segment is not empty" check in the
 * top-level Makefile), so a non-zero static initializer here would place
 * them somewhere select_color_family()'s writes below could silently fail
 * on real hardware -- these exist precisely to be written at runtime, so
 * they must live in .bss, not .data.
 */
static void (*hw_set_color)(Vwk *vwk, WORD pen, WORD *rgb);
static void (*hw_get_color)(const Vwk *vwk, WORD pen, WORD *rgb);

/*
 * Resolves hw_set_color/hw_get_color once (issue #173). Called from
 * init_colors(), which itself runs once per physical workstation open,
 * before any palette read/write -- has_videl/has_tt_shifter/has_ste_shifter
 * are boot-time constants (see detect_video(), bios/machine.c), so this
 * mirrors, for this build configuration, exactly what planar_mode_desc()
 * computes into SCREEN_MODE_DESC.shifter for a dispatch build.
 * planar_set_color()/planar_get_color() below trust that init_colors() has
 * already run and dispatch through hw_set_color/hw_get_color
 * unconditionally -- no NULL check, which would just be the per-call
 * runtime test this issue removes, re-added one level down.
 */
static void select_color_family(void)
{
#if CONF_WITH_VIDEL
    if (has_videl)
    {
        hw_set_color = planar_set_videl_color;
        hw_get_color = planar_get_videl_color;
        return;
    }
#endif
#if CONF_WITH_TT_SHIFTER
    if (has_tt_shifter)
    {
        hw_set_color = planar_set_tt_color;
        hw_get_color = planar_get_tt_color;
        return;
    }
#endif
#if CONF_WITH_STE_SHIFTER
    if (has_ste_shifter)
    {
        hw_set_color = planar_set_ste_color;
        hw_get_color = planar_get_ste_color;
        return;
    }
#endif
    hw_set_color = planar_set_st_color;
    hw_get_color = planar_get_st_color;
}

void planar_set_color(Vwk *vwk, WORD pen, WORD *rgb)
{
    hw_set_color(vwk, pen, rgb);
}

void planar_get_color(const Vwk *vwk, WORD pen, WORD *rgb)
{
    hw_get_color(vwk, pen, rgb);
}
#endif /* !CONF_WITH_VDI_BACKEND_DISPATCH && !CONF_WITH_VDI_BACKEND_TRUECOLOR */

/*
 * screen_set_color/screen_get_color - dispatch a palette write/read to the
 * current screen's backend (issue #171), the same three-way pattern as
 * get_start_addr() in vdi_misc.c: through the ops table when more than one
 * renderer is built in, or straight to the one enabled renderer's function
 * otherwise, so a single-renderer build pays nothing for dispatch that can
 * only ever resolve one way.
 */
static void screen_set_color(Vwk *vwk, WORD pen, WORD *rgb)
{
#if CONF_WITH_VDI_BACKEND_DISPATCH
    const vdi_backend_ops *backend = vdi_screen_backend();

    /* see the comment in get_start_addr() (vdi_misc.c) */
    if (!backend)
        return;
    backend->set_color(vwk, pen, rgb);
#elif CONF_WITH_VDI_BACKEND_TRUECOLOR
    /* see the comment in get_start_addr() (vdi_misc.c) */
    vdi_truecolor_set_color(vwk, MAP_COL[pen], rgb[0], rgb[1], rgb[2]);
#else
    /* see the comment in get_start_addr() (vdi_misc.c) */
    planar_set_color(vwk, pen, rgb);
#endif
}

static void screen_get_color(const Vwk *vwk, WORD pen, WORD *rgb)
{
#if CONF_WITH_VDI_BACKEND_DISPATCH
    const vdi_backend_ops *backend = vdi_screen_backend();

    /* see the comment in get_start_addr() (vdi_misc.c) */
    if (!backend)
    {
        rgb[0] = rgb[1] = rgb[2] = 0;
        return;
    }
    backend->get_color(vwk, pen, rgb);
#elif CONF_WITH_VDI_BACKEND_TRUECOLOR
    /* see the comment in get_start_addr() (vdi_misc.c) */
    vdi_truecolor_get_color(vwk, MAP_COL[pen], &rgb[0], &rgb[1], &rgb[2]);
#else
    /* see the comment in get_start_addr() (vdi_misc.c) */
    planar_get_color(vwk, pen, rgb);
#endif
}


/*
 * vdi_vs_color - set color index table
 */
void vdi_vs_color(Vwk *vwk)
{
    WORD colnum, i;
    WORD *intin, rgb[3], *rgbptr;

    colnum = INTIN[0];

    /* Check for valid color index */
    if (colnum < 0 || colnum >= linea_vars.DEV_TAB[13])
    {
        /* It was out of range */
        return;
    }

#if CONF_WITH_TT_SHIFTER
    colnum = hw_adjust_colnum(colnum);  /* handles palette bank issues (issue #173) */
#endif

#if CONF_WITH_VDI_BACKEND_TRUECOLOR
    if (vdi_screen_is_truecolor())
    {
        /*
         * Per-workstation "requested" state (issue #89), mirroring
         * upstream's VwkExt::req_col: unlike the global REQ_COL/req_col2
         * below, storing into vwk->tc_req_col here means a vs_color() on
         * one workstation cannot leak into another workstation's
         * vq_color(pen,0) answer.
         *
         * Indexed by INTIN[0], the pen as passed in -- not colnum, which
         * may have been bank-adjusted above -- since tc_req_col (unlike
         * REQ_COL/req_col2) is documented as plain pen-indexed. vq_color()
         * reads it back by INTIN[0] too (see below), so a bank adjustment
         * here would desync the two. This is a genuine backend-specific
         * storage-layout decision (like cpy_raster()'s packed-MFDB-layout
         * branches in vdi_raster.c), not a primitive, so it stays inline
         * here rather than moving into screen_set_color() below.
         */
        for (i = 0, intin = INTIN+1, rgbptr = rgb; i < 3; i++, intin++, rgbptr++)
        {
            vwk->tc_req_col[INTIN[0]][i] = *intin;
            if (*intin > 1000)
                *rgbptr = 1000;
            else if (*intin < 0)
                *rgbptr = 0;
            else *rgbptr = *intin;
        }
    }
    else
#endif
    {
        /*
         * Copy raw values to the "requested colour" arrays, then clamp
         * them to 0-1000 before writing the actual hardware colour below.
         */
        for (i = 0, intin = INTIN+1, rgbptr = rgb; i < 3; i++, intin++, rgbptr++)
        {
            if (colnum < 16)
                linea_vars.REQ_COL[colnum][i] = *intin;
#if EXTENDED_PALETTE
            else
                req_col2[colnum-16][i] = *intin;
#endif
            if (*intin > 1000)
                *rgbptr = 1000;
            else if (*intin < 0)
                *rgbptr = 0;
            else *rgbptr = *intin;
        }
    }

    /*
     * Write the actual hardware/backend palette entry, dispatched through
     * the VDI backend ops table (issue #171) the same way the other VDI
     * drawing primitives are (see vdi_backend.h). Keyed by INTIN[0], the
     * raw pen as passed in -- not colnum above, which may have been
     * bank-adjusted for TT REQ_COL storage -- matching vdi_vq_color()'s
     * read below.
     */
    screen_set_color(vwk, INTIN[0], rgb);
}


/* Set the default palette etc. */
void init_colors(void)
{
    int i;

#if CONF_WITH_TT_SHIFTER
    /* Resolve hw_adjust_colnum (issue #173) -- needed by vdi_vs_color()/
     * vdi_vq_color() regardless of which backend is active, see the
     * comment on select_colnum_adjust() above. */
    select_colnum_adjust();
#endif

#if !CONF_WITH_VDI_BACKEND_DISPATCH && !CONF_WITH_VDI_BACKEND_TRUECOLOR
    /*
     * Resolve which hardware palette family drives planar_set_color()/
     * planar_get_color() (issue #173), before anything below reads or
     * writes a hardware colour register through them.
     */
    select_color_family();
#endif

    /* set up palette */
    memcpy(linea_vars.REQ_COL, st_palette, sizeof(st_palette));    /* use ST as default */

#if CONF_WITH_VIDEL
    if (has_videl)
    {
        memcpy(linea_vars.REQ_COL, videl_palette1, sizeof(videl_palette1));
        memcpy(req_col2, videl_palette2, sizeof(videl_palette2));
    }
    else
#endif
#if CONF_WITH_TT_SHIFTER
    if (has_tt_shifter)
    {
        memcpy(linea_vars.REQ_COL, tt_palette1, sizeof(tt_palette1));
        memcpy(req_col2, tt_palette2, sizeof(tt_palette2));
    }
    else
#endif
    {
        /* Nothing */
    }

    /* set up vdi pen -> hardware colour register mapping */
    memcpy(MAP_COL, MAP_COL_ROM, sizeof(MAP_COL_ROM));
    MAP_COL[1] = linea_vars.DEV_TAB[13] - 1;   /* pen 1 varies according to # colours available */

#if EXTENDED_PALETTE
    for (i = 16; i < MAXCOLOURS-1; i++)
        MAP_COL[i] = i;
    MAP_COL[i] = 15;
#endif

    /* set up reverse mapping (hardware colour register -> vdi pen) */
    for (i = 0; i < linea_vars.DEV_TAB[13]; i++)
        REV_MAP_COL[MAP_COL[i]] = i;

    /*
     * Now initialise the hardware -- but only if the active backend is
     * planar: the truecolor backend has no shared hardware palette to
     * preload here (RGB565 packed pixels *are* the colour), and seeds each
     * workstation's own pseudo-palette separately, from
     * vdi_truecolor_init_palette() (init_wk(), vdi_control.c) instead --
     * see the vdi_screen_is_truecolor() branches above.
     *
     * Pre-#173, this unconditionally called the single planar-only
     * set_color(), even in what's now a dispatch build, so a dispatch
     * build's planar hardware always got preloaded here regardless of
     * which backend ended up active for a given screen. set_hw_color below
     * preserves that: hw_set_color directly when planar is the only
     * renderer (the common case, and the only one where it's declared --
     * see above), or the selected backend's own set_color when dispatch
     * means hw_set_color doesn't exist -- vwk is unused by every
     * planar_set_*_color() (see above), so passing NULL here is safe as
     * long as the active backend actually is planar.
     */
    {
#if CONF_WITH_VDI_BACKEND_DISPATCH
        /*
         * vdi_screen_backend() can return NULL (see its comment in
         * vdi_backend.h -- can't happen for any of this codebase's drivers
         * today, but every other call site still guards it, so this does
         * too). Truecolor backends keep per-workstation pseudo-palettes, so
         * they cannot be preloaded through this NULL-workstation path.
         */
        const vdi_backend_ops *const backend = vdi_screen_backend();
        void (*const set_hw_color)(Vwk *vwk, WORD pen, WORD *rgb) =
            (backend && !vdi_screen_is_truecolor()) ? backend->set_color : NULL;
#elif CONF_WITH_VDI_BACKEND_TRUECOLOR
        void (*const set_hw_color)(Vwk *vwk, WORD pen, WORD *rgb) = NULL;
#else
        void (*const set_hw_color)(Vwk *vwk, WORD pen, WORD *rgb) = hw_set_color;
#endif

        if (set_hw_color)
        {
            for (i = 0; i < linea_vars.DEV_TAB[13]; i++)
            {
                if (i < 16)
                    set_hw_color(NULL, i, linea_vars.REQ_COL[i]);
#if EXTENDED_PALETTE
                else
                    set_hw_color(NULL, i, req_col2[i-16]);
#endif
            }
        }
    }

#ifdef HATARI_DUOCHROME_WORKAROUND
    /*
     * The following is a workaround for a bug in Hatari v1.9 and earlier.
     * It is disabled by default, and should be removed once the Hatari
     * bug is fixed.
     *
     * In Duochrome mode (ST high on a TT), Hatari uses palette register
     * 0 as the background colour and register 255 as the foreground
     * colour; this is not how real hardware behaves.  However, there
     * was a compensating bug in EmuTOS that set register 0 to white and
     * register 255 to black, so everything displayed OK.
     *
     * On real hardware, register 0 contains the inversion bit (bit 1),
     * and the foreground/background colours are in registers 254/255.
     * For both TOS3 and EmuTOS, the initial value for VDI pens 0/254/255
     * are white/white/black for all resolutions, which causes hardware
     * registers 0/254/255 to be set to 0x0fff/0x0fff/0x000.  Without any
     * compensation, this would cause problems when switching to duochrome
     * mode: since the inversion bit in register 0 is set, the display
     * would show as white on black.
     *
     * Since it's desirable for other reasons to leave register 0 as
     * white, TOS3 (and EmuTOS) compensate as follows: if the inversion
     * bit is set, registers 254/255 are set to 0x0000/0x0fff.  This
     * produces the correct black on white display on real hardware, but
     * a display of white on white under Hatari (both EmuTOS and TOS3).
     *
     * The following workaround preserves the inversion bit, but sets a
     * value of "almost black" in register 0.  The output will be visible
     * on both Hatari and real TT hardware, but Hatari will display black
     * on white, while real hardware displays white on black.  Another
     * consequence of this workaround is that a vq_color() for the actual
     * value of pen 0 will return an unexpected result.
     */
#if CONF_WITH_TT_SHIFTER
    if (has_tt_shifter && (sshiftmod == ST_HIGH))
        EsetColor(0,0x0002);
#endif
#endif
}


/*
 * vdi_vq_color - query color index table
 */
void vdi_vq_color(Vwk *vwk)
{
    WORD colnum;

    colnum = INTIN[0];

    INTOUT[1] = INTOUT[2] = INTOUT[3] = 0;  /* Default values */
    CONTRL->nintout = 4;

    /* Check for valid color index */
    if (colnum < 0 || colnum >= linea_vars.DEV_TAB[13])
    {
        /* It was out of range */
        INTOUT[0] = -1;
        return;
    }
    INTOUT[0] = colnum;

#if CONF_WITH_TT_SHIFTER
    colnum = hw_adjust_colnum(colnum);  /* handles palette bank issues (issue #173) */
#endif

    if (INTIN[1] == 0)  /* return last-requested value */
    {
#if CONF_WITH_VDI_BACKEND_TRUECOLOR
        if (vdi_screen_is_truecolor())
        {
            /*
             * Per-workstation state (issue #89): unlike the global
             * REQ_COL/req_col2 below, indexed by INTIN[0] rather than
             * colnum, matching how vdi_vs_color() stores it -- colnum may
             * have been bank-adjusted above, but tc_req_col is plain
             * pen-indexed. Same backend-specific storage-layout exception
             * as the mirror comment in vdi_vs_color() above.
             */
            INTOUT[1] = vwk->tc_req_col[INTIN[0]][0];
            INTOUT[2] = vwk->tc_req_col[INTIN[0]][1];
            INTOUT[3] = vwk->tc_req_col[INTIN[0]][2];
        }
        else
#endif
        if (colnum < 16)
        {
            INTOUT[1] = linea_vars.REQ_COL[colnum][0];
            INTOUT[2] = linea_vars.REQ_COL[colnum][1];
            INTOUT[3] = linea_vars.REQ_COL[colnum][2];
        }
#if EXTENDED_PALETTE
        else
        {
            INTOUT[1] = req_col2[colnum-16][0];
            INTOUT[2] = req_col2[colnum-16][1];
            INTOUT[3] = req_col2[colnum-16][2];
        }
#endif
        return;
    }

    /*
     * Return the actual current value, dispatched through the VDI backend
     * ops table (issue #171) the same way vdi_vs_color() writes it above.
     * Keyed by INTIN[0], the raw pen, not colnum (which may have been
     * bank-adjusted above for the REQ_COL lookup only).
     */
    {
        WORD rgb[3];

        screen_get_color(vwk, INTIN[0], rgb);
        INTOUT[1] = rgb[0];
        INTOUT[2] = rgb[1];
        INTOUT[3] = rgb[2];
    }
}
