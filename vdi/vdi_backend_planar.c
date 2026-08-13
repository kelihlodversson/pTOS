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
 * default_planar_backend_ops - the ST-shifter table, the only planar
 * vdi_backend_ops this file defines (issue #173 follow-up).
 *
 * Earlier revisions of this file defined one const table per planar
 * hardware palette family (ST/STE/TT/Videl), all sharing every slot but
 * set_color/get_color verbatim -- four near-identical 15-pointer tables,
 * each living in ROM on the m68k targets. vdi_backend_select() now keeps
 * only this one in ROM and patches a single mutable copy, planar_backend_ops
 * (below), at runtime instead -- see the comment there.
 *
 * const: this table itself is never written to (only memcpy()'d from), so
 * it stays in ROM on the m68k targets, where .data shares emutos.ld's
 * read-only region with .text (see its FIXME comment) -- a non-const table
 * here would accept writes that silently do nothing on real hardware.
 */
const vdi_backend_ops default_planar_backend_ops = {
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

/*
 * planar_backend_ops - the table vdi_backend_select() actually hands out
 * for a planar screen (issue #173 follow-up).
 *
 * No initializer, so this lands in .bss (real RAM even on the m68k
 * targets), not .data -- see the comment on default_planar_backend_ops
 * above. vdi_backend_select() populates it on every call: memcpy() the ST
 * defaults from default_planar_backend_ops, then patch set_color/get_color
 * to the selected shifter family's pair if it isn't ST (which needs no
 * patch -- the copied defaults are already correct).
 */
vdi_backend_ops planar_backend_ops;
