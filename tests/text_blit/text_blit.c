/*
 * text_blit.c - regression test for VDI text output on bitplane screens
 *
 * Part of the blitter text epic (#498), issue #499.
 *
 * VDI text output has a software path (the CPU draws the glyphs) and,
 * where the machine has a blitter, will get a hardware path.  This test
 * draws strings through both and compares the screen memory, so that a
 * blitter implementation can be checked against the software one for every
 * style, writing mode, alignment and clip rectangle.  It does so in two
 * ways:
 *
 *  - plain text (no effects, no rotation) is also compared with a small
 *    reference model written in C straight from the font data, so that
 *    the software path itself is checked, not only compared with itself;
 *  - text with effects, rotation and clipping is compared between the two
 *    paths only.
 *
 * The path is selected with Blitmode(): VDI uses the blitter only while it
 * is enabled.  Until text output uses the blitter at all the two renderings
 * are produced by the same code, so the comparison between them cannot
 * fail; the model comparison is what is checked then.
 *
 * The test needs an Atari ST/STE style bitplane screen (1, 2 or 4 planes,
 * 320 or 640 pixels wide: an ST, an STE, a TT in an ST mode, or a Falcon in
 * a mode with 16 colours or fewer) and a blitter; it is skipped everywhere
 * else.
 * It draws on a band of the screen, which is saved and restored.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include "gem.h"

void test_text_blit(void);

#ifndef __mc68000__

void test_text_blit(void)
{
    ptest_begin("text_blit (#499)");
    conws("SKIP (not an m68k machine)\r\n");
}

#else   /* __mc68000__ */

typedef unsigned char U8;

/* the part of the font header the model needs, see include/fonthdr.h */
struct fonthead {
    short font_id;
    short point;
    char name[32];
    unsigned short first_ade, last_ade;
    unsigned short top, ascent, half, descent, bottom;
    unsigned short max_char_width, max_cell_width;
    unsigned short left_offset, right_offset;
    unsigned short thicken, ul_size, lighten, skew, flags;
    const U8 *hor_table;
    const unsigned short *off_table;
    const U8 *dat_table;
    unsigned short form_width, form_height;
};

/* the area of the screen the test draws on */
#define BAND_LINES      80
#define MAX_BPL         320           /* 4 planes of 640 pixels */
#define BAND_MAX        (BAND_LINES * MAX_BPL)
#define TEXT_X          40              /* where text starts, plus a shift */
#define TEXT_Y          40              /* ... and its top line in the band */

/* vst_effects() bits */
#define EF_BOLD         1
#define EF_LIGHT        2
#define EF_SKEW         4
#define EF_UNDER        8
#define EF_OUTLINE      16

/* font header flags, see include/fonthdr.h */
#define F_MONOSPACE     8

/* one string to draw */
typedef struct {
    const char *str;
    short x, y;                 /* v_gtext() position; y is relative to the band */
    short point;                /* vst_point() */
    short effects;
    short mode;                 /* vswr_mode(): 1 replace, 2 trans, 3 xor, 4 rev trans */
    short color;
    short rot;                  /* tenths of a degree */
    short halign;               /* vst_alignment() horizontal: 0 left, 2 right */
    short clip;                 /* if set, clip to cr */
    GRECT cr;                   /* relative to the band */
    short jlen;                 /* if set, v_justified() to this width */
    short jword, jchar;         /* ... with this word and character spacing */
} CASE;

static short vh;
static short scr_w, scr_h, planes, ncolors;
static long bpl;                /* bytes per scanline */
static long band_y;             /* first scanline of the band */
static U8 *band;                /* its screen memory */
static long band_len;

static U8 saved[BAND_MAX] __attribute__((aligned(4)));      /* the screen before the test */
static U8 pattern[BAND_MAX] __attribute__((aligned(4)));    /* what the band is set to before each string */
static U8 snap_cpu[BAND_MAX] __attribute__((aligned(4)));   /* result of the software path */
static U8 snap_blt[BAND_MAX] __attribute__((aligned(4)));   /* result of the blitter path */
static U8 expect[BAND_MAX] __attribute__((aligned(4)));     /* result according to the model */

static const struct fonthead *const *sysfonts;
static short last_point;        /* the point size vst_point() settled on */
static int blitter_was_on;

static int cases_run;
static int cases_failed;
static int reports;

/* ---- output ---- */

