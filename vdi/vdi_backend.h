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

/*
 * A NULL slot means "this backend does not implement this primitive" --
 * never "fall back to another backend." A backend is only ever selected
 * for descriptors whose layout/color-model/bpp combination it actually
 * supports (see vdi_backend_select()), so an incompatible fallback can
 * never happen by construction.
 *
 * This table currently only covers the primitives this slice converts.
 * Follow-up slices (line/vline, raster copy, text blit, mouse cursor,
 * full palette -- issue #35 parts 2b/5) add their own slots when they
 * actually implement them.
 */
typedef struct vdi_backend_ops {
    BOOL (*open)(Vwk *vwk);
    void (*close)(Vwk *vwk);

    UWORD *(*get_start_addr)(WORD x, WORD y);
    UWORD (*get_pixel)(WORD x, WORD y);
    void (*put_pixel)(WORD x, WORD y, UWORD color);
    void (*fill_rect)(const VwkAttrib *attr, const Rect *rect);
} vdi_backend_ops;

/*
 * This runtime selection machinery -- vdi_backend_select(),
 * vdi_screen_backend(), and the planar backend's ops table -- only exists
 * when CONF_WITH_VDI_TRUECOLOR is set (see vdi/build.mk). Without a
 * truecolor backend, planar is the only backend that could ever be
 * selected, so the primitives that would otherwise dispatch through it
 * (get_start_addr/pixelread/put_pix/draw_rect_common in vdi_misc.c/
 * vdi_fill.c/vdi_line.c) call planar_get_start_addr()/planar_get_pixel()/
 * planar_put_pixel()/planar_fill_rect() directly instead. This matters on
 * cartridge_defconfig, whose 128 KB image has essentially no room for
 * dispatch overhead that can only ever resolve one way.
 */
#if CONF_WITH_VDI_TRUECOLOR

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
 * draw_rect_common()) since this whole path only builds for MACHINE_RPI,
 * which has none of cartridge_defconfig's byte-budget pressure. There is
 * currently exactly one screen.
 */
const vdi_backend_ops *vdi_screen_backend(void);

extern const vdi_backend_ops planar_backend_ops;
extern const vdi_backend_ops packed_truecolor_backend_ops;

#endif /* CONF_WITH_VDI_TRUECOLOR */

#endif /* VDI_BACKEND_H */
