/*
 * vdi_backend.h - per-workstation VDI drawing backends
 *
 * Copyright (C) 2026 by the pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VDI_BACKEND_H
#define VDI_BACKEND_H

#include "portab.h"
#include "screen_mode.h"

/*
 * Forward declarations of the workstation and drawing types, so that
 * this header can be included by vdi_defs.h without an include cycle.
 * The full definitions live in vdi_defs.h.
 */
typedef struct Vwk_ Vwk;
typedef struct VwkAttrib_ VwkAttrib;
typedef struct VwkClip_ VwkClip;
typedef struct Rect_ Rect;
typedef struct Line_ Line;

/* Backend kinds, as selected by vdi_backend_kind(). */
#define VDI_BACKEND_UNSUPPORTED  0U
#define VDI_BACKEND_PLANAR       1U
#define VDI_BACKEND_TRUECOLOR16  2U

/*
 * Operations table of a drawing backend.  Every drawing and misc slot
 * may be NULL: a NULL slot means that the operation is not supported by
 * this backend, and the VDI entry points must fail or no-op instead of
 * falling back to another layout.
 */
typedef struct vdi_backend_ops {
    BOOL (*open)(Vwk *vwk);
    BOOL (*clone)(Vwk *vwk, const Vwk *source);
    void (*close)(Vwk *vwk);
    BOOL (*mode_changed)(Vwk *vwk, const SCREEN_MODE_DESC *mode);
    UBYTE *(*pixel_addr)(const Vwk *vwk, WORD x, WORD y);
    ULONG (*read_pixel)(const Vwk *vwk, WORD x, WORD y);
    BOOL (*write_pixel)(Vwk *vwk, WORD x, WORD y, ULONG pixel);
    BOOL (*fill_rect)(Vwk *vwk, const VwkAttrib *attr, const Rect *rect);
    BOOL (*draw_line)(Vwk *vwk, const Line *line, WORD wrt_mode,
                      UWORD color, UWORD *linemask);
    BOOL (*draw_vertical_line)(Vwk *vwk, const Line *line, WORD wrt_mode,
                               UWORD color, UWORD *linemask);
    BOOL (*scan_seed_span)(Vwk *vwk, const VwkClip *clip, WORD x, WORD y,
                           ULONG search_color, WORD *left, WORD *right);
    BOOL (*raster_copy)(Vwk *vwk, BOOL transparent);
    BOOL (*raster_transform)(Vwk *vwk);
    BOOL (*text_blit)(Vwk *vwk, void *vars);
    BOOL (*mouse_draw)(Vwk *vwk, WORD x, WORD y);
    void (*mouse_restore)(Vwk *vwk);
    BOOL (*set_color)(Vwk *vwk, WORD index, const WORD *rgb);
    BOOL (*get_color)(Vwk *vwk, WORD index, WORD requested, WORD *rgb);
} VDI_BACKEND_OPS;

/*
 * Per-workstation backend state, stored directly in Vwk.  pseudo_palette
 * maps requested VDI color indexes to packed pixel values; capabilities
 * is a platform/backend-specific bit mask.
 */
typedef struct vdi_backend_state {
    SCREEN_MODE_DESC mode;
    const struct vdi_backend_ops *ops;
    UBYTE *framebuffer;
    ULONG pseudo_palette[256];
    ULONG capabilities;
} VDI_BACKEND_STATE;

/*
 * Select the backend kind for a screen mode.  This is a pure function:
 * it only inspects the descriptor.
 */
UWORD vdi_backend_kind(const SCREEN_MODE_DESC *mode);

/*
 * Bind a workstation to the backend matching mode.  On success the
 * workstation owns the backend state; on failure ops is cleared and
 * FALSE is returned, so an unsupported mode can never keep an old
 * backend table.
 */
BOOL vdi_backend_bind(Vwk *vwk, const SCREEN_MODE_DESC *mode, UBYTE *framebuffer);

/* Copy the backend state from source to vwk. */
BOOL vdi_backend_clone(Vwk *vwk, const Vwk *source);

/* Tear down the backend state of a workstation. */
void vdi_backend_close(Vwk *vwk);

/*
 * Return the physical handle-1 screen workstation, never a virtual one.
 * This preserves the documented compatibility behaviour of the fake
 * CUR_WORK object.
 */
Vwk *vdi_get_screen_vwk(void);

#endif /* VDI_BACKEND_H */
