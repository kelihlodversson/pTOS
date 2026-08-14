/*
 * screen_mode.c - screen mode descriptor validation
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "screen_mode.h"

BOOL screen_mode_desc_valid(const SCREEN_MODE_DESC *desc)
{
    ULONG min_pitch;

    if (desc->pitch == 0)
        return FALSE;

    min_pitch = ((ULONG)desc->width * desc->bits_per_pixel + 7) / 8;
    if (desc->pitch < min_pitch)
        return FALSE;

    switch (desc->layout) {
    case SCREEN_LAYOUT_PLANAR:
        switch (desc->shifter) {
        case SCREEN_SHIFTER_ST:
        case SCREEN_SHIFTER_STE:
        case SCREEN_SHIFTER_TT:
        case SCREEN_SHIFTER_VIDEL:
            break;
        default:
            return FALSE;
        }
        break;
    case SCREEN_LAYOUT_PACKED:
        /* shifter is meaningless for a packed layout (see screen_mode.h) */
        if (desc->shifter != SCREEN_SHIFTER_NONE)
            return FALSE;
        break;
    default:
        return FALSE;
    }

    switch (desc->color_model) {
    case SCREEN_COLOR_INDEXED:
        /* pixel_format is meaningless for indexed color (see screen_mode.h) */
        if (desc->pixel_format != SCREEN_PIXEL_NONE)
            return FALSE;
        break;
    case SCREEN_COLOR_TRUECOLOR:
        switch (desc->pixel_format) {
        case SCREEN_PIXEL_RGB565:
            /*
             * vdi_backend_select() picks the truecolor backend off
             * pixel_format alone; a descriptor claiming RGB565 with the
             * wrong bits_per_pixel would pass an undersized pitch check
             * and then drive 16bpp address arithmetic against a buffer
             * that isn't actually 16bpp. Reject the mismatch here instead.
             */
            if (desc->bits_per_pixel != 16)
                return FALSE;
            /*
             * The RGB565 backend does UWORD loads/stores per pixel and
             * relies on every scanline starting 2-byte aligned; an odd
             * pitch would misalign every other row.
             */
            if (desc->pitch & 1)
                return FALSE;
            break;
        case SCREEN_PIXEL_XRGB8888:
            /*
             * Mirror of the RGB565 check: vdi_backend_select() picks the
             * 32 bpp backend off pixel_format alone, so a descriptor
             * claiming XRGB8888 with the wrong bits_per_pixel would drive
             * 4-byte address arithmetic against a buffer that isn't 32 bpp.
             * Reject the mismatch here instead.  The 32 bpp backend does
             * ULONG loads/stores per pixel and relies on every scanline
             * starting 4-byte aligned.
             */
            if (desc->bits_per_pixel != 32)
                return FALSE;
            if (desc->pitch & 3)
                return FALSE;
            break;
        default:
            return FALSE;
        }
        break;
    default:
        return FALSE;
    }

    return TRUE;
}
