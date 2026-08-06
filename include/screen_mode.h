/*
 * screen_mode.h - describes a screen's pixel layout and color model
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef SCREEN_MODE_H
#define SCREEN_MODE_H

#include "portab.h"

#define SCREEN_LAYOUT_PLANAR   0   /* interleaved Atari-style bitplanes */
#define SCREEN_LAYOUT_PACKED   1   /* packed pixels, one pixel per unit */

#define SCREEN_COLOR_INDEXED   0   /* pixel value is a CLUT index */
#define SCREEN_COLOR_TRUECOLOR 1   /* pixel value directly encodes color */

#define SCREEN_PIXEL_NONE      0   /* not applicable: indexed color */
#define SCREEN_PIXEL_RGB565    1   /* 5 red / 6 green / 5 blue bits, packed into a UWORD */

typedef struct {
    UWORD width;          /* visible width, in pixels */
    UWORD height;         /* visible height, in pixels */
    ULONG pitch;           /* bytes per scan line */
    UWORD bits_per_pixel;
    UBYTE layout;          /* SCREEN_LAYOUT_* */
    UBYTE color_model;     /* SCREEN_COLOR_* */
    UBYTE pixel_format;    /* SCREEN_PIXEL_*, meaningful only for SCREEN_COLOR_TRUECOLOR */
} SCREEN_MODE_DESC;

/*
 * Rejects a descriptor whose fields can't describe a real framebuffer:
 * zero pitch, a pitch too small to hold one scan line at the stated
 * width/depth, or an unrecognized layout/color-model/pixel-format
 * combination.
 */
BOOL screen_mode_desc_valid(const SCREEN_MODE_DESC *desc);

#endif /* SCREEN_MODE_H */
