/*
 * vdi_backend_truecolor.c - packed 16bpp RGB565 truecolor VDI backend
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "../bios/lineavars.h"
#include "../bios/tosvars.h"
#include "vdi_defs.h"
#include "vdi_backend.h"

/*
 * Default VDI palette, indexes 0-15, as packed 0x00BBGGRR values --
 * mirrors raspi_dflt_palette[0..15] in bios/raspi_screen.c (the
 * standard 16-color VDI palette: white, red, green, yellow, blue,
 * magenta, cyan, ltgray, gray, ltred, ltgreen, ltyellow, ltblue,
 * ltmagenta, ltcyan, black). This is the "default mapping needed by
 * colors already in use" the design calls for -- full vs_color()/
 * vq_color() truecolor semantics are a follow-up.
 */
static const ULONG default_prgb_palette[16] = {
    0x00ffffff, 0x000000ff, 0x0000ff00, 0x0000ffff,
    0x00ff0000, 0x00ff00ff, 0x00ffff00, 0x00bbbbbb,
    0x00888888, 0x000000aa, 0x0000aa00, 0x0000aaaa,
    0x00aa0000, 0x00aa00aa, 0x00aaaa00, 0x00000000
};

static UWORD rgb565_from_prgb(ULONG prgb)
{
    UBYTE r = (UBYTE)(prgb & 0xffUL);
    UBYTE g = (UBYTE)((prgb >> 8) & 0xffUL);
    UBYTE b = (UBYTE)((prgb >> 16) & 0xffUL);

    return (UWORD)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static UWORD truecolor_pixel_for_index(WORD index)
{
    if (index < 0 || index > 15)
        index = 0;

    return rgb565_from_prgb(default_prgb_palette[index]);
}

/*
 * Address calculation for a packed 16bpp (2 bytes/pixel) framebuffer.
 * Fixed at 2 bytes/pixel because this backend is only ever selected for
 * SCREEN_PIXEL_RGB565 (see vdi_backend_select()).
 */
static UWORD *truecolor_get_start_addr(WORD x, WORD y)
{
    UBYTE *addr;

    addr = v_bas_ad;
    addr += (LONG)x * 2;
    addr += (LONG)y * linea_vars.v_lin_wr;
    return (UWORD *)addr;
}

static UWORD truecolor_get_pixel(WORD x, WORD y)
{
    UWORD raw = *truecolor_get_start_addr(x, y);
    WORD i;

    for (i = 0; i < 16; i++) {
        if (rgb565_from_prgb(default_prgb_palette[i]) == raw)
            return (UWORD)i;
    }

    return 0;   /* not one of the default 16 -- report black rather than guess */
}

static void truecolor_put_pixel(WORD x, WORD y, UWORD color)
{
    UWORD *addr = truecolor_get_start_addr(x, y);

    *addr = truecolor_pixel_for_index((WORD)color);
}

static void truecolor_fill_rect(const VwkAttrib *attr, const Rect *rect)
{
    const UWORD patmsk = attr->patmsk;
    UWORD pixel = truecolor_pixel_for_index((WORD)attr->color);
    UBYTE *row = (UBYTE *)truecolor_get_start_addr(0, rect->y1);
    WORD x, y, i;

    for (y = rect->y1; y <= rect->y2; y++, row += linea_vars.v_lin_wr) {
        WORD patind = patmsk & y;   /* starting pattern */
        UWORD pattern = attr->patptr[patind];
        UWORD *dst = (UWORD *)row;

        for (x = rect->x1, i = 0; x <= rect->x2; x++, i++) {
            BOOL set = (pattern & ((1<<15)>>(i & 15))) != 0;

            switch (attr->wrt_mode) {
            case 3:                 /* erase (reverse transparent) mode */
                if (!set)
                    dst[x] = pixel;
                break;
            case 2:                 /* xor mode */
                if (set)
                    dst[x] ^= pixel;
                break;
            case 1:                 /* transparent mode */
                if (set)
                    dst[x] = pixel;
                break;
            default:                /* replace mode */
                dst[x] = set ? pixel : 0;
            }
        }
    }
}

static BOOL truecolor_open(Vwk *vwk)
{
    (void)vwk;
    return TRUE;
}

static void truecolor_close(Vwk *vwk)
{
    (void)vwk;
}

const vdi_backend_ops packed_truecolor_backend_ops = {
    truecolor_open,
    truecolor_close,
    truecolor_get_start_addr,
    truecolor_get_pixel,
    truecolor_put_pixel,
    truecolor_fill_rect,
};
