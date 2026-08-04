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
 * Default VDI palette, as packed 0x00BBGGRR values -- mirrors the full
 * 256-entry raspi_dflt_palette[] in bios/raspi_screen.c.
 *
 * Callers of put_pixel()/fill_rect() pass hardware palette register
 * indices (0-255, see MAP_COL[] in vdi/vdi_col.c), not VDI pen numbers
 * (0-15) -- vdi_col.c's init_colors() sets MAP_COL[1] to
 * DEV_TAB[13]-1, which is 255 on RPi (256-colour DEV_TAB[13]), so VDI
 * pen 1 (black, the default drawing colour) arrives here as index 255,
 * not 1. Indexing the same 256-entry space the hardware palette used
 * (rather than clamping anything outside 0-15 to index 0, which used
 * to alias pen 1 to default_prgb_palette[0] == white) keeps this
 * consistent with the indexed framebuffer the old 8bpp path used to
 * write into. This is the "default mapping needed by colors already
 * in use" the design calls for -- full vs_color()/vq_color()
 * truecolor semantics are a follow-up.
 */
static const ULONG default_prgb_palette[256] = {
    0x00ffffff, 0x000000ff, 0x0000ff00, 0x0000ffff,
    0x00ff0000, 0x00ff00ff, 0x00ffff00, 0x00bbbbbb,
    0x00888888, 0x000000aa, 0x0000aa00, 0x0000aaaa,
    0x00aa0000, 0x00aa00aa, 0x00aaaa00, 0x00000000,
    0x00ffffff, 0x00ededed, 0x00dddddd, 0x00cccccc,
    0x00bababa, 0x00aaaaaa, 0x00999999, 0x00878787,
    0x00777777, 0x00666666, 0x00545454, 0x00444444,
    0x00333333, 0x00212121, 0x00111111, 0x00000000,
    0x000000ff, 0x001100ff, 0x002100ff, 0x003300ff,
    0x004400ff, 0x005400ff, 0x006600ff, 0x007700ff,
    0x008700ff, 0x009900ff, 0x00aa00ff, 0x00ba00ff,
    0x00cc00ff, 0x00dd00ff, 0x00ed00ff, 0x00ff00ff,
    0x00ff00ed, 0x00ff00dd, 0x00ff00cc, 0x00ff00ba,
    0x00ff00aa, 0x00ff0099, 0x00ff0087, 0x00ff0077,
    0x00ff0066, 0x00ff0054, 0x00ff0044, 0x00ff0033,
    0x00ff0021, 0x00ff0011, 0x00ff0000, 0x00ff1100,
    0x00ff2100, 0x00ff3300, 0x00ff4400, 0x00ff5400,
    0x00ff6600, 0x00ff7700, 0x00ff8700, 0x00ff9900,
    0x00ffaa00, 0x00ffba00, 0x00ffcc00, 0x00ffdd00,
    0x00ffed00, 0x00ffff00, 0x00edff00, 0x00ddff00,
    0x00ccff00, 0x00baff00, 0x00aaff00, 0x0099ff00,
    0x0087ff00, 0x0077ff00, 0x0066ff00, 0x0054ff00,
    0x0044ff00, 0x0033ff00, 0x0021ff00, 0x0011ff00,
    0x0000ff00, 0x0000ff11, 0x0000ff21, 0x0000ff33,
    0x0000ff44, 0x0000ff54, 0x0000ff66, 0x0000ff77,
    0x0000ff87, 0x0000ff99, 0x0000ffaa, 0x0000ffba,
    0x0000ffcc, 0x0000ffdd, 0x0000ffed, 0x0000ffff,
    0x0000edff, 0x0000ddff, 0x0000ccff, 0x0000baff,
    0x0000aaff, 0x000099ff, 0x000087ff, 0x000077ff,
    0x000066ff, 0x000054ff, 0x000044ff, 0x000033ff,
    0x000021ff, 0x000011ff, 0x000000ba, 0x001100ba,
    0x002100ba, 0x003300ba, 0x004400ba, 0x005400ba,
    0x006600ba, 0x007700ba, 0x008700ba, 0x009900ba,
    0x00aa00ba, 0x00ba00ba, 0x00ba00aa, 0x00ba0099,
    0x00ba0087, 0x00ba0077, 0x00ba0066, 0x00ba0054,
    0x00ba0044, 0x00ba0033, 0x00ba0021, 0x00ba0011,
    0x00ba0000, 0x00ba1100, 0x00ba2100, 0x00ba3300,
    0x00ba4400, 0x00ba5400, 0x00ba6600, 0x00ba7700,
    0x00ba8700, 0x00ba9900, 0x00baaa00, 0x00baba00,
    0x00aaba00, 0x0099ba00, 0x0087ba00, 0x0077ba00,
    0x0066ba00, 0x0054ba00, 0x0044ba00, 0x0033ba00,
    0x0021ba00, 0x0011ba00, 0x0000ba00, 0x0000ba11,
    0x0000ba21, 0x0000ba33, 0x0000ba44, 0x0000ba54,
    0x0000ba66, 0x0000ba77, 0x0000ba87, 0x0000ba99,
    0x0000baaa, 0x0000baba, 0x0000aaba, 0x000099ba,
    0x000087ba, 0x000077ba, 0x000066ba, 0x000054ba,
    0x000044ba, 0x000033ba, 0x000021ba, 0x000011ba,
    0x00000077, 0x00110077, 0x00210077, 0x00330077,
    0x00440077, 0x00540077, 0x00660077, 0x00770077,
    0x00770066, 0x00770054, 0x00770044, 0x00770033,
    0x00770021, 0x00770011, 0x00770000, 0x00771100,
    0x00772100, 0x00773300, 0x00774400, 0x00775400,
    0x00776600, 0x00777700, 0x00667700, 0x00547700,
    0x00447700, 0x00337700, 0x00217700, 0x00117700,
    0x00007700, 0x00007711, 0x00007721, 0x00007733,
    0x00007744, 0x00007754, 0x00007766, 0x00007777,
    0x00006677, 0x00005477, 0x00004477, 0x00003377,
    0x00002177, 0x00001177, 0x00000044, 0x00110044,
    0x00210044, 0x00330044, 0x00440044, 0x00440033,
    0x00440021, 0x00440011, 0x00440000, 0x00441100,
    0x00442100, 0x00443300, 0x00444400, 0x00334400,
    0x00214400, 0x00114400, 0x00004400, 0x00004411,
    0x00004421, 0x00004433, 0x00004444, 0x00003344,
    0x00002144, 0x00001144, 0x00ffffff, 0x00000000
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
    if (index < 0 || index > 255)
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

    /*
     * Callers expect a hardware palette register index back (see the
     * comment on default_prgb_palette[] above), not a 0-15 VDI pen
     * number, so this has to search the full 256-entry space to match.
     */
    for (i = 0; i < 256; i++) {
        if (rgb565_from_prgb(default_prgb_palette[i]) == raw)
            return (UWORD)i;
    }

    return 0;   /* not one of the default 256 -- index 0 is white, the closest we can do without guessing */
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
                /*
                 * The planar path XORs the pattern into every plane
                 * unconditionally -- a full bitwise invert, independent
                 * of attr->color (this is how the AES draws rubber-band
                 * selection boxes). XOR-ing in the mapped foreground
                 * pixel instead is not equivalent: it's a no-op for any
                 * pen mapping to 0x0000, and produces an arbitrary
                 * colour rather than an inversion for anything else. So
                 * this ignores attr->color/pixel entirely and inverts,
                 * matching planar's actual semantics.
                 */
                if (set)
                    dst[x] ^= 0xffff;
                break;
            case 1:                 /* transparent mode */
                if (set)
                    dst[x] = pixel;
                break;
            default:                /* replace mode */
                /*
                 * Unset pattern bits paint pen 0 (white by default),
                 * matching the planar path, which writes color index 0
                 * for unset bits -- not raw RGB565 0 (black).
                 */
                dst[x] = set ? pixel : truecolor_pixel_for_index(0);
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
