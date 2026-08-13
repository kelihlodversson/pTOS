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
 * never "fall back to another backend." The dispatcher's
 * vdi_backend_ops_init() (see below) fills every NULL slot with a
 * renderer-agnostic default built only on the mandatory primitives
 * (get_start_addr, get_pixel, put_pixel, get_raw_pixel, put_raw_pixel),
 * which must be non-NULL.  open/close default to no-ops.
 */
typedef struct vdi_backend_ops {
    BOOL (*open)(Vwk *vwk);
    void (*close)(Vwk *vwk);

    UWORD *(*get_start_addr)(WORD x, WORD y);
    UWORD (*get_pixel)(WORD x, WORD y);
    void (*put_pixel)(WORD x, WORD y, UWORD color);
    /*
     * Raw pixel access, bypassing the palette-index mapping of
     * get_pixel()/put_pixel().  A raw value is a ULONG holding the active
     * format's packed pixel (RGB565 in the low 16 bits, XRGB8888 over all
     * 32).  Mandatory: the generic defaults need it to express bitwise
     * operations (XOR write mode, the opaque boolean-raster-op path of
     * raster_copy), which a palette index cannot represent.  On the planar
     * backend the raw value is the composed plane index, i.e.
     * get_pixel()/put_pixel() themselves.
     */
    ULONG (*get_raw_pixel)(WORD x, WORD y);
    void (*put_raw_pixel)(WORD x, WORD y, ULONG raw);
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
     * in vdi_fill.c).  A backend that does not provide its own gets the
     * generic default (see vdi_backend_ops_init()).
     */
    WORD (*search_right)(const VwkClip *clip, WORD x, WORD y, UWORD search_col);
    WORD (*search_left)(const VwkClip *clip, WORD x, WORD y, UWORD search_col);
    /*
     * Bytes per packed pixel (2 for RGB565, 4 for XRGB8888).  Used by
     * vdi_backend_ops_init()'s generic defaults to compute a raw XOR mask
     * that covers one whole pixel.  Mandatory, always set by the table.
     */
    UWORD pixel_size;
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
#if CONF_WITH_VDI_BACKEND_TRUECOLOR32
extern vdi_backend_ops packed_truecolor32_backend_ops;
#endif

/*
 * Installs a generic default into every NULL slot of a backend ops table
 * (see the defaults in vdi_backend.c).  Mandatory slots must already be
 * non-NULL.  Idempotent: safe to call on every vdi_backend_select().
 */
void vdi_backend_ops_init(vdi_backend_ops *ops);

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
    const vdi_backend_ops *backend = vdi_screen_backend();

#if CONF_WITH_VDI_BACKEND_TRUECOLOR32
    if (backend == &packed_truecolor32_backend_ops)
        return TRUE;
#endif
    return backend == &packed_truecolor_backend_ops;
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
ULONG vdi_truecolor_pixel_for_index(WORD index);

/*
 * vs_color()/vq_color() pseudo-palette access for the truecolor backend
 * (issue #89). Genuinely per-workstation (see the tc_palette comment on
 * struct Vwk_ in vdi_defs.h): vwk identifies which workstation's palette
 * to read/write. index is a MAP_COL-mapped hardware palette register
 * index; r/g/b are VDI-scale color values (0-1000). Called from
 * vdi_vs_color()/vdi_vq_color() in vdi_col.c, which already have the
 * right vwk from the VDI dispatcher.
 */
void vdi_truecolor_set_color(Vwk *vwk, WORD index, WORD r, WORD g, WORD b);
void vdi_truecolor_get_color(const Vwk *vwk, WORD index, WORD *r, WORD *g, WORD *b);

/* Seeds vwk's pseudo-palette with the backend's boot-time defaults. Called
 * by init_wk() (vdi_control.c) whenever a workstation is opened, and by
 * vdi_backend_active_vwk() (see above) for the physical workstation if
 * drawing happens before vdi_v_opnwk() ever runs. */
void vdi_truecolor_init_palette(Vwk *vwk);

/* Bytes per packed pixel of the current screen (2 for RGB565, 4 for
 * XRGB8888) -- v_planes / 8. Used by the software mouse cursor, the AES
 * colour-icon packers and setup_info() instead of hard-coding 2. */
UWORD vdi_truecolor_pixel_size(void);

/*
 * The workstation whose pseudo-palette get_pixel()/put_pixel()/etc.
 * (none of which take a Vwk*) should translate indices through -- see the
 * definitions in vdi_backend_truecolor.c. Written by vdi_main.c's
 * screen() once per VDI call, and by vdi_v_clsvwk() (vdi_control.c),
 * which points it back at the physical workstation if the Vwk it is
 * about to free is the currently active one.
 */
void vdi_backend_set_active_vwk(Vwk *vwk);
Vwk *vdi_backend_active_vwk(void);
#endif

/*
 * Extern backend query + write-mode helpers, visible to the AES via
 * include/gsxdefs.h.  vdi_colour_blit_mode() is defined in vdi/vdi_raster.c
 * (always built); vdi_truecolor_screen() in vdi_backend_truecolor.c (built
 * exactly when this backend is).  See gr_colourblit() (aes/gemgraf.c).
 */
BOOL vdi_truecolor_screen(void);
WORD vdi_colour_blit_mode(void);

#endif /* VDI_BACKEND_H */
