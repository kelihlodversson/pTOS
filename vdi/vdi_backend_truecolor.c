/*
 * vdi_backend_truecolor.c - packed 16bpp RGB565 truecolor VDI backend
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "asm.h"
#include "../bios/lineavars.h"
#include "../bios/tosvars.h"
#include "vdi_defs.h"
#include "vdi_backend.h"
#include "kprint.h"

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

/*
 * default_prgb_palette[] converted to RGB565, built lazily on first use.
 * truecolor_get_pixel() searches this space on every call, so it matters
 * that the conversion happens once rather than being redone for every
 * candidate on every pixel read.
 */
static UWORD rgb565_palette[256];
static BOOL rgb565_palette_ready;

static void ensure_rgb565_palette(void)
{
    WORD i;

    if (rgb565_palette_ready)
        return;

    for (i = 0; i < 256; i++)
        rgb565_palette[i] = rgb565_from_prgb(default_prgb_palette[i]);

    rgb565_palette_ready = TRUE;
}

static UWORD truecolor_pixel_for_index(WORD index)
{
    if (index < 0 || index > 255)
        index = 0;

    ensure_rgb565_palette();
    return rgb565_palette[index];
}

/*
 * Public wrapper for callers outside this backend that need to turn a
 * MAP_COL-mapped hardware palette index into the raw RGB565 pixel value
 * this backend would write for it -- e.g. the RPi software mouse cursor
 * in vdi/vdi_mouse.c, which draws by poking pixels directly rather than
 * going through put_pixel()/fill_rect().
 */
