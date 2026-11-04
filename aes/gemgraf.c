/*      GEMGRAF.C       04/11/84 - 09/17/85     Lee Lorenzen            */
/*      merge High C vers. w. 2.2               8/21/87         mdf     */
/*      fix gr_gicon null text                  11/18/87        mdf     */

/*
*       Copyright 1999, Caldera Thin Clients, Inc.
*                 2002-2025 The EmuTOS development team
*
*       This software is licenced under the GNU Public License.
*       Please see LICENSE.TXT for further information.
*
*                  Historical Copyright
*       -------------------------------------------------------------
*       GEM Application Environment Services              Version 2.3
*       Serial No.  XXXX-0000-654321              All Rights Reserved
*       Copyright (C) 1987                      Digital Research Inc.
*       -------------------------------------------------------------
*/

/* #define ENABLE_KDEBUG */

#include "emutos.h"
#include "obdefs.h"
#include "aesdefs.h"
#include "aesext.h"
#include "intmath.h"
#include "funcdef.h"
#include "gemdos.h"

#include "gemgraf.h"
#include "gemgsxif.h"
#include "optimopt.h"
#include "gsx2.h"
#include "gemrender.h"
#include "rectfunc.h"


GLOBAL WORD     gl_width;       /* screen width */
GLOBAL WORD     gl_height;      /* screen height */

GLOBAL WORD     gl_wchar;       /* width of character cell (normal font) */
GLOBAL WORD     gl_hchar;       /* height of character cell (normal font) */

GLOBAL WORD     gl_wschar;      /* width of character cell (small font) */
GLOBAL WORD     gl_hschar;      /* height of character cell (small font) */

GLOBAL WORD     gl_wbox;        /* box width */
GLOBAL WORD     gl_hbox;        /* box height */

GLOBAL GRECT    gl_clip;        /* global clipping rectangle */

GLOBAL WORD     gl_nplanes;     /* number of bit planes */
GLOBAL WORD     gl_handle;      /* physical workstation handle */

GLOBAL FDB      gl_src;
GLOBAL FDB      gl_dst;

GLOBAL WS       gl_ws;          /* work_out[] for physical workstation */
GLOBAL VDICONTROL contrl;
GLOBAL WORD     intin[128];
GLOBAL WORD     ptsin[20];

GLOBAL GRECT    gl_rscreen;     /* the entire screen */
GLOBAL GRECT    gl_rfull;       /* the screen except the menu bar */
GLOBAL GRECT    gl_rzero;       /* 0,0,0,0 */
GLOBAL GRECT    gl_rcenter;     /* a box centered in the 'gl_rfull' area */
GLOBAL GRECT    gl_rmenu;       /* the menu bar */


/*
 * the following are used to save the currently-set values for
 * various VDI attributes, in order to save unnecessary VDI calls
 */
WORD            gl_mode;        /* text writing mode (vswr_mode) */
WORD            gl_tcolor;      /* text colour (vst_color) */
WORD            gl_lcolor;      /* line colour (vsl_color) */
WORD            gl_fis;         /* interior type (vsf_interior) */
WORD            gl_patt;        /* style of fill pattern (vsf_style) */
WORD            gl_font;        /* font type (IBM/SMALL) for v_gtext */

WORD            gl_wptschar;    /* width of character (normal font) */
WORD            gl_hptschar;    /* height of character (normal font) */

WORD            gl_wsptschar;   /* width of character (small font) */
WORD            gl_hsptschar;   /* height of character (small font) */


/*
 *  Routine to draw a certain number of points in a polyline relative
 *  to a given x,y offset
 */
void gsx_pline(WORD offx, WORD offy, WORD cnt, const Point *pts)
{
    WORD    i, j;

    for (i = 0; i < cnt; i++)
    {
        j = i * 2;
        ptsin[j] = offx + pts[i].x;
        ptsin[j+1] = offy + pts[i].y;
    }

    gsx_xline(cnt, ptsin);
}


/*
 *  Routine to blit to a screen area
 */
