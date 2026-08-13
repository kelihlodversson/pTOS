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
 * (get_start_addr, get_pixel, put_pixel, get_raw_pixel, put_raw_pixel,
 * set_color, get_color), which must be non-NULL.  open/close default to
 * no-ops.
 */
typedef struct vdi_backend_ops {
    BOOL (*open)(Vwk *vwk);
    void (*close)(Vwk *vwk);

    UWORD *(*get_start_addr)(WORD x, WORD y);
    UWORD (*get_pixel)(WORD x, WORD y);
    void (*put_pixel)(WORD x, WORD y, UWORD color);
    /*
     * Raw framebuffer word access, bypassing the palette-index mapping of
     * get_pixel()/put_pixel().  Mandatory: the generic defaults need it to
     * express bitwise operations (XOR write mode, the opaque boolean-raster-
     * op path of raster_copy), which a palette index cannot represent.  On
     * the planar backend the raw value is the composed plane index, i.e.
     * get_pixel()/put_pixel() themselves.
     */
    UWORD (*get_raw_pixel)(WORD x, WORD y);
    void (*put_raw_pixel)(WORD x, WORD y, UWORD raw);

    /*
     * Write/read the "actual" hardware or backend palette entry for a VDI
     * pen (see vdi_vs_color()/vdi_vq_color() in vdi_col.c, issue #171).
     * pen is the raw pen number as passed by the caller (VDI's INTIN[0]),
     * not adjusted for TT palette banking -- each backend resolves it
     * through its own indexing (MAP_COL[] for both current backends).
     * rgb holds VDI-scale (0-1000) component values; set_color's caller
     * has already range-clamped them.
     *
     * Mandatory, like the raw-pixel pair above: there is no renderer-
     * agnostic way to write a hardware/backend palette, so there is no
     * generic default to fall back to.  The separate "last requested"
     * value cache (REQ_COL/req_col2 vs. a workstation's tc_req_col) is a
     * backend-specific storage layout, not a primitive -- it stays inline
     * at the vdi_col.c call site, the same kind of setup/data-layout
     * decision as cpy_raster()'s packed-MFDB-layout branches.
     */
    void (*set_color)(Vwk *vwk, WORD pen, WORD *rgb);
    void (*get_color)(const Vwk *vwk, WORD pen, WORD *rgb);

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

/*
 * default_planar_backend_ops/planar_backend_ops (vdi_backend_planar.c) --
 * the planar backend's ops table, selected by vdi_backend_select() for a
 * planar screen (issue #173 and its follow-up).
 *
 * A single hardware palette family (ST/STE/TT/Videl, see
 * SCREEN_MODE_DESC.shifter) only ever varies set_color/get_color -- every
 * other slot is shifter-agnostic. Rather than one const table per family
 * (four near-identical 15-pointer tables, all but two pointers duplicated),
 * vdi_backend_select() keeps only the ST-shifter defaults in ROM
 * (default_planar_backend_ops) and patches a single mutable copy,
 * planar_backend_ops, at runtime: memcpy() the defaults in, then overwrite
 * set_color/get_color for the selected family if it isn't ST -- see
 * vdi_backend_select() in vdi_backend.c.
 *
 * const/non-const split matters on the m68k targets, where .data shares
 * emutos.ld's read-only ROM region with .text (see its FIXME comment): a
 * non-const table there would accept writes that silently do nothing on
 * real hardware, and a table this is only ever memcpy()'d *from* has
 * nothing to gain from living in writable storage. planar_backend_ops has
 * no initializer, so it lands in .bss -- real RAM even on those targets --
 * not .data.
 *
 * Both are always fully populated (default_planar_backend_ops at compile
 * time, planar_backend_ops by the copy-then-patch above), so
 * vdi_backend_select() only ever runs the read-only
 * vdi_backend_ops_validate() on planar_backend_ops, never the mutating
 * vdi_backend_ops_init() -- see the comment on that pair below.
 * packed_truecolor_backend_ops is different: it can leave optional slots
 * NULL under CONF_VDI_SPARSE_TABLE (see vdi_backend_truecolor.c), so it
 * genuinely needs vdi_backend_ops_init()'s mutation and stays non-const --
 * that only matters on MACHINE_RPI, where the "ROM" is actually writable
 * RAM.
 */
extern const vdi_backend_ops default_planar_backend_ops;
extern vdi_backend_ops planar_backend_ops;
extern vdi_backend_ops packed_truecolor_backend_ops;

/*
 * Installs a generic default into every NULL slot of a backend ops table
 * (see the defaults in vdi_backend.c), then validates it (see
 * vdi_backend_ops_validate() below). Mutates ops, so it cannot be used on a
 * const table -- see the planar_*_backend_ops comment above. Idempotent:
 * safe to call on every vdi_backend_select().
 */
void vdi_backend_ops_init(vdi_backend_ops *ops);

/*
 * Read-only counterpart to vdi_backend_ops_init(), for tables that are
 * always fully populated and never need the NULL-slot fill-in (currently
 * the planar_*_backend_ops variants, which are const). KDEBUGs if a
 * mandatory primitive is missing, same check vdi_backend_ops_init() runs
 * after filling defaults.
 */
void vdi_backend_ops_validate(const vdi_backend_ops *ops);

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
