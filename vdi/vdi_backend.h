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
 * Picks a backend ops table for a mode descriptor, or NULL if no backend
 * supports that layout/color-model/pixel-format combination.
 */
const vdi_backend_ops *vdi_backend_select(const SCREEN_MODE_DESC *mode);

/*
 * The backend ops table for the current screen workstation, or NULL if
 * none was selected (e.g. the screen's mode descriptor doesn't match
 * any backend yet). There is currently exactly one screen.
 */
const vdi_backend_ops *vdi_screen_backend(void);

extern const vdi_backend_ops planar_backend_ops;
#if CONF_WITH_VDI_TRUECOLOR
extern const vdi_backend_ops packed_truecolor_backend_ops;
#endif

#endif /* VDI_BACKEND_H */
