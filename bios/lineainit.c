/*
 * lineainit.c - linea graphics initialization
 *
 * Copyright (C) 2001-2024 by Authors:
 *
 * Authors:
 *  MAD  Martin Doering
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/* #define ENABLE_KDEBUG */

#include "emutos.h"
#include "lineavars.h"
#include "screen.h"
#include "screen_mode.h"

#define DBG_LINEA 0

/* Precomputed value of log2(8/v_planes).
 * To get the address of a pixel x in a scan line, use the formula:
 * (x&0xfff0)>>shift_offset[v_planes]
 * Only the indexes 1, 2, 4 and 8 are meaningful.
 */
const BYTE shift_offset[9] = {0, 3, 2, 0, 1, 0, 0, 0, 0};

/*
 * mouse cursor save areas
 */
MCS ext_mouse_cursor_save;   /* use for v_planes > 4 */

MCS *mcs_ptr;   /* ptr to current mouse cursor save area, based on v_planes */
struct _lineavars linea_vars;

/*
 * linea_init - init linea variables
 */
void linea_init(void)
{
    SCREEN_MODE_DESC desc;

    screen_get_current_mode_desc(&desc);

    linea_vars.v_planes = desc.bits_per_pixel;
    linea_vars.V_REZ_HZ = desc.width;
    linea_vars.V_REZ_VT = desc.height;
    linea_vars.v_lin_wr = desc.pitch;         /* bytes per line */
    linea_vars.BYTES_LIN = linea_vars.v_lin_wr;       /* I think BYTES_LIN = v_lin_wr (PES) */

    mcs_ptr = (linea_vars.v_planes <= 4) ? (MCS *)&linea_vars.mouse_cursor_save : &ext_mouse_cursor_save;

    /*
     * this is a convenient place to update the workstation xres/yres which
     * may have been changed by a Setscreen()
     */
    linea_vars.DEV_TAB[0] = linea_vars.V_REZ_HZ - 1;
    linea_vars.DEV_TAB[1] = linea_vars.V_REZ_VT - 1;

#if DBG_LINEA
    kprintf("planes: %d\n", linea_vars.v_planes);
    kprintf("lin_wr: %d\n", linea_vars.v_lin_wr);
    kprintf("hz_rez: %d\n", linea_vars.V_REZ_HZ);
    kprintf("vt_rez: %d\n", linea_vars.V_REZ_VT);
#endif
}
