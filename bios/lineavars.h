/*
 * lineavars.h - name of linea graphics related variables
 *
 * Copyright (C) 2001-2017 by Authors:
 *
 * Authors:
 *  MAD   Martin Doering
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * Put in this file only the low-mem vars actually used by C code!
 */

#ifndef LINEAVARS_H
#define LINEAVARS_H

#include "portab.h"
#include "font.h"
#include "mform.h"
#include "vdipb.h"

/* Screen related variables */

/*
 * mouse cursor save area
 *
 * NOTE: the lineA version of this only has the first 64 ULONGs,
 * to handle a maximum of 4 video planes.  Writing into area[64]
 * and above when referencing the lineA version will overwrite
 * other lineA variables with unpredictable results.
 */
typedef struct {
        WORD    len;            /* height of saved form */
        UWORD   *addr;          /* screen address of saved form */
        BYTE    stat;           /* save status */
        BYTE    reserved;
        ULONG   area[8*16];     /* handle up to 8 video planes */
} MCS;
/* defines for 'stat' above */
#define MCS_VALID   0x01        /* save area is valid */
#define MCS_LONGS   0x02        /* saved data is in longword format */

extern const BYTE shift_offset[9];  /* pixel to address helper */
extern MCS *mcs_ptr;            /* ptr to mouse cursor save area in use */

struct Vwk_;
struct font_head;

struct _lineavars {
    /* Font related VDI variables */
    WORD angle;                 /* -910 angle */
    WORD beg_angle;             /* -908 start angle */
    const Fonthead *cur_font;   /* -906 current VDI font */
    WORD unused1[23];

    /* The mouse form storage area: */
    Mcdb mouse_cdb;             /* -856 (cdb+0) Mouse hot spot - x coord */
                                /* -854 (cdb+2) Mouse hot spot - y coord */
                                /* -852 (cdb+4) Writing mode for mouse pointer */
                                /* -850 (cdb+6) Mouse background color as pel value */
                                /* -848 (cdb+8) Mouse foreground color as pel value */
                                /* -846 (cdb+10) Storage for mouse mask and cursor */

    /* Extended workstation information */
    WORD INQ_TAB[45];           /* -782 information returned from a _vq_extnd() */
    /* Workstation information */
    WORD DEV_TAB[45];           /* -692 information returned from a _v_opnwk() */

    /* Mouse data */
    WORD GCURX;                 /* -602 mouse X position */
    WORD GCURY;                 /* -600 mouse Y position */
    WORD HIDE_CNT;              /* -598 Number of levels the mouse is hidden */
    WORD MOUSE_BT;              /* -596 mouse button state */

    /* RGB values for colors 0-15 */
    WORD REQ_COL[16][3];        /* -594 48 WORDs of RGB data (color registers) */
    
    /* Workstation PTSOUT information */
    WORD SIZ_TAB[15];           /* -498 size table */

    WORD TERM_CH;               /* -468 pressed key, aciii + scancode */
    WORD chc_mode;              /* -466 input mode choice */
    struct Vwk_ *CUR_WORK;      /* -464 parameter block of current workst. */
    const Fonthead *def_font;   /* -460 default font */
    const Fonthead *font_ring[4]; /* -456 three pointers to sysfonts and NULL */
    WORD font_count;            /* -440 total numbers of fonts in font_ring */
    
    WORD line_cw;               /* -438 Width associated with q_circle data */
    WORD loc_mode;              /* -436 input mode, locator */
    UBYTE unused2[80];          /* some space (input mode???) */
    WORD num_qc_lines;          /* -354 Number of lines making up wide line - not sure if right here */
    
    WORD str_mode;              /* -352 input mode, string */
    WORD val_mode;              /* -350 input mode, valuator */

    BYTE cur_ms_stat;           /* -348 current mouse status */
    BYTE unused3;
    
    WORD disab_cnt;             /* -346 disable depth count. (>0 => disabled) */
    WORD newx;                  /* -344 new mouse x&y position */
    WORD newy;
    BYTE draw_flag;             /* -340 non-zero means draw mouse form on vblank */
    BYTE mouse_flag;            /* -339 non-zero if mouse ints disabled */

    WORD sav_cur_x;             /* -338 save area for cursor cell coords. */
    WORD sav_cur_y;             /* -336 save area for cursor cell coords. */
    void *retsav;               /* -334 I'm not sure if this is right here */

    /* note that this area is only used when v_planes <= 4 */
    struct {
	    WORD save_len;              /* -330 height of saved form */
    	UWORD *save_addr;           /* -328 screen addr of saved form */
	    BYTE save_stat;             /* -324 save status */
    	BYTE save_reserved;
	    ULONG save_area[4*16];      /* -322 form save area */
	} mouse_cursor_save;
	
    /* Timer vectors */
    void (*tim_addr)(int);             /* -66  timer interrupt vector */
    void (*tim_chain)(int);            /* -62  timer interrupt vector save */
    void (*user_but)(WORD status);     /* -58  user button vector */
    void (*user_cur)(WORD x, WORD y);  /* -54  user cursor vector */
    void (*user_mot)(void);            /* -50  user motion vector */

    /* VDI ESC variables */
    
