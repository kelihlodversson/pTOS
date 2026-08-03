/*
 * test-vdi-truecolor.c - host tests for the packed 16-bit backend
 *
 * Links the real selector (vdi/vdi_backend.c), the real truecolor
 * backend (vdi/vdi_backend_truecolor.c) and the shared colour packing
 * (bios/screen_mode.c) natively.  The planar operation table is stubbed
 * out because vdi_backend.c references it.
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "vdi_defs.h"

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

static void make_bgr565(SCREEN_MODE_DESC *mode)
{
    make_rgb565(mode);
    mode->pixel_format = SCREEN_PIXEL_BGR565;
}

/* Stub planar operation table: only the lifecycle slots are filled. */
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

/* the real truecolor table, defined in vdi/vdi_backend_truecolor.c */
extern const VDI_BACKEND_OPS vdi_backend_truecolor_ops;

int main(void)
{
    static const size_t guard = 32;
    SCREEN_MODE_DESC mode;
    Vwk vwk, other;
    UBYTE *buf;
    UBYTE *fb;
    size_t size;
    size_t i;

    make_rgb565(&mode);
    assert(screen_mode_validate(&mode) == TRUE);

    /* guarded framebuffer: canaries before and after the usable area */
    size = (size_t)mode.pitch * mode.height;
    buf = malloc(size + 2 * guard);
    assert(buf != NULL);
    memset(buf, 0x5a, size + 2 * guard);
    fb = buf + guard;

    memset(&vwk, 0, sizeof(vwk));
    assert(vdi_backend_bind(&vwk, &mode, fb) == TRUE);
    assert(vwk.backend.ops == &vdi_backend_truecolor_ops);

    /* pixel address is base + y*pitch + x*2 */
    assert(vdi_pixel_addr(&vwk, 0, 0) == fb);
    assert(vdi_pixel_addr(&vwk, 5, 3) == fb + 3 * mode.pitch + 10);
    assert(vdi_pixel_addr(&vwk, 639, 479) == fb + 479 * mode.pitch + 1278);

    /* native UWORD write and read round-trip */
    assert(vdi_write_pixel(&vwk, 5, 3, 0xf800) == TRUE);
    assert(*(UWORD *)(fb + 3 * mode.pitch + 10) == 0xf800U);
    assert(vdi_read_pixel(&vwk, 5, 3) == 0xf800UL);
    assert(vdi_write_pixel(&vwk, 5, 3, 0x1234) == TRUE);
    assert(vdi_read_pixel(&vwk, 5, 3) == 0x1234UL);

    /* out-of-bounds writes are rejected and the canaries stay intact */
    assert(vdi_write_pixel(&vwk, -1, 0, 0xffff) == FALSE);
    assert(vdi_write_pixel(&vwk, 0, -1, 0xffff) == FALSE);
    assert(vdi_write_pixel(&vwk, 640, 0, 0xffff) == FALSE);
    assert(vdi_write_pixel(&vwk, 0, 480, 0xffff) == FALSE);
    for (i = 0; i < guard; i++) {
        assert(buf[i] == 0x5a);
        assert(buf[guard + size + i] == 0x5a);
    }

    /* RGB conversion endpoints for RGB565 */
    assert(screen_mode_pack_color(&mode, 1000, 0, 0) == 0xf800U);
    assert(screen_mode_pack_color(&mode, 0, 1000, 0) == 0x07e0U);
    assert(screen_mode_pack_color(&mode, 0, 0, 1000) == 0x001fU);

    /* BGR565 swaps red and blue */
    make_bgr565(&mode);
    assert(screen_mode_pack_color(&mode, 1000, 0, 0) == 0x001fU);
    assert(screen_mode_pack_color(&mode, 0, 0, 1000) == 0xf800U);

    /* pseudo-palette entries through the color hook */
    {
        const WORD white[3] = { 1000, 1000, 1000 };
        const WORD green[3] = { 0, 1000, 0 };

        vdi_backend_set_color(&vwk, 1, white);
        assert(vwk.backend.pseudo_palette[1] == 0xffffUL);
        vdi_backend_set_color(&vwk, 2, green);
        assert(vwk.backend.pseudo_palette[2] == 0x07e0UL);
    }

    /* cloning copies the pseudo-palette and the framebuffer pointer */
    memset(&other, 0, sizeof(other));
    assert(vdi_backend_clone(&other, &vwk) == TRUE);
    assert(other.backend.ops == vwk.backend.ops);
    assert(other.backend.framebuffer == fb);
    assert(other.backend.pseudo_palette[2] == 0x07e0UL);

    /* an unbound workstation fails cleanly, never with garbage */
    memset(&other, 0, sizeof(other));
    assert(vdi_pixel_addr(&other, 0, 0) == NULL);
    assert(vdi_read_pixel(&other, 0, 0) == 0UL);
    assert(vdi_write_pixel(&other, 0, 0, 0) == FALSE);

    /* a planar descriptor cannot select the truecolor backend */
    make_rgb565(&mode);
    mode.layout = SCREEN_LAYOUT_PLANAR;
    assert(vdi_backend_kind(&mode) == VDI_BACKEND_UNSUPPORTED);

    free(buf);
    return 0;
}
