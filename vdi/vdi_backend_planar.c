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
 * One vdi_backend_ops variant per planar hardware palette family (issue
 * #173), selected by vdi_backend_select() from SCREEN_MODE_DESC.shifter
 * (see screen_mode.h, planar_mode_desc() in bios/screen.c). Every slot but
 * set_color/get_color is shifter-agnostic and shared verbatim between the
 * variants -- plane count, not chip family, is what the rest of the planar
 * backend varies on, and that's already handled generically.
 *
 * const: every slot below is always populated, so vdi_backend_select() only
 * ever runs the read-only vdi_backend_ops_validate() on these, never the
 * mutating vdi_backend_ops_init(). That matters on the m68k targets, where
 * .data shares emutos.ld's read-only ROM region with .text (see its FIXME
 * comment) -- a non-const table here would silently fail to hold any write,
 * and vdi_backend_ops_init() would never actually need to write anything to
 * a table with no NULL slots, so nothing is lost by not calling it.
 */
const vdi_backend_ops planar_st_backend_ops = {
    planar_open,
    planar_close,
    planar_get_start_addr,
    planar_get_pixel,
    planar_put_pixel,
    planar_get_pixel,       /* get_raw_pixel: a planar pixel's raw value is its composed colour index */
    planar_put_pixel,       /* put_raw_pixel: same for writing */
    planar_set_st_color,
    planar_get_st_color,
    planar_fill_rect,
    planar_text_blit,
    planar_raster_copy,
    planar_draw_line,
    planar_search_right,
    planar_search_left,
};

#if CONF_WITH_STE_SHIFTER
const vdi_backend_ops planar_ste_backend_ops = {
    planar_open,
    planar_close,
    planar_get_start_addr,
    planar_get_pixel,
    planar_put_pixel,
    planar_get_pixel,
    planar_put_pixel,
    planar_set_ste_color,
    planar_get_ste_color,
    planar_fill_rect,
    planar_text_blit,
    planar_raster_copy,
    planar_draw_line,
    planar_search_right,
    planar_search_left,
};
#endif

#if CONF_WITH_TT_SHIFTER
const vdi_backend_ops planar_tt_backend_ops = {
    planar_open,
    planar_close,
    planar_get_start_addr,
    planar_get_pixel,
    planar_put_pixel,
    planar_get_pixel,
    planar_put_pixel,
    planar_set_tt_color,
    planar_get_tt_color,
    planar_fill_rect,
    planar_text_blit,
    planar_raster_copy,
    planar_draw_line,
    planar_search_right,
    planar_search_left,
};
#endif

#if CONF_WITH_VIDEL
const vdi_backend_ops planar_videl_backend_ops = {
    planar_open,
    planar_close,
    planar_get_start_addr,
    planar_get_pixel,
    planar_put_pixel,
    planar_get_pixel,
    planar_put_pixel,
    planar_set_videl_color,
    planar_get_videl_color,
    planar_fill_rect,
    planar_text_blit,
    planar_raster_copy,
    planar_draw_line,
    planar_search_right,
    planar_search_left,
};
#endif
