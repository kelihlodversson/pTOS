/*
 * vdi_backend_planar.c - planar (interleaved-bitplane) VDI backend
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "vdi_defs.h"
#include "vdi_backend.h"

static BOOL planar_open(Vwk *vwk)
{
    (void)vwk;
    return TRUE;
}

static void planar_close(Vwk *vwk)
{
    (void)vwk;
}

/*
 * ULONG raw-pixel adapters for the ops table: a planar pixel's raw value
 * is its composed colour index, which fits a UWORD; widen to the ops
 * interface's ULONG on the way in/out.
 */
static ULONG planar_get_raw_pixel(WORD x, WORD y)
{
    return (ULONG)planar_get_pixel(x, y);
}

static void planar_put_raw_pixel(WORD x, WORD y, ULONG raw)
{
    planar_put_pixel(x, y, (UWORD)raw);
}

vdi_backend_ops planar_backend_ops = {
    planar_open,
    planar_close,
    planar_get_start_addr,
    planar_get_pixel,
    planar_put_pixel,
    planar_get_raw_pixel,
    planar_put_raw_pixel,
    planar_fill_rect,
    planar_text_blit,
    planar_raster_copy,
    planar_draw_line,
    planar_search_right,
    planar_search_left,
    2,                          /* pixel_size */
};
