/*
 * vdi_backend.h - runtime-dispatch VDI drawing backend
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef VDI_BACKEND_H
#define VDI_BACKEND_H

#include "portab.h"
#include "screen_mode.h"
#include "vdi_defs.h"
#include "vdi_textblit.h"
#include "vdi_raster.h"

/*
 * A NULL slot means "this backend does not implement this primitive" --
 * never "fall back to another backend." NULL slots are filled with generic
 * defaults by vdi_backend_ops_init() (task 2); mandatory slots are
 * get_start_addr, get_pixel, put_pixel (and get_raw_pixel/put_raw_pixel
 * once task 2 adds them).
 */
typedef struct vdi_backend_ops {
    BOOL (*open)(Vwk *vwk);
    void (*close)(Vwk *vwk);

    UWORD *(*get_start_addr)(WORD x, WORD y);
    UWORD (*get_pixel)(WORD x, WORD y);
    void (*put_pixel)(WORD x, WORD y, UWORD color);
    void (*fill_rect)(const VwkAttrib *attr, const Rect *rect);
    void (*text_blit)(LOCALVARS *vars);
    void (*raster_copy)(struct raster_t *raster, struct blit_frame *info);

    /*
     * Draws a single non-horizontal line (abline()'s horizontal case is
     * handled earlier via fill_rect(), see draw_rect_common()). linemask
     * is the current line-style state (see LN_MASK); returns the state
     * after rotating one bit per pixel drawn, for the caller to save back.
     */
    UWORD (*draw_line)(const Line *line, WORD wrt_mode, UWORD color, UWORD linemask);

    /*
     * Scan right/left from (x,y) along a horizontal line for the last
     * pixel matching search_col (a MAP_COL-mapped hardware palette index,
     * like get_pixel()'s return value) before the first mismatch or the
     * clip edge -- used by contourfill()'s seed-fill scan (see end_pts()
     * in vdi_fill.c). Mandatory, like get_pixel()/put_pixel(): a backend
     * that implements get_pixel() can always answer this too, by
     * construction, so callers don't need to guard the slot itself.
     */
    WORD (*search_right)(const VwkClip *clip, WORD x, WORD y, UWORD search_col);
    WORD (*search_left)(const VwkClip *clip, WORD x, WORD y, UWORD search_col);
} vdi_backend_ops;

/*
 * This runtime selection machinery -- vdi_backend_select(),
 * vdi_screen_backend(), the vdi_backend_ops table, and the per-renderer
 * tables -- only exists when CONF_WITH_VDI_BACKEND_DISPATCH is set (more
 * than one renderer enabled, see vdi/build.mk). With exactly one
 * renderer, the primitives that would otherwise dispatch through it
 * (get_start_addr/pixelread/put_pix/draw_rect_common/text_blit/raster_copy/
 * abline/end_pts in vdi_misc.c/vdi_fill.c/vdi_line.c/vdi_textblit.c/
 * vdi_raster.c) call that renderer's primitives directly instead -- the
 * planar ones for a planar-only build, the truecolor ones for a
 * truecolor-only build. This matters on cartridge_defconfig, whose 128 KB
 * image has essentially no room for dispatch overhead that can only ever
 * resolve one way.
 */
#if CONF_WITH_VDI_BACKEND_DISPATCH

/*
 * Picks a backend ops table for a mode descriptor, or NULL if no backend
 * supports that layout/color-model/pixel-format combination.
 *
 * Every existing driver reports a descriptor a backend handles (planar+
 * indexed, or on MACHINE_RPI, packed+truecolor+RGB565), and can only be
 * queried once its video hardware is set up, so in practice this cannot
 * return NULL for any of them. The check exists for whichever future
 * driver reports something no backend yet implements.
 */
const vdi_backend_ops *vdi_backend_select(const SCREEN_MODE_DESC *mode);

/*
 * The backend ops table for the current screen workstation. Self-
 * initializes on first call if vdi_v_opnwk() hasn't run yet (see the
 * comment on the definition) -- callers do not need to call vdi_v_opnwk()
 * first. Returns NULL only in the vdi_backend_select() case above, which
 * cannot happen for any of this codebase's drivers today -- but callers
 * still guard against it (see get_start_addr()/pixelread()/put_pix()/
 * draw_rect_common()). There is currently exactly one screen.
 */
const vdi_backend_ops *vdi_screen_backend(void);

extern vdi_backend_ops planar_backend_ops;
extern vdi_backend_ops packed_truecolor_backend_ops;

#endif /* CONF_WITH_VDI_BACKEND_DISPATCH */

/*
 * Is the current screen workstation driven by the packed-truecolor
 * backend?  Used by text_blt() to decide whether styled text must go
 * through pre_blit(), by cpy_raster() for the packed 1-plane MFDB layout,
 * and by contourfill() for the full MAP_COL palette index.
 *
 * Inline so it exists in all three build modes: under dispatch it is the
 * runtime check against the selected table; with exactly one renderer the
 * answer is a compile-time constant.
 */
static inline BOOL vdi_screen_is_truecolor(void)
{
#if CONF_WITH_VDI_BACKEND_DISPATCH
    return vdi_screen_backend() == &packed_truecolor_backend_ops;
#else
    return CONF_WITH_VDI_BACKEND_TRUECOLOR;
#endif
}

/*
 * Turns a MAP_COL-mapped hardware palette index into the raw RGB565 pixel
 * value the packed-truecolor backend would write for it. Used by callers
 * that poke pixels directly instead of going through put_pixel()/
 * fill_rect() -- currently the RPi software mouse cursor in vdi_mouse.c.
 */
#if CONF_WITH_VDI_BACKEND_TRUECOLOR
UWORD vdi_truecolor_pixel_for_index(WORD index);
#endif

#endif /* VDI_BACKEND_H */
