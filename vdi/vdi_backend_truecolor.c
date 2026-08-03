/*
 * vdi_backend_truecolor.c - packed 16-bit truecolor VDI drawing backend
 *
 * Copyright (C) 2026 by the pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * This file intentionally does not include config.h: it must stay
 * compilable by the native host tests, and it only uses information
 * from the screen mode descriptor, never from the build configuration.
 */

#include "vdi_defs.h"

/*
 * The truecolor backend keeps no state beyond VDI_BACKEND_STATE itself,
 * so the lifecycle hooks succeed trivially for now.  The drawing and
 * misc slots are NULL until the packed16 primitives are implemented and
 * routed through the backends.
 */

static BOOL truecolor_open(Vwk *vwk)
{
    (void)vwk;
    return TRUE;
}

static BOOL truecolor_clone(Vwk *vwk, const Vwk *source)
{
    (void)vwk;
    (void)source;
    return TRUE;
}

static void truecolor_close(Vwk *vwk)
{
    (void)vwk;
}

static BOOL truecolor_mode_changed(Vwk *vwk, const SCREEN_MODE_DESC *mode)
{
    (void)vwk;
    (void)mode;
    return TRUE;
}

/* Address of the 16-bit pixel value at (x,y). */
static UBYTE *truecolor_pixel_addr(const Vwk *vwk, WORD x, WORD y)
{
    const VDI_BACKEND_STATE *state = &vwk->backend;

    return state->framebuffer + (LONG)y * state->mode.pitch + (LONG)x * 2;
}

static ULONG truecolor_read_pixel(const Vwk *vwk, WORD x, WORD y)
{
    const UWORD *p = (const UWORD *)truecolor_pixel_addr(vwk, x, y);

    return *p;
}

static BOOL truecolor_write_pixel(Vwk *vwk, WORD x, WORD y, ULONG pixel)
{
    const VDI_BACKEND_STATE *state = &vwk->backend;
    UWORD *p;

    if (x < 0 || y < 0 || x >= state->mode.width || y >= state->mode.height)
        return FALSE;

    p = (UWORD *)truecolor_pixel_addr(vwk, x, y);
    *p = (UWORD)pixel;
    return TRUE;
}

/* Pack a requested VDI RGB value into the pseudo-palette of a pen. */
static BOOL truecolor_set_color(Vwk *vwk, WORD index, const WORD *rgb)
{
    VDI_BACKEND_STATE *state = &vwk->backend;

    state->pseudo_palette[index] =
        screen_mode_pack_color(&state->mode, rgb[0], rgb[1], rgb[2]);
    return TRUE;
}

const VDI_BACKEND_OPS vdi_backend_truecolor_ops = {
    .open = truecolor_open,
    .clone = truecolor_clone,
    .close = truecolor_close,
    .mode_changed = truecolor_mode_changed,
    .pixel_addr = truecolor_pixel_addr,
    .read_pixel = truecolor_read_pixel,
    .write_pixel = truecolor_write_pixel,
    .set_color = truecolor_set_color,
};
