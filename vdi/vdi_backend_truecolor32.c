/*
 * vdi_backend_truecolor32.c - packed 32bpp XRGB8888 truecolor VDI backend
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * Thin wrapper: the drawing code is the shared template
 * (vdi_backend_truecolor_tmpl.c), instantiated here for 32bpp XRGB8888.
 * All shared state (default palette, active workstation, the format-
 * independent exports) lives in vdi_backend_truecolor.c, which is always
 * built when CONF_WITH_VDI_BACKEND_TRUECOLOR is set -- this wrapper only
 * defines its own ops table.  No extern stubs: a single-renderer 32bpp
 * build is impossible (CONF_WITH_VDI_BACKEND_TRUECOLOR32 depends on the
 * dispatcher), and under dispatch the drawing goes through the table.
 */

#include "config.h"
#include "portab.h"
#include "asm.h"
#include "lineavars.h"
#include "tosvars.h"
#include "vdi_defs.h"
#include "vdi_backend.h"
#include "kprint.h"

#define PIXEL ULONG
#define PIXEL_SIZE 4
#include "vdi_backend_truecolor_tmpl.c"
#undef PIXEL_SIZE
#undef PIXEL

static UWORD *truecolor32_get_start_addr(WORD x, WORD y)
{
    return (UWORD *)tc_get_start_addr(x, y);
}

static void truecolor32_ops_set_color(Vwk *vwk, WORD pen, WORD *rgb)
{
    vdi_truecolor_set_color(vwk, MAP_COL[pen], rgb[0], rgb[1], rgb[2]);
}

static void truecolor32_ops_get_color(const Vwk *vwk, WORD pen, WORD *rgb)
{
    vdi_truecolor_get_color(vwk, MAP_COL[pen], &rgb[0], &rgb[1], &rgb[2]);
}

vdi_backend_ops packed_truecolor32_backend_ops = {
    NULL,                       /* open: generic default */
    NULL,                       /* close: generic default */
    truecolor32_get_start_addr,
    tc_get_pixel,
    tc_put_pixel,
    tc_get_raw_pixel,
    tc_put_raw_pixel,
    truecolor32_ops_set_color,
    truecolor32_ops_get_color,
#if CONF_VDI_SPARSE_TABLE
    /* The optional slots are left NULL so vdi_backend_ops_init() fills them
     * with the generic defaults -- same sparse behavior as the 16bpp RGB565
     * wrapper (see vdi_backend_truecolor.c). */
    NULL, NULL, NULL, NULL, NULL, NULL,
#else
    tc_fill_rect,
    tc_text_blit,
    tc_raster_copy,
    tc_draw_line,
    tc_search_right,
    tc_search_left,
#endif
    4,                          /* pixel_size */
};
