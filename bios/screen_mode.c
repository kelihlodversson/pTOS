/*
 * screen_mode.c - shared screen mode descriptor validation
 *
 * Copyright (C) 2026 by the pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "screen_mode.h"

/*
 * Clamp a VDI 0-1000 color component to the valid range.
 */
static UWORD clamp_component(WORD value)
{
    if (value < 0)
        return 0U;
    if (value > 1000)
        return 1000U;
    return (UWORD)value;
}

/*
 * Pack a component into n bits with rounding, using ULONG arithmetic
 * so that the intermediate products survive the 16-bit int of m68k.
 */
static UWORD pack_component(UWORD value, UWORD bits)
{
    ULONG range;

    range = (1UL << bits) - 1UL;
    return (UWORD)((range * (ULONG)value + 500UL) / 1000UL);
}

/*
 * screen_mode_pack_color - convert VDI 0-1000 components to a pixel
 * value in the format described by the mode.
 */
UWORD screen_mode_pack_color(const SCREEN_MODE_DESC *mode, WORD red, WORD green, WORD blue)
{
    UWORD r;
    UWORD g;
    UWORD b;

    r = clamp_component(red);
    g = clamp_component(green);
    b = clamp_component(blue);

    switch (mode->pixel_format) {
    case SCREEN_PIXEL_RGB565:
        return (UWORD)((ULONG)pack_component(r, 5) << 11)
             | (UWORD)((ULONG)pack_component(g, 6) << 5)
             | pack_component(b, 5);
    case SCREEN_PIXEL_RGB555:
        return (UWORD)((ULONG)pack_component(r, 5) << 10)
             | (UWORD)((ULONG)pack_component(g, 5) << 5)
             | pack_component(b, 5);
    case SCREEN_PIXEL_BGR565:
        return (UWORD)((ULONG)pack_component(b, 5) << 11)
             | (UWORD)((ULONG)pack_component(g, 6) << 5)
             | pack_component(r, 5);
    case SCREEN_PIXEL_BGR555:
        return (UWORD)((ULONG)pack_component(b, 5) << 10)
             | (UWORD)((ULONG)pack_component(g, 5) << 5)
             | pack_component(r, 5);
    default:
        return 0U;
    }
}

/*
 * screen_mode_validate - sanity-check a screen mode descriptor.
 *
 * Rejects zero dimensions, depth or pitch, pitches that cannot be
 * expressed in the UWORD Line-A variables, internally inconsistent
 * layout/color combinations, unknown truecolor formats, and packed
 * pitches that cannot hold a scan line.  Planar and packed indexed
 * descriptors legitimately carry SCREEN_PIXEL_NONE.
 */
BOOL screen_mode_validate(const SCREEN_MODE_DESC *mode)
{
    ULONG min_pitch;

    if (mode->width == 0 || mode->height == 0)
        return FALSE;
    if (mode->bits_per_pixel == 0)
        return FALSE;
    if (mode->pitch == 0UL || mode->pitch > 0xffffUL)
        return FALSE;

    if (mode->layout == SCREEN_LAYOUT_PLANAR) {
        if (mode->color_model != SCREEN_COLOR_INDEXED)
            return FALSE;
        if (mode->pixel_format != SCREEN_PIXEL_NONE)
            return FALSE;
        return TRUE;
    }

    if (mode->layout != SCREEN_LAYOUT_PACKED)
        return FALSE;

    min_pitch = (ULONG)mode->width * (((ULONG)mode->bits_per_pixel + 7UL) / 8UL);
    if (mode->pitch < min_pitch)
        return FALSE;

    if (mode->color_model == SCREEN_COLOR_TRUECOLOR) {
        if (mode->pixel_format != SCREEN_PIXEL_RGB555
            && mode->pixel_format != SCREEN_PIXEL_RGB565
            && mode->pixel_format != SCREEN_PIXEL_BGR555
            && mode->pixel_format != SCREEN_PIXEL_BGR565)
            return FALSE;
        return TRUE;
    }

    if (mode->color_model == SCREEN_COLOR_INDEXED)
        return mode->pixel_format == SCREEN_PIXEL_NONE;

    return FALSE;
}
