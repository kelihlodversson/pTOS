/*
 * vdi_backend_truecolor.c - packed truecolor VDI backend, 16bpp RGB565
 * wrapper around the shared drawing-code template
 * vdi_backend_truecolor_tmpl.c, which it #includes with PIXEL=UWORD and
 * PIXEL_SIZE=2.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "asm.h"
#include "lineavars.h"
#include "tosvars.h"
#include "vdi_defs.h"
#include "vdi_backend.h"
#include "kprint.h"
#include "endian.h"

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

static ULONG xrgb8888_from_prgb(ULONG prgb)
{
    return cpu2le32(0xff000000UL | ((prgb & 0x000000ffUL) << 16)
                    | (prgb & 0x0000ff00UL) | ((prgb & 0x00ff0000UL) >> 16));
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

    for (i = 0; i < 256; i++) {
        if (linea_vars.v_planes == 32) {
            /* XRGB8888: convert 0x00BBGGRR to 0xFFRRGGBB, the DRM
             * XRGB8888 pixel value (little-endian bytes B,G,R,X) */
            vwk->tc_palette[i] = xrgb8888_from_prgb(default_prgb_palette[i]);
        } else {
            vwk->tc_palette[i] = rgb565_from_prgb(default_prgb_palette[i]);
        }
    }

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

/*
 * Linkable backend query for the AES (declared in include/gsxdefs.h):
 * is the current screen workstation driven by the packed-truecolor
 * backend?  Thin wrapper over vdi_screen_is_truecolor(), which the AES
 * cannot call -- that inline lives in vdi_backend.h, a vdi/-private
 * header the AES never includes.  This file is built exactly when
 * CONF_WITH_VDI_BACKEND_TRUECOLOR is set, matching the A1 branch's
 * compile-time guard in aes/gemrslib.c; under backend dispatch it is the
 * runtime check against the selected ops table.
 */
BOOL vdi_truecolor_screen(void)
{
    return vdi_screen_is_truecolor();
}

/*
 * Bytes per packed pixel of the current screen (2 for RGB565, 4 for
 * XRGB8888).  v_planes is the descriptor's bits_per_pixel (lineainit.c),
 * so this is exactly bpp/8.  Used by the software mouse cursor, the AES
 * colour-icon packers and setup_info() to stop hard-coding 2.
 */
UWORD vdi_truecolor_pixel_size(void)
{
    return (UWORD)(linea_vars.v_planes / 8);
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
ULONG vdi_truecolor_pixel_for_index(WORD index)
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

/*
 * VDI-scale (0-1000) component -> 8 bits, rounding division like
 * rgb565_from_vdi() above.  Used by the XRGB8888 packing path.
 */
static UBYTE vdi_from_vdi8(WORD c)
{
    return (UBYTE)(((LONG)c * 255 + 500) / 1000);
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

    if (linea_vars.v_planes == 32) {
        /* XRGB8888: pack 0xFFRRGGBB, the DRM XRGB8888 pixel value
         * (little-endian memory bytes B,G,R,X) */
        ULONG prgb = (ULONG)vdi_from_vdi8(b) | ((ULONG)vdi_from_vdi8(g) << 8)
                   | ((ULONG)vdi_from_vdi8(r) << 16);
        vwk->tc_palette[index] = cpu2le32(prgb | 0xff000000UL);
    } else {
        vwk->tc_palette[index] = rgb565_from_vdi(r, g, b);
    }
}

void vdi_truecolor_get_color(const Vwk *vwk, WORD index, WORD *r, WORD *g, WORD *b)
{
    if (index < 0 || index > 255)
        index = 0;

    if (linea_vars.v_planes == 32) {
        ULONG packed = le2cpu32(vwk->tc_palette[index]);
        *r = (WORD)(((LONG)((packed >> 16) & 0xffUL) * 1000 + 127) / 255);
        *g = (WORD)(((LONG)((packed >>  8) & 0xffUL) * 1000 + 127) / 255);
        *b = (WORD)(((LONG)(packed & 0xffUL) * 1000 + 127) / 255);
    } else {
        vdi_from_rgb565(vwk->tc_palette[index], r, g, b);
    }
}

static void truecolor_ops_set_color(Vwk *vwk, WORD pen, WORD *rgb)
{
    vdi_truecolor_set_color(vwk, MAP_COL[pen], rgb[0], rgb[1], rgb[2]);
}

static void truecolor_ops_get_color(const Vwk *vwk, WORD pen, WORD *rgb)
{
    vdi_truecolor_get_color(vwk, MAP_COL[pen], &rgb[0], &rgb[1], &rgb[2]);
}

/* The drawing code lives in the shared template, instantiated here for
 * 16bpp RGB565.  The 32 bpp XRGB8888 instantiation is
 * vdi_backend_truecolor32.c.  Everything in the template is static; the
 * nine direct-call names below exist only for single-renderer builds. */
#define PIXEL UWORD
#define PIXEL_SIZE 2
#include "vdi_backend_truecolor_tmpl.c"
#undef PIXEL_SIZE
#undef PIXEL

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
    tc_get_start_addr,
    tc_get_pixel,
    tc_put_pixel,
    tc_get_raw_pixel,
    tc_put_raw_pixel,
    truecolor_ops_set_color,
    truecolor_ops_get_color,
#if CONF_VDI_SPARSE_TABLE
    /* The optional slots are left NULL so vdi_backend_ops_init() fills them
     * with the generic defaults -- this exercises issue #138's defaults
     * against the real RGB565 framebuffer. Never in production images. */
    NULL, NULL, NULL, NULL, NULL, NULL,
#else
    tc_fill_rect,
    tc_text_blit,
    tc_raster_copy,
    tc_draw_line,
    tc_search_right,
    tc_search_left,
#endif
    2,                          /* pixel_size */
};

#if !CONF_WITH_VDI_BACKEND_DISPATCH
/*
 * Single-renderer truecolor builds call these directly (see vdi/build.mk
 * comment); under dispatch they are never referenced and the templates'
 * statics are reached through the ops table instead.
 */
UWORD *truecolor_get_start_addr(WORD x, WORD y) { return tc_get_start_addr(x, y); }
UWORD truecolor_get_pixel(WORD x, WORD y) { return tc_get_pixel(x, y); }
void truecolor_put_pixel(WORD x, WORD y, UWORD color) { tc_put_pixel(x, y, color); }
void truecolor_fill_rect(const VwkAttrib *attr, const Rect *rect) { tc_fill_rect(attr, rect); }
UWORD truecolor_draw_line(const Line *line, WORD wrt_mode, UWORD color, UWORD linemask) { return tc_draw_line(line, wrt_mode, color, linemask); }
WORD truecolor_search_right(const VwkClip *clip, WORD x, WORD y, UWORD search_col) { return tc_search_right(clip, x, y, search_col); }
WORD truecolor_search_left(const VwkClip *clip, WORD x, WORD y, UWORD search_col) { return tc_search_left(clip, x, y, search_col); }
void truecolor_raster_copy(struct raster_t *raster, struct blit_frame *info) { tc_raster_copy(raster, info); }
void truecolor_text_blit(LOCALVARS *vars) { tc_text_blit(vars); }
#endif
