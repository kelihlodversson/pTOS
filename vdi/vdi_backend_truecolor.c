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
#include "vdi_col.h"
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
 * write into. This is only the boot-time default that
 * vdi_truecolor_init_palette() below (issue #89) seeds each workstation's
 * own tc_palette[] from -- vdi_truecolor_set_color() is what lets
 * vs_color() change a workstation's entries away from it at runtime.
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
 * Full-precision packed-0x00BBGGRR -> VDI-scale (0-1000 per component)
 * conversion, for seeding tc_req_col[] (issue #89) -- unlike
 * rgb565_from_prgb() above, this doesn't go through the 5/6/5-bit
 * quantization, since tc_req_col holds "requested", not "actual", values.
 */
static void vdi_from_prgb(ULONG prgb, WORD *r, WORD *g, WORD *b)
{
    UBYTE rb = (UBYTE)(prgb & 0xffUL);
    UBYTE gb = (UBYTE)((prgb >> 8) & 0xffUL);
    UBYTE bb = (UBYTE)((prgb >> 16) & 0xffUL);

    *r = (WORD)(((LONG)rb * 1000 + 127) / 255);
    *g = (WORD)(((LONG)gb * 1000 + 127) / 255);
    *b = (WORD)(((LONG)bb * 1000 + 127) / 255);
}

/*
 * Seeds vwk's pseudo-palette (issue #89) with default_prgb_palette[].
 * Called by init_wk() (vdi_control.c) whenever a workstation is opened,
 * and by physical_vwk_seeded() below for the physical workstation if
 * drawing happens before vdi_v_opnwk() ever runs.
 *
 * Seeds both tc_palette[] (RGB565, hwreg-indexed, what get_pixel()/
 * put_pixel()/vq_color(pen,1) read) and tc_req_col[] (VDI 0-1000 scale,
 * pen-indexed, what vq_color(pen,0) reads) from the same source, via
 * MAP_COL[pen] to find which hwreg's default a given pen currently has --
 * so the two forms of vq_color() agree at boot, before any vs_color()
 * call.
 *
 * The tc_req_col loop is bounded by DEV_TAB[13] (the pen count for the
 * current screen, capped at 256), not a flat 256: MAP_COL[] itself is
 * only sized MAXCOLOURS entries (16 when EXTENDED_PALETTE is off -- e.g.
 * a hypothetical non-RPi build with CONF_WITH_VDI_BACKEND_TRUECOLOR
 * enabled by hand alongside the dispatcher), so indexing it up to 255
 * unconditionally would read out of bounds. tc_palette[] has no such
 * limit -- it is indexed directly by hwreg, not through MAP_COL[] -- so
 * it stays a flat 256. Pens above DEV_TAB[13] are never valid VDI pens
 * (vdi_vs_color()/vdi_vq_color() both reject colnum >= DEV_TAB[13] before
 * ever touching tc_req_col), so leaving them unseeded here is fine.
 */
void vdi_truecolor_init_palette(Vwk *vwk)
{
    WORD i, npens;

    for (i = 0; i < 256; i++)
        vwk->tc_palette[i] = rgb565_from_prgb(default_prgb_palette[i]);

    npens = linea_vars.DEV_TAB[13];
    if (npens < 0)
        npens = 0;
    else if (npens > 256)
        npens = 256;

    for (i = 0; i < npens; i++)
        vdi_from_prgb(default_prgb_palette[MAP_COL[i]],
                      &vwk->tc_req_col[i][0], &vwk->tc_req_col[i][1], &vwk->tc_req_col[i][2]);
}

/*
 * The workstation whose pseudo-palette the drawing primitives below
 * (get_pixel/put_pixel/fill_rect/text_blit/raster_copy/draw_line/
 * search_left/search_right) translate indices through -- none of them
 * take a Vwk*, so vdi_main.c's screen() dispatcher is the sole writer,
 * via vdi_backend_set_active_vwk(), once per VDI call, with the Vwk
 * resolved for that call's handle (or the physical workstation for
 * v_opnwk()/v_opnvwk(), which have no handle yet).
 *
 * Lives here rather than in vdi_backend.c because this file is built
 * whenever CONF_WITH_VDI_BACKEND_TRUECOLOR is set, including the
 * single-renderer RPi default; vdi_backend.c only builds when both
 * renderers are enabled (see vdi/build.mk).
 *
 * This is a fresh pTOS-internal variable rather than linea_vars.CUR_WORK,
 * because CUR_WORK is part of the documented line-A ABI: user code can
 * point it at a fake reduced "workstation" (see the NOTE on Vwk_ in
 * vdi_defs.h), which is safe for CUR_WORK's existing single-WORD use
 * (fill_color, at a fixed low offset) but would be an out-of-bounds read
 * for a 256-entry palette placed well beyond that.
 */
static Vwk *active_vwk;
static BOOL physical_palette_seeded;

/*
 * The physical workstation, with its tc_palette/tc_req_col guaranteed
 * seeded -- even the very first time this is called, which can happen
 * before vdi_v_opnwk() has ever run (Line-A reachable pre-AES, or before
 * any screen() dispatch at all; see the vdi_screen_backend() self-init
 * comment in vdi_control.c for the same situation with .mode/.backend).
 * Idempotent past the first call: init_wk() (vdi_control.c) re-seeds the
 * physical workstation's palette for real whenever vdi_v_opnwk() actually
 * runs, which this flag does not need to track -- it only needs to know
 * whether *some* seeding has happened yet.
 */
static Vwk *physical_vwk_seeded(void)
{
    Vwk *phys = vdi_physical_vwk();

    if (!physical_palette_seeded) {
        vdi_truecolor_init_palette(phys);
        physical_palette_seeded = TRUE;
    }
    return phys;
}

void vdi_backend_set_active_vwk(Vwk *vwk)
{
    active_vwk = vwk;
}

Vwk *vdi_backend_active_vwk(void)
{
    /*
     * vdi_main.c's screen() sets active_vwk to the physical workstation
     * for opcodes with no handle yet (v_opnwk()/v_opnvwk()) as well as
     * for nothing-dispatched-yet -- so "active_vwk is currently the
     * physical workstation" (not just "active_vwk is NULL") is the
     * condition that needs the seeding check, or a program that only
     * ever opens virtual workstations could leave the physical palette
     * BSS-zero (all black) while something reads it in between.
     */
    if (!active_vwk || active_vwk == vdi_physical_vwk())
        return physical_vwk_seeded();

    return active_vwk;
}

static UWORD *active_palette(void)
{
    return vdi_backend_active_vwk()->tc_palette;
}

static UWORD truecolor_pixel_for_index(WORD index)
{
    if (index < 0 || index > 255)
        index = 0;

    return active_palette()[index];
}

/*
 * Public wrapper for callers outside this backend that need to turn a
 * MAP_COL-mapped hardware palette index into the raw RGB565 pixel value
 * this backend would write for it -- currently only the RPi software
 * mouse cursor in vdi/vdi_mouse.c, which draws by poking pixels directly
 * rather than going through put_pixel()/fill_rect().
 *
 * Deliberately reads the *physical* workstation's palette rather than
 * active_vwk: the cursor is a screen-global element (its bg_col/fg_col
 * are hardware-register indices, not tied to any one workstation), and
 * it is drawn from the VBL interrupt (vdi_mouse.c's vb_draw()) as well as
 * from Line-A text output, both of which run independently of -- and can
 * race -- whatever VDI call last set active_vwk. Using the physical
 * workstation keeps the cursor's colors from changing whenever some
 * other (possibly virtual) workstation happens to have been the most
 * recent VDI caller.
 */
UWORD vdi_truecolor_pixel_for_index(WORD index)
{
    if (index < 0 || index > 255)
        index = 0;

    return physical_vwk_seeded()->tc_palette[index];
}

/*
 * VDI-scale (0-1000 per component) <-> RGB565 conversion for
 * vs_color()/vq_color() (issue #89). Uses rounding division rather than
 * rgb565_from_prgb()'s truncating shifts above, since those start from an
 * already-8-bit component -- vs_color() gives full 0-1000 precision that's
 * worth rounding rather than truncating away.
 */
static UWORD rgb565_from_vdi(WORD r, WORD g, WORD b)
{
    UWORD r5 = (UWORD)(((LONG)r * 31 + 500) / 1000);
    UWORD g6 = (UWORD)(((LONG)g * 63 + 500) / 1000);
    UWORD b5 = (UWORD)(((LONG)b * 31 + 500) / 1000);

    return (UWORD)((r5 << 11) | (g6 << 5) | b5);
}

static void vdi_from_rgb565(UWORD raw, WORD *r, WORD *g, WORD *b)
{
    UWORD r5 = (raw >> 11) & 0x1f;
    UWORD g6 = (raw >> 5) & 0x3f;
    UWORD b5 = raw & 0x1f;

    *r = (WORD)(((LONG)r5 * 1000 + 15) / 31);
    *g = (WORD)(((LONG)g6 * 1000 + 31) / 63);
    *b = (WORD)(((LONG)b5 * 1000 + 15) / 31);
}

/*
 * vs_color()/vq_color() pseudo-palette read/write ports for the truecolor
 * backend (issue #89), called from vdi_vs_color()/vdi_vq_color() in
 * vdi_col.c with the vwk those already have from the VDI dispatcher.
 * index is a MAP_COL-mapped hardware palette register index, like every
 * other index this file takes -- not a 0-15 VDI pen number.
 *
 * Genuinely per-workstation: each Vwk carries its own tc_palette[], so a
 * vs_color() on one workstation cannot affect another's rendering, unlike
 * put_pixel()/get_pixel()/etc. above which -- having no Vwk* of their own
 * -- go through vdi_backend_active_vwk() instead.
 */
void vdi_truecolor_set_color(Vwk *vwk, WORD index, WORD r, WORD g, WORD b)
{
    if (index < 0 || index > 255)
        return;

    /*
     * Defend the public contract even though the only current caller
     * (vdi_vs_color()) already clamps: r/g/b feed straight into 5/6-bit
     * field packing below, and an out-of-range value (or a future caller
     * that forgets to clamp) would corrupt the packed RGB565 word rather
     * than just look wrong.
     */
    if (r < 0) r = 0; else if (r > 1000) r = 1000;
    if (g < 0) g = 0; else if (g > 1000) g = 1000;
    if (b < 0) b = 0; else if (b > 1000) b = 1000;

    vwk->tc_palette[index] = rgb565_from_vdi(r, g, b);
}

void vdi_truecolor_get_color(const Vwk *vwk, WORD index, WORD *r, WORD *g, WORD *b)
{
    if (index < 0 || index > 255)
        index = 0;

    vdi_from_rgb565(vwk->tc_palette[index], r, g, b);
}

/*
 * Address calculation for a packed 16bpp (2 bytes/pixel) framebuffer.
 * Fixed at 2 bytes/pixel because this backend is only ever selected for
 * SCREEN_PIXEL_RGB565 (see vdi_backend_select()).
 */
UWORD *truecolor_get_start_addr(WORD x, WORD y)
{
    UBYTE *addr;

    addr = v_bas_ad;
    addr += (LONG)x * 2;
    addr += (LONG)y * linea_vars.v_lin_wr;
    return (UWORD *)addr;
}

UWORD truecolor_get_pixel(WORD x, WORD y)
{
    UWORD raw = *truecolor_get_start_addr(x, y);
    const UWORD *palette = active_palette();
    WORD i;

    /*
     * Callers expect a hardware palette register index back (see the
     * comment on default_prgb_palette[] above), not a 0-15 VDI pen
     * number, so this has to search the full 256-entry space to match.
     */
    for (i = 0; i < 256; i++) {
        if (palette[i] == raw)
            return (UWORD)i;
    }

    return 0;   /* not one of the active palette's 256 -- index 0 is white, the closest we can do without guessing */
}

void truecolor_put_pixel(WORD x, WORD y, UWORD color)
{
    UWORD *addr = truecolor_get_start_addr(x, y);

    *addr = truecolor_pixel_for_index((WORD)color);
}

void truecolor_fill_rect(const VwkAttrib *attr, const Rect *rect)
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
void truecolor_text_blit(LOCALVARS *vars)
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
void truecolor_raster_copy(struct raster_t *raster, struct blit_frame *info)
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

/*
 * truecolor draw_line: backs abline() (vdi_line.c) for non-horizontal
 * lines on the packed RGB565 screen (abline() handles horizontal lines
 * itself, via fill_rect(), and never calls this for one).
 *
 * A packed screen has no bitplanes to loop over, so this is a single-pass
 * Bresenham writing one whole pixel per step, unlike the planar
 * implementation's per-bitplane loop (planar_draw_line() in vdi_line.c).
 * The write-mode semantics are derived directly from that function's
 * per-bitplane logic, composed across a whole pixel instead of one bit
 * per plane:
 *  - replace: every step writes either the line color or (at a line-
 *    style gap) palette index 0 -- matching replace mode's planar
 *    behaviour of also clearing gaps, not leaving them untouched.
 *  - transparent (or): only "on" steps write the line color; gaps are
 *    untouched.
 *  - xor: only "on" steps invert whatever pixel is already there,
 *    regardless of the requested color -- matching the planar loop's
 *    unconditional bit flip (compare truecolor_fill_rect()'s xor case).
 *  - reverse transparent (not): only "on" steps write the complement of
 *    the line color's palette index, mapped through the palette; gaps
 *    are untouched.
 */
UWORD truecolor_draw_line(const Line *line, WORD wrt_mode, UWORD color, UWORD linemask)
{
    UWORD x1, y1, x2, y2;
    WORD dx, dy, loopcnt;
    LONG yinc;
    UBYTE *adr;
    UWORD fgpix, bg0pix, notpix;

    if (line->x2 < line->x1) {
        x1 = line->x2; y1 = line->y2;
        x2 = line->x1; y2 = line->y1;
    } else {
        x1 = line->x1; y1 = line->y1;
        x2 = line->x2; y2 = line->y2;
    }

    dx = x2 - x1;
    dy = y2 - y1;

    fgpix = truecolor_pixel_for_index((WORD)color);
    bg0pix = truecolor_pixel_for_index(0);
    notpix = truecolor_pixel_for_index((WORD)(~color & 0xff));

    if (dy < 0) {
        dy = -dy;
        yinc = -(LONG)linea_vars.v_lin_wr;
    } else {
        yinc = (LONG)linea_vars.v_lin_wr;
    }
    adr = (UBYTE *)truecolor_get_start_addr(x1, y1);

    if (dx >= dy) {
        WORD eps = -dx, e1 = 2*dy, e2 = 2*dx;

        for (loopcnt = dx; loopcnt >= 0; loopcnt--) {
            UWORD *p = (UWORD *)adr;

            rolw1(linemask);
            switch (wrt_mode) {
            case 3: if (linemask & 1) *p = notpix; break;
            case 2: if (linemask & 1) *p ^= 0xffff; break;
            case 1: if (linemask & 1) *p = fgpix; break;
            default: *p = (linemask & 1) ? fgpix : bg0pix; break;
            }
            adr += 2;
            eps += e1;
            if (eps >= 0) {
                eps -= e2;
                adr += yinc;
            }
        }
    } else {
        WORD eps = -dy, e1 = 2*dx, e2 = 2*dy;

        for (loopcnt = dy; loopcnt >= 0; loopcnt--) {
            UWORD *p = (UWORD *)adr;

            rolw1(linemask);
            switch (wrt_mode) {
            case 3: if (linemask & 1) *p = notpix; break;
            case 2: if (linemask & 1) *p ^= 0xffff; break;
            case 1: if (linemask & 1) *p = fgpix; break;
            default: *p = (linemask & 1) ? fgpix : bg0pix; break;
            }
            adr += yinc;
            eps += e1;
            if (eps >= 0) {
                eps -= e2;
                adr += 2;
            }
        }
    }

    return linemask;
}

/*
 * truecolor_search_right/truecolor_search_left: scan a horizontal run of
 * matching color on the packed RGB565 screen, for contourfill()'s
 * seed-fill (see end_pts() in vdi_fill.c and the vdi_backend_ops comment
 * in vdi_backend.h).
 *
 * search_col is a MAP_COL-mapped hardware palette index, like
 * get_pixel()'s return value -- converted to its raw RGB565 pixel once,
 * up front, rather than calling get_pixel() (and paying its 256-entry
 * reverse palette search) again for every pixel of a scan that can span
 * the whole screen width.
 */
WORD truecolor_search_right(const VwkClip *clip, WORD x, WORD y, UWORD search_col)
{
    UWORD pixel = truecolor_pixel_for_index((WORD)search_col);
    const UWORD *addr = truecolor_get_start_addr(x, y);

    while (x++ < clip->xmx_clip) {
        if (*++addr != pixel)
            break;
    }
    return x - 1;       /* output x coord -1 to endxright. */
}

WORD truecolor_search_left(const VwkClip *clip, WORD x, WORD y, UWORD search_col)
{
    UWORD pixel = truecolor_pixel_for_index((WORD)search_col);
    const UWORD *addr = truecolor_get_start_addr(x, y);

    while (x-- > clip->xmn_clip) {
        if (*--addr != pixel)
            break;
    }
    return x + 1;       /* output x coord + 1 to endxleft. */
}

static UWORD truecolor_get_raw_pixel(WORD x, WORD y)
{
    return *truecolor_get_start_addr(x, y);
}

static void truecolor_put_raw_pixel(WORD x, WORD y, UWORD raw)
{
    *truecolor_get_start_addr(x, y) = raw;
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

vdi_backend_ops packed_truecolor_backend_ops = {
    truecolor_open,
    truecolor_close,
    truecolor_get_start_addr,
    truecolor_get_pixel,
    truecolor_put_pixel,
    truecolor_get_raw_pixel,
    truecolor_put_raw_pixel,
#if CONF_VDI_SPARSE_TABLE
    /* The optional slots are left NULL so vdi_backend_ops_init() fills them
     * with the generic defaults -- this exercises issue #138's defaults
     * against the real RGB565 framebuffer. Never in production images. */
    NULL, NULL, NULL, NULL, NULL, NULL,
#else
    truecolor_fill_rect,
    truecolor_text_blit,
    truecolor_raster_copy,
    truecolor_draw_line,
    truecolor_search_right,
    truecolor_search_left,
#endif
};
