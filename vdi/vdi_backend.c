/*
 * vdi_backend.c - VDI backend selection and workstation lifecycle
 *
 * Copyright (C) 2026 by the pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "vdi_defs.h"

/*
 * The operation tables are exported by the per-layout backend files.
 * The truecolor table is only referenced when that backend is built, so
 * platforms without it still link.
 */
extern const VDI_BACKEND_OPS vdi_backend_planar_ops;
#if CONF_WITH_VDI_TRUECOLOR
extern const VDI_BACKEND_OPS vdi_backend_truecolor_ops;
#endif

/*
 * Select the backend kind for a screen mode.  The kind is a runtime
 * fact of the descriptor, never of the platform or of v_planes alone.
 */
UWORD vdi_backend_kind(const SCREEN_MODE_DESC *mode)
{
    if (mode->layout == SCREEN_LAYOUT_PLANAR)
        return (mode->color_model == SCREEN_COLOR_INDEXED)
             ? VDI_BACKEND_PLANAR : VDI_BACKEND_UNSUPPORTED;

    if (mode->layout == SCREEN_LAYOUT_PACKED
        && mode->color_model == SCREEN_COLOR_TRUECOLOR
        && mode->bits_per_pixel == 16) {
        switch (mode->pixel_format) {
        case SCREEN_PIXEL_RGB555:
        case SCREEN_PIXEL_RGB565:
        case SCREEN_PIXEL_BGR555:
        case SCREEN_PIXEL_BGR565:
            return VDI_BACKEND_TRUECOLOR16;
        default:
            break;
        }
    }

    return VDI_BACKEND_UNSUPPORTED;
}

BOOL vdi_backend_bind(Vwk *vwk, const SCREEN_MODE_DESC *mode, UBYTE *framebuffer)
{
    VDI_BACKEND_STATE *state = &vwk->backend;
    const VDI_BACKEND_OPS *ops = NULL;

    switch (vdi_backend_kind(mode)) {
    case VDI_BACKEND_PLANAR:
        ops = &vdi_backend_planar_ops;
        break;
    case VDI_BACKEND_TRUECOLOR16:
#if CONF_WITH_VDI_TRUECOLOR
        ops = &vdi_backend_truecolor_ops;
#endif
        break;
    default:
        break;
    }

    /* Tear down any previous backend before opening a new one, so a
     * re-bind (e.g. after setscreen) cannot leak backend state. */
    if (state->ops != NULL && state->ops->close != NULL)
        state->ops->close(vwk);

    state->ops = ops;
    state->framebuffer = framebuffer;
    if (ops == NULL)
        return FALSE;

    state->mode = *mode;
    state->capabilities = 0UL;
    if (ops->open != NULL && !ops->open(vwk)) {
        state->ops = NULL;
        state->framebuffer = NULL;
        return FALSE;
    }
    return TRUE;
}

BOOL vdi_backend_clone(Vwk *vwk, const Vwk *source)
{
    vwk->backend = source->backend;
    if (vwk->backend.ops != NULL && vwk->backend.ops->clone != NULL)
        return vwk->backend.ops->clone(vwk, source);
    return vwk->backend.ops != NULL;
}

void vdi_backend_close(Vwk *vwk)
{
    if (vwk->backend.ops != NULL && vwk->backend.ops->close != NULL)
        vwk->backend.ops->close(vwk);
    vwk->backend.ops = NULL;
    vwk->backend.framebuffer = NULL;
}