static void pnum(long n)
{
    char buf[14];
    char *p = buf + sizeof(buf) - 1;
    unsigned long u;

    *p = '\0';
    if (n < 0)
    {
        conws("-");
        u = 0UL - (unsigned long)n;
    }
    else
        u = (unsigned long)n;
    do
    {
        *--p = (char)('0' + (u % 10));
        u /= 10;
    } while (u);
    conws(p);
}

static void pnums(const char *label, long n)
{
    conws(" ");
    conws(label);
    conws("=");
    pnum(n);
}

static void report(const char *kind, const CASE *c, long off, int a, int b)
{
    if (reports >= 8)
        return;
    reports++;
    conws("\r\n    ");
    conws(kind);
    conws(": \"");
    conws(c->str);
    conws("\"");
    pnums("x", c->x);
    pnums("pt", c->point);
    pnums("fx", c->effects);
    pnums("mode", c->mode);
    pnums("col", c->color);
    pnums("rot", c->rot);
    pnums("al", c->halign);
    if (c->clip)
    {
        conws(" clip=");
        pnum(c->cr.x);
        conws(",");
        pnum(c->cr.y);
        conws(",");
        pnum(c->cr.w);
        conws(",");
        pnum(c->cr.h);
    }
    pnums("line", off / bpl);
    pnums("byte", off % bpl);
    pnums("got", a);
    pnums("want", b);
}

/* ---- the machine ---- */

static const struct fonthead *const *get_sysfonts(void)
{
    register const struct fonthead *const *fonts __asm__("a1");

    /* Line-A initialisation: a1 points to the three system font headers */
    __asm__ volatile (".word 0xa000" : "=a"(fonts) : : "d0", "d1", "d2", "a0", "a2", "memory", "cc");
    return fonts;
}

/* Cursconf() is a macro with a value too */
static void set_cursor(int on)
{
    long old;

    old = Cursconf(on ? 1 : 0, 0);
    (void)old;
}

/* Blitmode() is a macro with a value; this one discards it */
static void set_blitter(int on)
{
    long old;

    old = Blitmode(on);
    (void)old;
}

static int setup(void)
{
    WORD out[57];
    WORD cw, ch, bw, bh;
    WORD aes_handle;

    if ((Blitmode(-1) & 2) == 0)
        return -1;              /* no blitter */
    blitter_was_on = Blitmode(-1) & 1;

    if (appl_init() < 0)
        return -2;
    aes_handle = graf_handle(&cw, &ch, &bw, &bh);
    vh = v_opnvwk(aes_handle, out);
    if (vh <= 0)
        return -2;
    scr_w = out[0] + 1;
    scr_h = out[1] + 1;
    vq_extnd(vh, 1, out);
    planes = out[4];
    ncolors = 1 << planes;

    if ((planes != 1 && planes != 2 && planes != 4) || (scr_w != 320 && scr_w != 640))
        return -3;
    bpl = (long)(scr_w >> 4) * 2 * planes;
    if (bpl * BAND_LINES > BAND_MAX || scr_h < 2 * BAND_LINES)
        return -3;

    band_y = scr_h - BAND_LINES - 8;
    band = (U8 *)Logbase() + band_y * bpl;
    band_len = bpl * BAND_LINES;
    sysfonts = get_sysfonts();
    return 0;
}

static void make_pattern(void)
{
    unsigned long seed = 0x2545F491UL;
    long i;

    for (i = 0; i < band_len; i++)
    {
        seed = seed * 69069UL + 1;
        pattern[i] = (U8)(seed >> 24);
    }
}

/*
 * the buffers are long aligned and a band is a whole number of longs,
 * which makes these four times faster than byte by byte
 */
static void copy(U8 *dst, const U8 *src)
{
    long *d = (long *)dst;
    const long *s = (const long *)src;
    long i;

    for (i = band_len >> 2; i > 0; i--)
        *d++ = *s++;
}

static long first_diff(const U8 *a, const U8 *b)
{
    const long *la = (const long *)a;
    const long *lb = (const long *)b;
    long i, n = band_len >> 2;

    for (i = 0; i < n; i++)
        if (la[i] != lb[i])
        {
            long j;

            for (j = i * 4; ; j++)
                if (a[j] != b[j])
                    return j;
        }
    return -1;
}

/* ---- drawing ---- */

