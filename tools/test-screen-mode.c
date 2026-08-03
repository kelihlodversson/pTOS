/*
 * test-screen-mode.c - host tests for the shared screen mode descriptor
 */
#include <assert.h>
#include <string.h>
#include "screen_mode.h"

static SCREEN_MODE_DESC make_mode(void)
{
    SCREEN_MODE_DESC mode;

    memset(&mode, 0, sizeof(mode));
    mode.width = 640;
    mode.height = 480;
    mode.pitch = 1280UL;
    mode.bits_per_pixel = 16;
    mode.layout = SCREEN_LAYOUT_PACKED;
    mode.color_model = SCREEN_COLOR_TRUECOLOR;
    mode.pixel_format = SCREEN_PIXEL_RGB565;
    return mode;
}

int main(void)
{
    SCREEN_MODE_DESC mode;

    mode = make_mode();
    assert(screen_mode_validate(&mode));
    assert(screen_mode_pack_color(&mode, 1000, 0, 0) == 0xf800U);
    assert(screen_mode_pack_color(&mode, 0, 1000, 0) == 0x07e0U);
    assert(screen_mode_pack_color(&mode, 0, 0, 1000) == 0x001fU);

    mode.pitch = 1279UL;
    assert(!screen_mode_validate(&mode));

    mode = make_mode();
    mode.pixel_format = SCREEN_PIXEL_NONE;
    assert(!screen_mode_validate(&mode));

    mode = make_mode();
    mode.pitch = 0x10000UL;
    assert(!screen_mode_validate(&mode));

    mode = make_mode();
    mode.bits_per_pixel = 4;
    mode.pitch = 320UL;
    mode.layout = SCREEN_LAYOUT_PLANAR;
    mode.color_model = SCREEN_COLOR_INDEXED;
    mode.pixel_format = SCREEN_PIXEL_NONE;
    assert(screen_mode_validate(&mode));

    mode.pitch = 0;
    assert(!screen_mode_validate(&mode));

    mode = make_mode();
    mode.bits_per_pixel = 8;
    mode.pitch = 640UL;
    mode.color_model = SCREEN_COLOR_INDEXED;
    mode.pixel_format = SCREEN_PIXEL_NONE;
    assert(screen_mode_validate(&mode));
    return 0;
}
