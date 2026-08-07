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

const vdi_backend_ops planar_backend_ops = {
    planar_open,
    planar_close,
    planar_get_start_addr,
    planar_get_pixel,
    planar_put_pixel,
    planar_fill_rect,
    planar_text_blit,
    planar_raster_copy,
    planar_draw_line,
    planar_search_right,
    planar_search_left,
};