static void draw(const CASE *c)
{
    GRECT cr;

    vswr_mode(vh, c->mode);
    vst_color(vh, c->color);
    last_point = vst_point(vh, c->point);
    vst_effects(vh, c->effects);
    vst_rotation(vh, c->rot);
    vst_alignment(vh, c->halign, 5);    /* top of the cell */
    if (c->clip)
    {
        cr = c->cr;
        cr.y += (WORD)band_y;
        vs_clip(vh, 1, &cr);
    }
    if (c->jlen)
        v_justified(vh, c->x, (WORD)(band_y + c->y), c->str, c->jlen, c->jword, c->jchar);
    else
        v_gtext(vh, c->x, (WORD)(band_y + c->y), c->str);
    if (c->clip)
        vs_clip(vh, 0, &cr);
}

/* ---- the reference model ---- */

static const struct fonthead *find_font(short point)
{
    int i;

    for (i = 0; i < 3; i++)
        if (sysfonts[i]->point == point)
            return sysfonts[i];
    return sysfonts[1];
}

/*
 * the pixel value (hardware colour register) of a VDI colour index, see
 * MAP_COL_ROM and init_colors() in vdi/vdi_col.c: pen 1 is the last
 * register whatever the number of colours
 */
static int pixel_value(int color)
{
    static const U8 map_rom[16] =
        { 0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13 };

    if (color == 1)
        return ncolors - 1;
    return map_rom[color & 15] & (ncolors - 1);
}

/* set the pixel at (x, y) of the band in the planar buffer as the mode says */
static void model_pixel(U8 *buf, const CASE *c, int x, int y, int glyph_bit)
{
    long off;
    U8 mask;
    int p, fg;

    if (x < 0 || x >= scr_w || y < 0 || y >= BAND_LINES)
        return;
    if (c->clip)
    {
        if (x < c->cr.x || x >= c->cr.x + c->cr.w || y < c->cr.y || y >= c->cr.y + c->cr.h)
            return;
    }

    off = (long)y * bpl + (long)(x >> 4) * planes * 2 + ((x & 8) ? 1 : 0);
    mask = (U8)(0x80 >> (x & 7));

    for (p = 0; p < planes; p++)
    {
        U8 *b = &buf[off + 2 * p];

        fg = (pixel_value(c->color) >> p) & 1;
        switch (c->mode) {
        case 1:                 /* replace */
            *b = (U8)((*b & ~mask) | ((glyph_bit && fg) ? mask : 0));
            break;
        case 2:                 /* transparent */
            if (glyph_bit)
                *b = (U8)((*b & ~mask) | (fg ? mask : 0));
            break;
        case 3:                 /* xor */
            if (glyph_bit)
                *b ^= mask;
            break;
        default:                /* reverse transparent */
            if (!glyph_bit)
                *b = (U8)((*b & ~mask) | (fg ? mask : 0));
            break;
        }
    }
}

/* the width of a plain string in pixels */
static int string_width(const CASE *c)
{
    const struct fonthead *f = find_font(c->point);
    const char *s;
    int w = 0;

    for (s = c->str; *s; s++)
    {
        unsigned ch = (U8)*s;

        if (ch < f->first_ade || ch > f->last_ade)
            ch = '?';
        ch -= f->first_ade;
        w += f->off_table[ch + 1] - f->off_table[ch];
    }
    return w;
}

/*
 * what text should look like: plain text, also clipped, and, not clipped,
 * text in bold, light and skewed, as VDI draws them (see normal_blit() in
 * vdi/arch/m68k/vdi_tblit.S), with none of its code.  Lines are styled
 * going up from the bottom one.  Bold keeps the glyph as wide as it is
 * less the weight (a bold glyph is wider by the weight, unless the font is
 * monospaced) and smears it right by the weight.  Skew starts a line one
 * pixel further right than the one below it whenever the bit of the
 * font's skew mask for it is set, the most significant bit being the
 * bottom line's, and writes it as wide as the glyph; the text starts left
 * by the font's left and right offsets (top alignment).  Light ANDs each
 * line with the font's light mask as it lies on the screen, the mask
 * being rotated one bit left on each line going up.  No rotation, no
 * right alignment with effects.
 */
