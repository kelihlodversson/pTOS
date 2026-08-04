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
        break;
    case SCREEN_COLOR_TRUECOLOR:
        switch (desc->pixel_format) {
        case SCREEN_PIXEL_RGB565:
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
