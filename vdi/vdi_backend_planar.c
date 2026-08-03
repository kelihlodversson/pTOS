/*
 * vdi_backend_planar.c - planar indexed VDI drawing backend
 *
 * Copyright (C) 2026 by the pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "vdi_defs.h"

/*
 * The planar backend keeps no state beyond VDI_BACKEND_STATE itself, so
 * the lifecycle hooks succeed trivially for now.  The drawing and misc
 * slots are NULL until the corresponding primitives are moved here and
 * routed through the backends.
 */

static BOOL planar_open(Vwk *vwk)
{
    (void)vwk;
    return TRUE;
}

static BOOL planar_clone(Vwk *vwk, const Vwk *source)
{
    (void)vwk;
    (void)source;
    return TRUE;
}

static void planar_close(Vwk *vwk)
{
    (void)vwk;
}

static BOOL planar_mode_changed(Vwk *vwk, const SCREEN_MODE_DESC *mode)
{
    (void)vwk;
    (void)mode;
    return TRUE;
}

/*
 * Address of the word containing pixel (x,y) in the first bitplane.
 * Planar pixels are interleaved one word per plane per 16-pixel group,
 * so a group of 16 pixels occupies bpp words (2*bpp bytes).
 */
static UBYTE *planar_pixel_addr(const Vwk *vwk, WORD x, WORD y)
{
    const VDI_BACKEND_STATE *state = &vwk->backend;
    UBYTE *addr = state->framebuffer;

    addr += (LONG)(x & 0xfff0) * state->mode.bits_per_pixel >> 3;
    addr += (LONG)y * state->mode.pitch;
    return addr;
}

/*
 * Compose the colour index of a pixel from the interleaved bitplanes.
 * This mirrors the historical get_color()/pixelread() semantics: the
 * highest-index plane becomes the least significant bit.
 */
static ULONG planar_read_pixel(const Vwk *vwk, WORD x, WORD y)
{
    const VDI_BACKEND_STATE *state = &vwk->backend;
    const UWORD *addr = (const UWORD *)planar_pixel_addr(vwk, x, y);
    UWORD color = 0;
    UWORD mask = 0x8000 >> (x & 0xf);
    WORD plane;

    addr += state->mode.bits_per_pixel;
    for (plane = 0; plane < state->mode.bits_per_pixel; plane++) {
        if (*--addr & mask)
            color |= 1;
        if (plane + 1 < state->mode.bits_per_pixel)
            color <<= 1;
    }
    return color;
}

/* Write the colour index of a pixel into the interleaved bitplanes. */
static BOOL planar_write_pixel(Vwk *vwk, WORD x, WORD y, ULONG pixel)
{
    const VDI_BACKEND_STATE *state = &vwk->backend;
    UWORD *addr;
    UWORD color = (UWORD)pixel;
    UWORD mask = 0x8000 >> (x & 0xf);
    WORD plane;

    if (x < 0 || y < 0 || x >= state->mode.width || y >= state->mode.height)
        return FALSE;

    addr = (UWORD *)planar_pixel_addr(vwk, x, y);
    for (plane = state->mode.bits_per_pixel - 1; plane >= 0; plane--) {
        color = (color >> 1) | (color << 15);
        if (color & 0x8000)
            *addr++ |= mask;
        else
            *addr++ &= ~mask;
    }
    return TRUE;
}

const VDI_BACKEND_OPS vdi_backend_planar_ops = {
    .open = planar_open,
    .clone = planar_clone,
    .close = planar_close,
    .mode_changed = planar_mode_changed,
    .pixel_addr = planar_pixel_addr,
    .read_pixel = planar_read_pixel,
    .write_pixel = planar_write_pixel,
};