static void model_text(U8 *buf, const CASE *c)
{
    const struct fonthead *f = find_font(last_point);
    const char *s;
    int x = c->x;
    int weight = (c->effects & EF_BOLD) ? f->thicken : 0;
    int extra = (weight && !(f->flags & F_MONOSPACE)) ? weight : 0;

    if (c->halign == 2)
        x -= string_width(c);
    if (c->effects & EF_SKEW)
        x -= f->left_offset + f->right_offset;

    for (s = c->str; *s; s++)
    {
        unsigned ch = (U8)*s;
        int gx0, w, keep, k, i, j, shift;
        unsigned short lite;
        U8 g[32];

        if (ch < f->first_ade || ch > f->last_ade)
            ch = '?';
        ch -= f->first_ade;
        gx0 = f->off_table[ch];
        w = f->off_table[ch + 1] - gx0 + extra;
        keep = w - weight;
        shift = 0;
        lite = f->lighten;
        for (k = 0; k < f->form_height; k++)
        {
            int r = f->form_height - 1 - k;     /* the line, from the top */

            if ((c->effects & EF_SKEW) && ((f->skew >> (15 - (k & 15))) & 1))
                shift++;
            for (i = 0; i < w && i < 32; i++)
            {
                g[i] = 0;
                for (j = 0; j <= weight && j <= i; j++)
                {
                    int gx = gx0 + i - j;

                    if (i - j < keep)
                        g[i] |= (f->dat_table[(long)r * f->form_width + (gx >> 3)] >> (7 - (gx & 7))) & 1;
                }
            }
            for (i = 0; i < w && i < 32; i++)
            {
                int px = x + shift + i;
                int bit = g[i];

                if (c->effects & EF_LIGHT)
                    bit &= (lite >> (15 - (px & 15))) & 1;
                model_pixel(buf, c, px, c->y + r, bit);
            }
            lite = (unsigned short)((lite << 1) | (lite >> 15));
        }
        x += w;
    }
}

/* the cases model_text() knows besides plain text */
static int modelled(const CASE *c)
{
    return !c->clip && !c->rot && !c->jlen && c->halign == 0
        && !(c->effects & ~(EF_BOLD | EF_LIGHT | EF_SKEW));
}

/*
 * what a clipped case should look like, without trusting the clipping
 * (and the shortcuts VDI takes for text outside the clip rectangle): the
 * same text drawn with no clip rectangle at all, of which only the pixels
 * inside the rectangle are kept; the rest of the band is as it was before.
 * Only valid for effects that work on each pixel on its own: thickening and
 * skewing work on the part of the glyph that is not clipped, and lightening
 * starts its mask at the first row that is not (VDI has always done so),
 * so what they draw inside the rectangle depends on the rectangle.
 */
static void model_clip(U8 *buf, const U8 *ref, const CASE *c)
{
    int x0 = c->cr.x, y0 = c->cr.y;
    int x1 = c->cr.x + c->cr.w, y1 = c->cr.y + c->cr.h;
    int x, y, p;

    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > scr_w)
        x1 = scr_w;
    if (y1 > BAND_LINES)
        y1 = BAND_LINES;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
        {
            long off = (long)y * bpl + (long)(x >> 4) * planes * 2 + ((x & 8) ? 1 : 0);
            U8 mask = (U8)(0x80 >> (x & 7));

            for (p = 0; p < planes; p++)
            {
                long o = off + 2 * p;

                buf[o] = (U8)((buf[o] & ~mask) | (ref[o] & mask));
            }
        }
}

/* ---- one case ---- */

static void check(const CASE *c, int plain)
{
    long d;
    int failed = 0;

    cases_run++;

    copy(band, pattern);
    set_blitter(0);
    draw(c);
    copy(snap_cpu, band);

    copy(band, pattern);
    set_blitter(1);
    draw(c);
    copy(snap_blt, band);

    d = first_diff(snap_cpu, snap_blt);
    if (d >= 0)
    {
        report("blitter differs from CPU", c, d, snap_blt[d], snap_cpu[d]);
        failed = 1;
    }

    if (plain || modelled(c))
    {
        copy(expect, pattern);
        model_text(expect, c);
        d = first_diff(snap_cpu, expect);
        if (d >= 0)
        {
            report("differs from model", c, d, snap_cpu[d], expect[d]);
            failed = 1;
        }
    }

    if (c->clip && !(c->effects & (EF_BOLD | EF_LIGHT | EF_SKEW)))
    {
        CASE open = *c;

        /* the same without a clip rectangle: pixels inside it must agree */
        open.clip = 0;
        copy(band, pattern);
        set_blitter(0);
        draw(&open);
        copy(expect, pattern);
        model_clip(expect, band, c);
        d = first_diff(snap_cpu, expect);
        if (d >= 0)
        {
            report("differs from unclipped", c, d, snap_cpu[d], expect[d]);
            failed = 1;
        }
    }

    if (failed)
        cases_failed++;
}

