#ifndef SCREEN_MODE_H
#define SCREEN_MODE_H

#include "portab.h"

/* Screen layout constants */
#define SCREEN_LAYOUT_PLANAR 1U
#define SCREEN_LAYOUT_PACKED 2U

/* Color model constants */
#define SCREEN_COLOR_INDEXED   1U
#define SCREEN_COLOR_TRUECOLOR 2U

/* Pixel format constants */
#define SCREEN_PIXEL_NONE   0U
#define SCREEN_PIXEL_RGB555 1U
#define SCREEN_PIXEL_RGB565 2U
#define SCREEN_PIXEL_BGR555 3U
#define SCREEN_PIXEL_BGR565 4U

/* Screen mode descriptor */
typedef struct screen_mode_desc {
    UWORD width;
    UWORD height;
    ULONG pitch;
    UWORD bits_per_pixel;
    UWORD layout;
    UWORD color_model;
    UWORD pixel_format;
    ULONG flags;
} SCREEN_MODE_DESC;

/* Screen mode validation functions */
BOOL screen_mode_validate(const SCREEN_MODE_DESC *mode);
UWORD screen_mode_pack_color(const SCREEN_MODE_DESC *mode, WORD red, WORD green, WORD blue);

#endif /* SCREEN_MODE_H */