void gsx_blt(void *saddr, WORD sx, WORD sy,
             WORD dx, WORD dy, WORD w, WORD h,
             WORD rule, WORD fgcolor, WORD bgcolor)
{
    WORD pxyarray[8];

    gsx_fix(&gl_src, (void *)saddr, w/8, h);
    gsx_fix_screen(&gl_dst);

    gsx_moff();
    pxyarray[0] = sx;
    pxyarray[1] = sy;
    pxyarray[2] = sx + w - 1;
    pxyarray[3] = sy + h - 1;
    pxyarray[4] = dx;
    pxyarray[5] = dy;
    pxyarray[6] = dx + w - 1;
    pxyarray[7] = dy + h - 1 ;
    if (fgcolor == -1)
        vro_cpyfm(rule, pxyarray, &gl_src, &gl_dst);
    else
        vrt_cpyfm(rule, pxyarray, &gl_src, &gl_dst, fgcolor, bgcolor);
    gsx_mon();
}


/*
 *  Routine to blit around something on the screen
 */
void bb_screen(WORD scsx, WORD scsy, WORD scdx, WORD scdy,
               WORD scw, WORD sch)
{
    gsx_blt(NULL, scsx, scsy,
            scdx, scdy,
            scw, sch, S_ONLY, -1, -1);
}


/*
 *  Routine to transform a mono standard form to device specific form, in place
 */
void gsx_trans(void *addr, UWORD wb, UWORD h)
{
    gsx_fix(&gl_src, addr, wb, h);
    gl_src.fd_stand = TRUE;

    gsx_fix(&gl_dst, addr, wb, h);
    vrn_trnfm(&gl_src, &gl_dst);
}


/*
 *  Routine to initialize all the global variables dealing with
 *  a particular workstation open
 */
void gsx_start(void)
{
    WORD dummy;

    /* reset variables to force initial VDI calls */
    gl_mode = gl_tcolor = gl_lcolor = -1;
    gl_fis = gl_patt = gl_font = -1;

    gl_clip.g_x = 0;
    gl_clip.g_y = 0;
    gl_width = gl_clip.g_w = gl_ws.ws_xres + 1;
    gl_height = gl_clip.g_h = gl_ws.ws_yres + 1;
    gl_nplanes = gsx_nplanes();

    KINFO(("VDI video mode = %dx%d %d-%s\n", gl_width, gl_height, gl_nplanes,
        (gl_nplanes < 16 ? "plane" : "bit")));

    /*
     * replacement VDIs (e.g. NVDI) may support more than two font sizes.
     * we use the current font height for the big (IBM) desktop font,
     * and the minimum font height for the small (SMALL) desktop font.
     */
    gsx_textsize(&gl_wptschar, &gl_hptschar, &gl_wchar, &gl_hchar);
    gl_ws.ws_chmaxh = gl_hptschar;  /* save the IBM font size here */
    vst_height(gl_ws.ws_chminh, &gl_wsptschar, &gl_hsptschar, &gl_wschar, &gl_hschar);

    /* must restore the current font height */
    vst_height(gl_ws.ws_chmaxh, &dummy, &dummy, &dummy, &dummy);

    gl_hbox = gl_hchar + 3;
    gl_wbox = divs(muls(gl_hbox, gl_ws.ws_hpixel), gl_ws.ws_wpixel);
    if (gl_wbox < gl_wchar + 4)
        gl_wbox = gl_wchar + 4;

    KDEBUG(("gsx_start(): gl_wchar=%d, gl_hchar=%d, gl_wbox=%d, gl_hbox=%d\n",
            gl_wchar, gl_hchar, gl_wbox, gl_hbox));

    vsl_type(7);
    vsl_width(1);
    vsl_udsty(0xffff);
    r_set(&gl_rscreen, 0, 0, gl_width, gl_height);
    r_set(&gl_rfull, 0, gl_hbox, gl_width, (gl_height - gl_hbox));
    r_set(&gl_rzero, 0, 0, 0, 0);
    r_set(&gl_rcenter, (gl_width-gl_wbox)/2, (gl_height-(2*gl_hbox))/2, gl_wbox, gl_hbox);
    r_set(&gl_rmenu, 0, 0, gl_width, gl_hbox);
}