UWORD vdi_truecolor_pixel_for_index(WORD index)
{
    return truecolor_pixel_for_index(index);
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
    ensure_rgb565_palette();
    for (i = 0; i < 256; i++) {
        if (rgb565_palette[i] == raw)
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

/*
 * fetch the source word in big-endian (Motorola font) byte order
 *
 * The text source -- the font itself, or the intermediate buffer that
 * pre_blit() filled -- is a big-endian byte stream: normal_blit() in
 * vdi/arch/arm/vdi_tblit.c reads it as a sequence of bytes for that
 * reason, and the m68k assembler reads it as big-endian words.  A native
 * UWORD load byte-swaps adjacent glyphs on little-endian machines (even
 * character codes render their successor, odd ones their predecessor),
 * so assemble the word from its bytes instead.
 */
static UWORD get_src_word(const UBYTE *p)
{
    return (UWORD)(((UWORD)p[0] << 8) | (UWORD)p[1]);
}

/*
 * truecolor text blit: output the current glyph to a packed RGB565 screen
 *
 * port of the upstream screen_blit16() (see vdi_textblit.c in EmuTOS),
 * adapted to the pTOS backend contract: colours come from the backend's
 * own palette conversion instead of CUR_WORK->ext->palette, and line-A
 * variables are read through linea_vars.
 */
static void truecolor_text_blit(LOCALVARS *vars)
{
    UBYTE *p;
    UWORD *q;
    UBYTE *src, *dst;
    UWORD fgcol, bgcol, src_mask, mask, skew_mask;
    WORD h, w, skew, skew_start;

    /*
     * set skew-related values
     *
     * NOTE: we can't test for skewed text using vars->STYLE, since
     * pre_blit() clears F_SKEW and F_THICKEN after it has processed them.
     */
    skew = linea_vars.LOFF + linea_vars.ROFF;
    skew_mask = (UWORD)vars->skew_msk;
    skew_start = vars->height;

    /*
     * the following adjustments are for skewed+outlined text, and make
     * the output almost the same as produced by TOS4.
     *
     * 1. since the source of skewed and/or outlined text must be an
     *    intermediate buffer, SOURCEX *must* be 0, and we force that.
     *    NOTE: in versions of TOS prior to TOS4 (& in TOS4 non-TC
     *    resolutions), this adjustment is not made.  As a result, text
     *    output is typically clipped.
     *
     * 2. a negative value for the nominal destination position is OK,
     *    because outlining has adjusted the starting position of characters
     *    leftwards.  however, such values are prohibited by do_clip(),
     *    which adjusts var->DESTX.  we adjust it back here ...
     *    NOTE: this situation can only happen at the beginning of a
     *    screen line.
     *
     * 3. for bigger fonts, skewing must not start at the bottom of the
     *    buffer, otherwise parts of the outline are clipped too agressively.
     *    at the moment, this fix is a bit of a kludge, though it works well
     *    enough.
     */
    if (skew && (vars->STYLE&F_OUTLINE))
    {
        if (linea_vars.SOURCEX)
        {
            KDEBUG(("SOURCEX (was %d) forced to zero for intermediate buffer\n", linea_vars.SOURCEX));
            linea_vars.SOURCEX = 0;
            vars->tsdad = 0;    /* this was set from SOURCEX in screen_blit() */
        }

        if (linea_vars.DESTX < 0)
        {
            KDEBUG(("vars->DESTX (was %d) set to DESTX (%d)\n", vars->DESTX, linea_vars.DESTX));
            vars->DESTX = linea_vars.DESTX;
        }
        if (vars->height > 8)       /* not a 6-point font */
            skew_start -= OUTLINE_THICKNESS;
    }

    /*
     * set up source stuff
     */
    src = vars->sform;
    src_mask = 0x8000 >> vars->tsdad;

    /*
     * set up destination stuff
     */
    vars->dform = v_bas_ad;
    vars->dform += vars->DESTX * sizeof(WORD);      /* add x coordinate part of addr */
    vars->dform += (UWORD)(vars->DESTY+vars->DELY-1) * (ULONG)linea_vars.v_lin_wr; /* add y coordinate part of addr */
    vars->d_next = -linea_vars.v_lin_wr;
    dst = vars->dform;

    /*
     * set up colours
     */
    fgcol = truecolor_pixel_for_index(vars->forecol);
    bgcol = truecolor_pixel_for_index(0);

    switch(vars->WRT_MODE) {
    /*
     * when called via lineA, modes 4-19 (corresponding to BitBlt modes 0-15)
     * are theoretically possible.  however, at this time we do not support them.
     */
    default:    /* WM_REPLACE */
        for (h = vars->height; h > 0; h--, src += vars->s_next, dst += vars->d_next)
        {
            p = src;
            q = (UWORD *)dst;
            for (w = vars->width, mask = src_mask; w > 0; w--)
            {
                *q++ = (get_src_word(p) & mask) ? fgcol : bgcol;
                rorw1(mask);
                if (mask == 0x8000)
                    p += 2;
            }
            /*
             * special handling for skewed text: since the character cells
             * are effectively slanted, we must shift the starting position
             * of a cell rightwards as we go up the character.
             */
            if (skew && (h <= skew_start))  /* OK to shift box for skewed text? */
            {
                rolw1(skew_mask);
                if (skew_mask & 0x8000)
                {
                    rorw1(src_mask);
                    if (src_mask == 0x8000)
                        src++;
                    dst += sizeof(UWORD);
                }
            }
        }
        break;
    case WM_TRANS:
        for (h = vars->height; h > 0; h--, src += vars->s_next, dst += vars->d_next)
        {
            p = src;
            q = (UWORD *)dst;
            for (w = vars->width, mask = src_mask; w > 0; w--)
            {
                if (get_src_word(p) & mask)
                    *q = fgcol;
                q++;
                rorw1(mask);
                if (mask == 0x8000)
                    p += 2;
            }
            /*
             * see comments for WM_REPLACE (above) for an explanation of
             * the following
             */
            if (skew && (h <= skew_start))  /* OK to shift box for skewed text? */
            {
                rolw1(skew_mask);
                if (skew_mask & 0x8000)
                {
                    rorw1(src_mask);
                    if (src_mask == 0x8000)
                        src++;
                    dst += sizeof(UWORD);
                }
            }
        }
        break;
    case WM_XOR:
        for (h = vars->height; h > 0; h--, src += vars->s_next, dst += vars->d_next)
        {
            p = src;
            q = (UWORD *)dst;
            for (w = vars->width, mask = src_mask; w > 0; w--)
            {
                if (get_src_word(p) & mask)
                    *q = ~*q;
                q++;
                rorw1(mask);
                if (mask == 0x8000)
                    p += 2;
            }
            /*
             * see comments for WM_REPLACE (above) for an explanation of
             * the following
             */
            if (skew && (h <= skew_start))  /* OK to shift box for skewed text? */
            {
                rolw1(skew_mask);
                if (skew_mask & 0x8000)
                {
                    rorw1(src_mask);
                    if (src_mask == 0x8000)
                        src++;
                    dst += sizeof(UWORD);
                }
            }
        }
        break;
    case WM_ERASE:
        for (h = vars->height; h > 0; h--, src += vars->s_next, dst += vars->d_next)
        {
            p = src;
            q = (UWORD *)dst;
            for (w = vars->width, mask = src_mask; w > 0; w--)
            {
                /*
                 * behaviour here differs from TOS 4.04 - for further info,
                 * see the comments in direct_screen_blit16()
                 */
                if (!(get_src_word(p) & mask))
                    *q = fgcol;
                q++;
                rorw1(mask);
                if (mask == 0x8000)
                    p += 2;
            }
            /*
             * see comments for WM_REPLACE (above) for an explanation of
             * the following
             */
            if (skew && (h <= skew_start))  /* OK to shift box for skewed text? */
            {
                rolw1(skew_mask);
                if (skew_mask & 0x8000)
                {
                    rorw1(src_mask);
                    if (src_mask == 0x8000)
                        src++;
                    dst += sizeof(UWORD);
                }
            }
        }
        break;
    }
}

/*
 * apply a VDI boolean raster-op (see BM_* in vdi_raster.h) to a source
 * and destination pixel, a whole 16-bit RGB565 word at a time -- the
 * same semantics the planar blitter emulator's do_blit() applies per
 * bitplane in vdi_raster.c, just applied once per pixel since this
 * backend has no planes to loop over.
 */
static UWORD apply_raster_op(WORD op, UWORD src, UWORD dst)
{
    switch (op & 0x0f) {
    case BM_ALL_WHITE:  return 0x0000;
    case BM_S_AND_D:    return (UWORD)(src & dst);
    case BM_S_AND_NOTD: return (UWORD)(src & ~dst);
    case BM_S_ONLY:     return src;
    case BM_NOTS_AND_D: return (UWORD)(~src & dst);
    case BM_D_ONLY:     return dst;
    case BM_S_XOR_D:    return (UWORD)(src ^ dst);
    case BM_S_OR_D:     return (UWORD)(src | dst);
    case BM_NOT_SORD:   return (UWORD)~(src | dst);
    case BM_NOT_SXORD:  return (UWORD)~(src ^ dst);
    case BM_NOT_D:      return (UWORD)~dst;
    case BM_S_OR_NOTD:  return (UWORD)(src | ~dst);
    case BM_NOT_S:      return (UWORD)~src;
    case BM_NOTS_OR_D:  return (UWORD)(~src | dst);
    case BM_NOT_SANDD:  return (UWORD)~(src & dst);
    case BM_ALL_BLACK:  return 0xffff;
    default:            return dst;
    }
}

/*
 * truecolor raster copy: backs vro_cpyfm()/vrt_cpyfm()/linea_raster()
 * (see cpy_raster() in vdi_raster.c) for the packed RGB565 screen.
 *
 * setup_info() only ever reports s_nxwd/d_nxwd == 2 and plane_ct == 1 for
 * a screen-side MFDB with this backend selected -- a screen word is
 * already one whole pixel, there are no bitplanes to interleave.
 * Anything else (a multi-plane colour-icon MFDB, see gr_colourblit() in
 * aes/gemgraf.c) falls outside what this backend can interpret; rather
 * than misreading plane-interleaved memory as packed pixels, it is
 * silently skipped -- colour icons don't render via this path yet, which
 * is no worse than the memory corruption the planar blitter emulator
 * would otherwise produce here.
 */
static void truecolor_raster_copy(struct raster_t *raster, struct blit_frame *info)
{
    WORD y;

    if (info->d_nxwd != 2)
        return;

    if (raster->transparent) {
        /*
         * 1bpp source (an icon shape/mask) to packed colour destination.
         * fg_col/bg_col are hardware palette indices; raster->mode is the
         * write mode requested by INTIN[0] (MD_REPLACE/TRANS/XOR/ERASE)
         * -- see the switch in cpy_raster() this mirrors, and
         * truecolor_text_blit() above for the same source-bit-walking
         * idiom applied to glyphs instead of icons.
         */
        UWORD fgpix = truecolor_pixel_for_index((WORD)raster->fg_col);
        UWORD bgpix = truecolor_pixel_for_index((WORD)raster->bg_col);

        for (y = 0; y < info->b_ht; y++) {
            const UBYTE *srow = (const UBYTE *)info->s_form
                + (LONG)(info->s_ymin + y) * info->s_nxln;
            UBYTE *drow = (UBYTE *)info->d_form
                + (LONG)(info->d_ymin + y) * info->d_nxln;
            const UBYTE *p = srow + (LONG)(info->s_xmin >> 4) * info->s_nxwd;
            UWORD *q = (UWORD *)(drow + (LONG)info->d_xmin * info->d_nxwd);
            UWORD mask = 0x8000 >> (info->s_xmin & 0x0f);
            WORD x;

            for (x = 0; x < info->b_wd; x++) {
                /*
                 * Icon mask/data words (unlike font glyph bytes -- see
                 * get_src_word() above) are stored as WORD *value*
                 * arrays by the resource compiler (tools/erd.c), which
                 * the target compiler already lays out in its native
                 * byte order. A native dereference here matches that,
                 * and matches how the planar blitter reads the same
                 * MFDB-sourced words (GetMemW() in vdi_raster.c);
                 * get_src_word()'s manual big-endian byte reassembly
                 * would double-handle the byte order and scramble every
                 * word on a little-endian target.
                 */
                BOOL set = (*(const UWORD *)p & mask) != 0;

                switch (raster->mode) {
                case MD_REPLACE:
                    *q = set ? fgpix : bgpix;
                    break;
                case MD_TRANS:
                    if (set)
                        *q = fgpix;
                    break;
                case MD_XOR:
                    if (set)
                        *q = ~*q;
                    break;
                case MD_ERASE:
                    if (!set)
                        *q = bgpix;
                    break;
                }
                q++;

                rorw1(mask);
                if (mask == 0x8000)
                    p += 2;
            }
        }
        return;
    }

    /* COPY RASTER OPAQUE: packed destination word == packed source word */
    if (info->s_nxwd != 2)
        return;

    {
        BOOL forward_y = TRUE, forward_x = TRUE;

        /*
         * Source and destination can be the same screen buffer (e.g. a
         * window drag or scroll) with overlapping rectangles -- pick a
         * scan direction that never overwrites source pixels before
         * they've been read, the same way bit_blt() picks a starting
         * corner for the planar blitter.
         */
        if (info->s_form == info->d_form) {
            if (info->d_ymin > info->s_ymin)
                forward_y = FALSE;
            else if ((info->d_ymin == info->s_ymin) && (info->d_xmin > info->s_xmin))
                forward_x = FALSE;
        }

        for (y = 0; y < info->b_ht; y++) {
            WORD row = forward_y ? y : (info->b_ht - 1 - y);
            const UWORD *srow = (const UWORD *)((const UBYTE *)info->s_form
                + (LONG)(info->s_ymin + row) * info->s_nxln + (LONG)info->s_xmin * info->s_nxwd);
            UWORD *drow = (UWORD *)((UBYTE *)info->d_form
                + (LONG)(info->d_ymin + row) * info->d_nxln + (LONG)info->d_xmin * info->d_nxwd);
            WORD x;

            for (x = 0; x < info->b_wd; x++) {
                WORD col = forward_x ? x : (info->b_wd - 1 - x);
                drow[col] = apply_raster_op(info->op_tab[0], srow[col], drow[col]);
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
    truecolor_text_blit,
    truecolor_raster_copy,
};
