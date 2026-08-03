/*
 * test-vdi-backend.c - host tests for VDI backend selection and lifecycle
 *
 * The real operation tables live in machine-dependent backend files, so
 * this test provides its own stubs with the same names to link
 * vdi_backend.c standalone.
 */
#include <assert.h>
#include <string.h>
#include "vdi_defs.h"

static void make_planar(SCREEN_MODE_DESC *mode)
{
    memset(mode, 0, sizeof(*mode));
    mode->width = 640;
    mode->height = 480;
    mode->pitch = 320UL;
    mode->bits_per_pixel = 4;
    mode->layout = SCREEN_LAYOUT_PLANAR;
    mode->color_model = SCREEN_COLOR_INDEXED;
    mode->pixel_format = SCREEN_PIXEL_NONE;
}

static void make_rgb565(SCREEN_MODE_DESC *mode)
{
    memset(mode, 0, sizeof(*mode));
    mode->width = 640;
    mode->height = 480;
    mode->pitch = 1280UL;
    mode->bits_per_pixel = 16;
    mode->layout = SCREEN_LAYOUT_PACKED;
    mode->color_model = SCREEN_COLOR_TRUECOLOR;
    mode->pixel_format = SCREEN_PIXEL_RGB565;
}

static void make_packed8(SCREEN_MODE_DESC *mode)
{
    memset(mode, 0, sizeof(*mode));
    mode->width = 640;
    mode->height = 480;
    mode->pitch = 640UL;
    mode->bits_per_pixel = 8;
    mode->layout = SCREEN_LAYOUT_PACKED;
    mode->color_model = SCREEN_COLOR_INDEXED;
    mode->pixel_format = SCREEN_PIXEL_NONE;
}

/* Stub operation tables: the drawing slots stay NULL. */
static BOOL stub_open(Vwk *vwk)
{
    (void)vwk;
    return TRUE;
}

static BOOL stub_clone(Vwk *vwk, const Vwk *source)
{
    (void)vwk;
    (void)source;
    return TRUE;
}

static void stub_close(Vwk *vwk)
{
    (void)vwk;
}

const VDI_BACKEND_OPS vdi_backend_planar_ops = {
    .open = stub_open,
    .clone = stub_clone,
    .close = stub_close,
};

const VDI_BACKEND_OPS vdi_backend_truecolor_ops = {
    .open = stub_open,
    .clone = stub_clone,
    .close = stub_close,
};

int main(void)
{
    SCREEN_MODE_DESC mode;

    make_planar(&mode);
    assert(vdi_backend_kind(&mode) == VDI_BACKEND_PLANAR);

    make_rgb565(&mode);
    assert(vdi_backend_kind(&mode) == VDI_BACKEND_TRUECOLOR16);

    make_packed8(&mode);
    assert(vdi_backend_kind(&mode) == VDI_BACKEND_UNSUPPORTED);

    make_rgb565(&mode);
    mode.pixel_format = SCREEN_PIXEL_NONE;
    assert(vdi_backend_kind(&mode) == VDI_BACKEND_UNSUPPORTED);

    make_rgb565(&mode);
    mode.bits_per_pixel = 8;
    assert(vdi_backend_kind(&mode) == VDI_BACKEND_UNSUPPORTED);

    /* packed indexed 8bpp and unknown formats must never select planar */
    make_packed8(&mode);
    assert(vdi_backend_kind(&mode) != VDI_BACKEND_PLANAR);

    make_rgb565(&mode);
    mode.color_model = SCREEN_COLOR_INDEXED;
    mode.pixel_format = SCREEN_PIXEL_NONE;
    assert(vdi_backend_kind(&mode) != VDI_BACKEND_PLANAR);
    assert(vdi_backend_kind(&mode) == VDI_BACKEND_UNSUPPORTED);
    return 0;
}
