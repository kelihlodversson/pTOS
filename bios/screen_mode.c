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
    case SCREEN_LAYOUT_PACKED:
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