/*
 * check a case, and, if VDI would draw it with direct_screen_blit()
 * (CONF_WITH_VDI_TEXT_SPEEDUP: no effects, no rotation, not clipped, left
 * aligned, byte aligned, an 8 pixel wide font), also the same pixels
 * right aligned, which VDI draws glyph by glyph through the path that
 * Blitmode() chooses.  Both are worth checking: the shortcut ignores
 * Blitmode(), so on its own it would compare itself with itself.
 */
static void check_cases(const CASE *c, int plain)
{
    CASE twin;

    check(c, plain);
    if (!c->effects && !c->rot && !c->clip && !c->halign && (c->x & 7) == 0
        && find_font(c->point)->max_cell_width == 8)
    {
        twin = *c;
        twin.halign = 2;
        twin.x = (short)(c->x + string_width(c));
        check(&twin, plain);
    }
}

/* ---- the test cases ---- */

static const short points[3] = { 8, 9, 10 };            /* 6x6, 8x8, 8x16 */
static const char *const strings[3] = { "A", "M@w", "ij,;" };

/* clip rectangles relative to a string drawn at TEXT_X, TEXT_Y (+ shift) */
static const short clips[][4] = {
    { 0, 0, 5, 40 },            /* cuts into the first glyph from the right */
    { 3, 0, 40, 40 },           /* from the left */
    { 6, 0, 11, 40 },           /* a slice in the middle */
    { 0, 3, 200, 5 },           /* rows only */
    { 7, 4, 13, 9 },            /* both */
    { 1, 1, 1, 1 },             /* a single pixel */
    { 8, 0, 8, 40 },            /* exactly a byte */
    { 16, 0, 16, 40 },          /* exactly a word */
    { 0, -30, 200, 10 },        /* wholly above the text */
    { 0, 30, 200, 10 },         /* wholly below it */
    { -30, 0, 20, 40 },         /* wholly to its left */
    { 100, 0, 40, 40 },         /* wholly to its right */
    { -1, 0, 1, 40 },           /* the column just before it */
    { 24, 0, 6, 40 },           /* the column just after the 3 wide glyphs */
};
#define NCLIPS ((int)(sizeof(clips) / sizeof(clips[0])))

static void set_clip(CASE *c, int n, int shift)
{
    c->clip = 1;
    c->cr.x = (short)(TEXT_X + shift + clips[n][0]);
    c->cr.y = (short)(TEXT_Y + clips[n][1] - 2);
    c->cr.w = clips[n][2];
    c->cr.h = clips[n][3];
}

/* plain text against the model: every font, mode, colour, alignment, clip */
static void group_plain(void)
{
    CASE c;
    int pt, mode, ci, s, dx, k, ncol;
    short colors[3];

    ncol = 0;
    colors[ncol++] = 0;
    colors[ncol++] = 1;
    if (ncolors > 2)
        colors[ncol++] = (short)(ncolors > 4 ? 5 : 2);

    c.jlen = 0;
    c.rot = 0;
    c.effects = 0;
    c.clip = 0;
    c.halign = 0;
    c.y = TEXT_Y;
    for (pt = 0; pt < 3; pt++)
        for (mode = 1; mode <= 4; mode++)
            for (ci = 0; ci < ncol; ci++)
                for (s = 0; s < 2; s++)
                {
                    c.str = strings[s];
                    c.point = points[pt];
                    c.mode = (short)mode;
                    c.color = colors[ci];
                    for (dx = 0; dx < 18; dx++)
                    {
                        c.x = (short)(TEXT_X + dx);
                        c.clip = 0;
                        check_cases(&c, 1);
                    }
                    /* a few shifts with each clip rectangle */
                    for (k = 0; k < NCLIPS; k++)
                        for (dx = 1; dx < 16; dx += 7)
                        {
                            c.x = (short)(TEXT_X + dx);
                            set_clip(&c, k, dx);
                            check_cases(&c, 1);
                        }
                }
}