    UWORD v_cel_ht;             /* -46  cell height (width is 8) */
    UWORD v_cel_mx;             /* -44  columns on the screen minus 1 */
    UWORD v_cel_my;             /* -42  rows on the screen minus 1 */
    UWORD v_cel_wr;             /* -40  length (in bytes) of a line of characters */
    WORD v_col_bg;              /* -38  current background color */
    WORD v_col_fg;              /* -36  current foreground color */
    UBYTE *v_cur_ad;            /* -34  current cursor address */
    UWORD v_cur_of;             /* -30  offset from begin of screen */
    UWORD v_cur_cx;             /* -28  current cursor column */
    UWORD v_cur_cy;             /* -26  current cursor row */
    BYTE v_period;              /* -24  cursor blink rate */
    BYTE v_cur_tim;             /* -23  cursor blink timer. */
    const UWORD *v_fnt_ad;      /* -22  pointer to current monospace font */
    UWORD v_fnt_nd;             /* -18  ascii code of last cell in font */
    UWORD v_fnt_st;             /* -16  ascii code of first cell in font */
    UWORD v_fnt_wr;             /* -14  font cell wrap */
    UWORD V_REZ_HZ;             /* -12  horizontal resolution in pixels */
    const UWORD *v_off_ad;      /* -10  pointer to font offset table */
    BYTE v_stat_0;              /* -6   video cell system status (was in words) */
    BYTE unused4;               /*      dummy */
    UWORD V_REZ_VT;             /* -4   vertical resolution in pixels */
    UWORD BYTES_LIN;            /* -2   byte per screen line */


/* =========================================================================== */
/* ==== Normal line-a variables now follow */
/* =========================================================================== */

    UWORD v_planes;             /* +0   number of video planes. */
    UWORD v_lin_wr;             /* +2   number of bytes/video line. */

    VDIPB local_pb;
    /* CONTRL                      +4   ptr to the CONTRL array. */
    /* INTIN                       +8   ptr to the INTIN array. */
    /* PTSIN                       +12  ptr to the PTSIN array. */
    /* INTOUT                      +16  ptr to the INTOUT array. */
    /* PTSOUT                      +20  ptr to the PTSOUT array. */

/* =========================================================================== */
/*      The following 4 variables are accessed by the line-drawing routines */
/*      as an array (to allow post-increment addressing).  They must be contiguous!! */
/* =========================================================================== */

    WORD COLBIT0;               /* colour bit value for plane 0 */
    WORD COLBIT1;               /* colour bit value for plane 1 */
    WORD COLBIT2;               /* colour bit value for plane 2 */
    WORD COLBIT3;               /* colour bit value for plane 3 */

    WORD LSTLIN;                /* 0 => not last line of polyline. */
    WORD LN_MASK;               /* line style mask. */
    WORD WRT_MODE;              /* writing mode. */

    WORD X1;                    /* _X1 coordinate for squares */
    WORD Y1;                    /* _Y1 coordinate for squares */
    WORD X2;                    /* _X2 coordinate for squares */
    WORD Y2;                    /* _Y2 coordinate for squares */
    UWORD *PATPTR;              /* fill pattern pointer */
    UWORD PATMSK;               /* fill pattern "mask" (line count) */
    WORD MFILL;                 /* multi-plane fill flag. (0 => 1 plane) */

    WORD CLIP;                  /* clipping flag. */
    WORD XMINCL;                /* x minimum clipping value. */
    WORD YMINCL;                /* y minimum clipping value. */
    WORD XMAXCL;                /* x maximum clipping value. */
    WORD YMAXCL;                /* y maximum clipping value. */

    WORD XDDA;                  /* accumulator for x DDA */
    UWORD DDAINC;               /* the fraction to be added to the DDA */
    WORD SCALDIR;               /* scale up or down flag. */
    WORD MONO;                  /* non-zero - cur font is monospaced */
    WORD SOURCEX;
    WORD SOURCEY;               /* upper left of character in font file */
    WORD DESTX;
    WORD DESTY;                 /* upper left of destination on screen */
    WORD DELX;
    WORD DELY;                  /* width and height of character */
    const UWORD *FBASE;         /* pointer to font data */
    WORD FWIDTH;                /* width of font form (in bytes) */
    WORD STYLE;                 /* special effects */
    WORD LITEMASK;              /* special effects */
    WORD SKEWMASK;              /* special effects */
    WORD WEIGHT;                /* special effects */
    WORD ROFF;
    WORD LOFF;                  /* skew above and below baseline */
    WORD SCALE;                 /* scale character */
    WORD CHUP;                  /* character rotation vector */
    WORD TEXTFG;                /* text foreground color */
    WORD *SCRTCHP;              /* pointer to base of scratch buffer */
    WORD SCRPT2;                /* large buffer base offset */
    WORD TEXTBG;                /* text background color */
    WORD COPYTRAN;              /* Flag for Copy-raster-form (<>0 = Transparent) */
    WORD (*FILL_ABORT)(void);   /* Address of Routine for testing break out of contourfill function */
};

extern struct _lineavars linea_vars;

#define CONTRL linea_vars.local_pb.contrl
#define INTIN  linea_vars.local_pb.intin
#define PTSIN  linea_vars.local_pb.ptsin
#define INTOUT linea_vars.local_pb.intout
#define PTSOUT linea_vars.local_pb.ptsout

extern void linea_init(void);   /* initialize variables */

#endif /* LINEAVARS_H */