/* effects and rotation: the two paths must agree */
static void group_effects(void)
{
    static const short rots[4] = { 0, 900, 1800, 2700 };
    static const short shifts[4] = { 0, 3, 8, 13 };
    CASE c;
    int pt, fx, r, mode, s, d;

    c.jlen = 0;
    c.clip = 0;
    c.halign = 0;
    c.y = TEXT_Y;
    c.color = (short)(ncolors > 2 ? 5 % ncolors : 1);
    for (pt = 1; pt < 3; pt++)
        for (fx = 0; fx < 32; fx++)
            for (r = 0; r < 4; r++)
                for (mode = 1; mode <= 4; mode++)
                {
                    /* all modes are checked with the common effects only */
                    if (mode > 2 && fx != 0 && fx != EF_BOLD && fx != EF_SKEW && fx != EF_OUTLINE)
                        continue;
                    for (s = 1; s < 3; s++)
                        for (d = 0; d < 4; d++)
                        {
                            /*
                             * bold also with glyphs that have a pixel in the
                             * last column and none beside it: a bold glyph
                             * of a monospaced font is no wider than the plain
                             * one, and loses that column before thickening
                             */
                            if (s == 2 && !(fx & EF_BOLD))
                                continue;
                            c.str = s == 2 ? "\xbf\xdfw" : strings[s];
                            c.point = points[pt];
                            c.effects = (short)fx;
                            c.mode = (short)mode;
                            c.rot = rots[r];
                            c.x = (short)(TEXT_X + shifts[d]);
                            check_cases(&c, 0);
                        }
                }
}

/*
 * bold, light and skewed text against the model: on a screen with several
 * planes both paths output skewed text with skewed_text_blit(), so it would
 * otherwise only be compared with itself.  Every font, writing mode, two
 * colours, and shifts that take the slanted lines across a word boundary.
 */
static void group_styled(void)
{
    static const short fxs[] = {
        EF_SKEW, EF_SKEW | EF_LIGHT, EF_SKEW | EF_BOLD, EF_SKEW | EF_BOLD | EF_LIGHT,
        EF_BOLD, EF_LIGHT, EF_BOLD | EF_LIGHT
    };
    CASE c;
    int pt, f, mode, ci, s, dx;
    short colors[2];

    colors[0] = 1;
    colors[1] = (short)(ncolors > 2 ? 5 % ncolors : 0);
    c.jlen = 0;
    c.rot = 0;
    c.clip = 0;
    c.halign = 0;
    c.y = TEXT_Y;
    for (pt = 0; pt < 3; pt++)
        for (f = 0; f < (int)(sizeof(fxs) / sizeof(fxs[0])); f++)
            for (mode = 1; mode <= 4; mode++)
                for (ci = 0; ci < 2; ci++)
                    for (s = 0; s < 2; s++)
                        for (dx = 0; dx < 18; dx++)
                        {
                            c.str = s ? "\xbf\xdfw" : strings[1];
                            c.point = points[pt];
                            c.effects = fxs[f];
                            c.mode = (short)mode;
                            c.color = colors[ci];
                            c.x = (short)(TEXT_X + dx);
                            check(&c, 0);
                        }
}

/* effects with a clip rectangle through the text */
static void group_clipped_effects(void)
{
    static const short fxs[] = {
        0, EF_BOLD, EF_LIGHT, EF_SKEW, EF_UNDER, EF_OUTLINE,
        EF_BOLD | EF_SKEW, EF_SKEW | EF_OUTLINE, EF_BOLD | EF_OUTLINE | EF_SKEW,
        EF_BOLD | EF_LIGHT | EF_SKEW | EF_UNDER | EF_OUTLINE
    };
    CASE c;
    int pt, f, k, mode, dx;

    c.jlen = 0;
    c.rot = 0;
    c.halign = 0;
    c.y = TEXT_Y;
    c.color = (short)(ncolors > 2 ? 5 % ncolors : 1);
    c.str = strings[1];
    for (pt = 1; pt < 3; pt++)
        for (f = 0; f < (int)(sizeof(fxs) / sizeof(fxs[0])); f++)
            for (k = 0; k < NCLIPS; k++)
                for (mode = 1; mode <= 2; mode++)
                    for (dx = 0; dx < 16; dx += 5)
                    {
                        c.point = points[pt];
                        c.effects = fxs[f];
                        c.mode = (short)mode;
                        c.x = (short)(TEXT_X + dx);
                        set_clip(&c, k, dx);
                        check_cases(&c, 0);
                    }
}

/*
 * justified text, CPU vs blitter: the spacing moves the glyphs apart (or,
 * for a width below the natural one, together) a pixel count at a time
 */
static void group_justified(void)
{
    static const char *const jstrings[2] = { "a b c", "Wi .,m x" };
    static const short fxs[] = { 0, EF_LIGHT, EF_UNDER, EF_BOLD };
    static const short deltas[] = { -6, 0, 5, 23, 40 };
    static const short spacing[][2] = { { 1, 0 }, { 0, 1 }, { 1, 1 } };
    CASE c;
    int pt, f, s, dl, sp, dx, k, mode;

    c.rot = 0;
    c.halign = 0;
    c.y = TEXT_Y;
    c.color = (short)(ncolors > 2 ? 5 % ncolors : 1);
    for (pt = 0; pt < 3; pt++)
        for (f = 0; f < (int)(sizeof(fxs) / sizeof(fxs[0])); f++)
            for (mode = 1; mode <= 2; mode++)
                for (s = 0; s < 2; s++)
                    for (dl = 0; dl < (int)(sizeof(deltas) / sizeof(deltas[0])); dl++)
                        for (sp = 0; sp < 3; sp++)
                            for (dx = 0; dx < 16; dx += 5)
                            {
                                c.str = jstrings[s];
                                c.point = points[pt];
                                c.effects = fxs[f];
                                c.mode = (short)mode;
                                c.x = (short)(TEXT_X + dx);
                                c.clip = 0;
                                c.jlen = 0;
                                c.jlen = (short)(string_width(&c) + deltas[dl]);
                                c.jword = spacing[sp][0];
                                c.jchar = spacing[sp][1];
                                check(&c, 0);
                                /* and with a clip rectangle through it */
                                if (dl == 2 || dl == 3)
                                    for (k = 0; k < NCLIPS; k += 2)
                                    {
                                        set_clip(&c, k, dx);
                                        check(&c, 0);
                                    }
                            }
}

static void run_group(const char *name, void (*fn)(void))
{
    cases_run = 0;
    cases_failed = 0;
    reports = 0;

    ptest_begin(name);
    fn();
    if (reports)
        conws("\r\n    ");
    pnum(cases_run);
    conws(" cases, ");
    pnum(cases_failed);
    conws(" failed: ");
    ptest_assert_msg(cases_failed == 0, "output differs, see above");
    ptest_pass();
}

void test_text_blit(void)
{
    int rc;

    rc = setup();
    if (rc)
    {
        ptest_begin("text_blit (#499)");
        conws(rc == -1 ? "SKIP (no blitter)\r\n" :
              rc == -2 ? "SKIP (no VDI)\r\n" : "SKIP (not a screen it can use: ");
        if (rc == -3)
        {
            pnum(scr_w);
            conws("x");
            pnum(scr_h);
            conws(", ");
            pnum(planes);
            conws(" planes)\r\n");
        }
        if (rc != -1)
        {
            if (vh > 0)
                v_clsvwk(vh);
            appl_exit();
        }
        return;
    }

    /*
     * the mouse is hidden, and so is the console's cursor: it blinks by
     * inverting its cell in the screen memory, which would change the
     * band between the two renderings if it were in it (the last text
     * row of ST-HIGH is)
     */
    v_hide_c(vh);
    set_cursor(0);
    copy(saved, band);
    make_pattern();

    run_group("text_blit: plain text matches the model (#499)", group_plain);
    run_group("text_blit: effects and rotation, CPU vs blitter (#499)", group_effects);
    run_group("text_blit: bold, light and skewed text match the model (#554)", group_styled);
    run_group("text_blit: effects and clipping, CPU vs blitter (#499)", group_clipped_effects);
    run_group("text_blit: justified text, CPU vs blitter (#531)", group_justified);

    vst_effects(vh, 0);
    vst_rotation(vh, 0);
    vswr_mode(vh, MD_REPLACE);
    vst_color(vh, 1);
    copy(band, saved);
    set_cursor(1);
    v_show_c(vh, 1);
    set_blitter(blitter_was_on);
    v_clsvwk(vh);
    appl_exit();
}

#endif  /* __mc68000__ */
