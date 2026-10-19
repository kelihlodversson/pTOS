/*
 * gdos_answer.c - regression test for the built-in GDOS's answers (#582)
 *
 * With scalable fonts (CONF_WITH_VDI_SCALABLE_FONTS), pTOS answers as
 * SpeedoGDOS does once the AUTO folder has run: vq_gdos() (trap #2 with
 * d0 = -2) returns '_FSM', and the FSMC cookie points to a structure with
 * the magic '_SPD' and a version of at least 4.00.  Without them,
 * vq_gdos() leaves -2 and there is no FSMC cookie.  The suite checks that
 * the two agree, and which of the two it found.  Any other answer is a
 * GDOS loaded from disk, which the built-in one leaves alone: nothing is
 * checked then.  So is an FSMC cookie of another GDOS that also answers
 * '_FSM' (SpeedoGDOS 5, FSMGDOS): the built-in one is '_SPD' version 4.00.
 *
 * With the built-in GDOS, it also opens a workstation and checks
 * vst_load_fonts() and vqt_name(): the scalable fonts come after the
 * system fonts, flagged as outline fonts (intout[33] = 1) with ids from
 * 10000 on, and a second vst_load_fonts() adds none.  How many there are
 * depends on the fonts in \GEMSYS\FONTS of the test disk
 * (tools/run-regression-tests.sh copies DejaVu Sans, Serif and Sans Mono,
 * TrueType, and Inter and Loma, OpenType with CFF outlines, Loma with an
 * ascender taller than its em, there when the host has them).  Each is
 * selected, sized and drawn (#576): vst_height() and vst_point() give
 * exactly the height and the size asked for, a monospaced one stays
 * monospaced in bold, and what each
 * drew is read back to see that
 * rotation, clipping, the effects, XOR mode and alignment to the ascent
 * and the half line apply to it, and the first is also drawn big, rotated
 * with effects, which takes a bigger text scratch buffer than the default
 * one.  The pixels are only checked where the system font's text can be
 * read back with v_get_pixel(); where it cannot, the result line says so.
 *
 * SpeedoGDOS's and NVDI's calls for the fonts (#577) are checked on the
 * first scalable font and the system font: vqt_ext_name()'s formats,
 * vqt_xfntinfo()'s names, file and sizes, vst_name() and
 * vqt_name_and_id(), vqt_fontheader(), and the cache calls; vst_arbpt()
 * and vst_arbpt32(), vst_setsize(), vst_width(), vst_skew(),
 * vqt_advance(), vqt_f_extent() and vqt_real_extent(), v_ftext() and
 * v_ftext_offset(); vst_map_mode() with text in BICS, Unicode (UCS-2
 * and UTF-16, a surrogate pair being one character) in the scalable font
 * and the system font, and vqt_char_index(); vst_kern(), vqt_pairkern()
 * and vst_track_offset(), which the width of text follows.
 *
 * x86-64 programs cannot call the VDI at all (trap class 2 is refused
 * from ring 3), so it is skipped there.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <stddef.h>
#include <string.h>

#define EINVFN          (-32L)
#define SS_GETCOOKIE    8
#define TAG(a, b, c, d) (((long)(a) << 24) | ((long)(b) << 16) | ((long)(c) << 8) | (d))

void test_gdos_answer(void);

#if !defined(__mc68000__) && !defined(__arm__)

void test_gdos_answer(void)
{
    ptest_begin("gdos_answer (#582)");
    conws("SKIP (no VDI for programs here)\r\n");
}

#else

struct cookie
{
    long tag;
    long value;
};

struct fsmc
{
    unsigned long magic;
    unsigned short version;
    unsigned short quality;
};

static short contrl[16], intin[64], ptsin[16], intout[64], ptsout[16];
static void *vdipb[5] = { contrl, intin, ptsin, intout, ptsout };

static short aes_control[5], aes_global[15];
static long aes_addr[2];
static void *aespb[6] = { aes_control, aes_global, intin, intout, aes_addr, aes_addr };

/* an AES call: without an AES, trap #2 leaves everything as it was */
static void aes(short op, short nintin, short nintout)
{
    aes_control[0] = op;
    aes_control[1] = nintin;
    aes_control[2] = nintout;
    aes_control[3] = 0;
    aes_control[4] = 0;
#ifdef __mc68000__
    {
        register long d0 __asm__("d0") = 200;
        register void *d1 __asm__("d1") = aespb;

        __asm__ volatile (
            "trap #2"
            : "+d" (d0), "+d" (d1)
            :
            : "d2", "a0", "a1", "memory", "cc");
    }
#else
    {
        register long r0 __asm__("r0") = 200;
        register void *r1 __asm__("r1") = aespb;

        __asm__ volatile (
            "svc 2"
            : "+r" (r0), "+r" (r1)
            :
            : "r2", "r3", "r12", "lr", "memory", "cc");
    }
#endif
}

/* a VDI call, with contrl[] already filled in */
static void vdi(void)
{
#ifdef __mc68000__
    register long d0 __asm__("d0") = 0x73;
    register void *d1 __asm__("d1") = vdipb;

    __asm__ volatile (
        "trap #2"
        : "+d" (d0), "+d" (d1)
        :
        : "d2", "a0", "a1", "memory", "cc");
#else
    register long r0 __asm__("r0") = 0x73;
    register void *r1 __asm__("r1") = vdipb;

    __asm__ volatile (
        "svc 2"
        : "+r" (r0), "+r" (r1)
        :
        : "r2", "r3", "r12", "lr", "memory", "cc");
#endif
}

static void vdi_op(short op, short nintin, short handle)
{
    contrl[0] = op;
    contrl[1] = 0;
    contrl[3] = nintin;
    contrl[5] = 0;
    contrl[6] = handle;
    vdi();
}

static void pnum(long n)
{
    char buf[12], *p = buf + sizeof(buf) - 1;

    *p = '\0';
    do
        *--p = (char)('0' + n % 10);
    while (n /= 10);
    conws(p);
}

/*
 * The scalable fonts on a workstation.  Run before ptest_begin(): opening
 * and closing the screen workstation writes to the console, which would
 * split the suite's result line.  The first failure is kept for later.
 */
static const char *font_failure;
static short fonts_loaded;
static int pixels_checked;
static short monospaced_fonts;

static void font_check(int ok, const char *msg)
{
    if (!ok && !font_failure)
        font_failure = msg;
}

static short ptsin_op(short op, short nptsin, short nintin, short handle)
{
    contrl[0] = op;
    contrl[1] = nptsin;
    contrl[3] = nintin;
    contrl[5] = 0;
    contrl[6] = handle;
    vdi();
    return intout[0];
}

/* the width of n times ch in the current font */
static short extent_of(short handle, short ch, short n)
{
    short i;

    for (i = 0; i < n; i++)
        intin[i] = ch;
    ptsin_op(116, 0, n, handle);        /* vqt_extent() */
    return ptsout[2] - ptsout[0];
}

/* the width of "Ag" in the current font */
static short extent_ag(short handle)
{
    intin[0] = 'A';
    intin[1] = 'g';
    ptsin_op(116, 0, 2, handle);        /* vqt_extent() */
    return ptsout[2] - ptsout[0];
}

/*
 * A scalable font selected, sized and drawn (#576): sizes are exact,
 * bigger at a bigger size, and the text path draws them.
 */
static void check_sizes(short handle, short system_fonts)
{
    short id, top12, top48, w12, w48;

    intin[0] = system_fonts + 1;
    vdi_op(130, 1, handle);             /* vqt_name(): the first scalable font */
    id = intout[0];

    intin[0] = id;
    vdi_op(21, 1, handle);              /* vst_font() */
    font_check(intout[0] == id, "vst_font() did not select the scalable font");

    intin[0] = 12;
    vdi_op(107, 1, handle);             /* vst_point(12) */
    top12 = ptsout[1];
    w12 = extent_ag(handle);
    font_check(top12 > 0 && w12 > 0, "a scalable font at 12 points has no size");

    intin[0] = 48;
    vdi_op(107, 1, handle);             /* vst_point(48) */
    font_check(intout[0] >= 44 && intout[0] <= 52, "vst_point(48) did not set 48 points");
    top48 = ptsout[1];
    w48 = extent_ag(handle);
    font_check(top48 > top12 * 3 && w48 > w12 * 3,
               "a scalable font at 48 points is not four times 12 points");

    /* exact heights, unlike a bitmap font's nearest one: check_heights() */

    /*
     * Many sizes, one after another: more than the cache holds, so older
     * ones are dropped and every size still comes out, each bigger.
     */
    {
        short pt, last = 0;

        for (pt = 8; pt <= 160; pt += 8)
        {
            intin[0] = pt;
            vdi_op(107, 1, handle);     /* vst_point() */
            font_check(ptsout[1] > last, "a bigger size of a scalable font is not bigger");
            last = ptsout[1];
        }
    }

    intin[0] = 1;
    vdi_op(21, 1, handle);              /* back to the system font */
}

/*
 * Drawing a scalable font, read back with v_get_pixel(): the text goes
 * through the bitmap text path, so rotation, clipping, the effects and
 * the writing modes apply to it as to a bitmap font (#576).
 */
struct ink
{
    long count;                         /* pixels not in colour 0 */
    short x0, y0, x1, y1;               /* the box around them */
};

static short ink_cx, ink_cy, ink_r;     /* the text's origin, and how far it reaches */
static short ink_text[2] = { 'A', 'g' };    /* what is drawn */
static short ink_len = 2;
static short ink_step = 1;              /* every pixel, or every other one */

static void ink_scan(short handle, short x0, short y0, short x1, short y1,
                     struct ink *ink)
{
    short x, y;

    ink->count = 0;
    ink->x0 = ink->y0 = 32767;
    ink->x1 = ink->y1 = -1;
    for (y = y0; y <= y1; y += ink_step)
        for (x = x0; x <= x1; x += ink_step)
        {
            ptsin[0] = x;
            ptsin[1] = y;
            ptsin_op(105, 1, 0, handle);    /* v_get_pixel() */
            if (intout[1] == 0)
                continue;
            ink->count++;
            if (x < ink->x0)
                ink->x0 = x;
            if (x > ink->x1)
                ink->x1 = x;
            if (y < ink->y0)
                ink->y0 = y;
            if (y > ink->y1)
                ink->y1 = y;
        }
}

/* clear the area around the origin, draw ink_text there and see what came out */
static void ink_draw(short handle, short angle, short effects, short mode,
                     short times, struct ink *ink)
{
    short i;

    intin[0] = 0;
    vdi_op(25, 1, handle);              /* vsf_color(0) */
    intin[0] = 1;
    vdi_op(23, 1, handle);              /* vsf_interior(solid) */
    ptsin[0] = ink_cx - ink_r;
    ptsin[1] = ink_cy - ink_r;
    ptsin[2] = ink_cx + ink_r;
    ptsin[3] = ink_cy + ink_r;
    ptsin_op(114, 2, 0, handle);        /* vr_recfl() */

    intin[0] = angle;
    vdi_op(13, 1, handle);              /* vst_rotation() */
    intin[0] = effects;
    vdi_op(106, 1, handle);             /* vst_effects() */
    intin[0] = mode;
    vdi_op(32, 1, handle);              /* vswr_mode() */
    for (i = 0; i < times; i++)
    {
        ptsin[0] = ink_cx;
        ptsin[1] = ink_cy;
        intin[0] = ink_text[0];
        intin[1] = ink_text[1];
        ptsin_op(8, 1, ink_len, handle);    /* v_gtext() */
    }
    intin[0] = 1;
    vdi_op(32, 1, handle);              /* back to replace mode */
    intin[0] = 0;
    vdi_op(106, 1, handle);             /* no effects */
    intin[0] = 0;
    vdi_op(13, 1, handle);              /* no rotation */

    ink_scan(handle, ink_cx - ink_r, ink_cy - ink_r,
             ink_cx + ink_r, ink_cy + ink_r, ink);
}

/* a and b the same within a tenth (and a pixel) */
static int about(long a, long b)
{
    long d = a > b ? a - b : b - a;

    return d * 10 <= (a > b ? a : b) + 10;
}

/*
 * The first row with ink in [y0, y1] between x0 and x1 after drawing ch
 * at (x, y) with that vertical alignment, or -1: only the rows around
 * where the top of the glyph must land are read, to keep it quick
 */
static short first_ink_row(short handle, short ch, short valign, short x, short y,
                           short x1, short y0, short y1)
{
    short row, col;

    intin[0] = 0;
    vdi_op(25, 1, handle);              /* vsf_color(0) */
    intin[0] = 1;
    vdi_op(23, 1, handle);              /* vsf_interior(solid) */
    ptsin[0] = x;
    ptsin[1] = y0 - 8;
    ptsin[2] = x1;
    ptsin[3] = y1 + 64;
    ptsin_op(114, 2, 0, handle);        /* vr_recfl() */
    intin[0] = 0;
    intin[1] = valign;
    vdi_op(39, 2, handle);              /* vst_alignment(left, valign) */
    ptsin[0] = x;
    ptsin[1] = y;
    intin[0] = ch;
    ptsin_op(8, 1, 1, handle);          /* v_gtext() */
    for (row = y0; row <= y1; row++)
        for (col = x; col <= x1; col++)
        {
            ptsin[0] = col;
            ptsin[1] = row;
            ptsin_op(105, 1, 0, handle);    /* v_get_pixel() */
            if (intout[1] != 0)
                return row;
        }

    return -1;
}

/*
 * Aligned to the ascent and to the half line, the top of 'H' and of 'x'
 * land on the y given, at every size (fractional sizes in pixels too)
 */
static void check_alignment(short handle, short id)
{
    short pt, w, y = 20;

    intin[0] = id;
    vdi_op(21, 1, handle);              /* vst_font() */
    intin[0] = 1;
    vdi_op(22, 1, handle);              /* vst_color(1) */
    for (pt = 8; pt <= 30; pt++)
    {
        intin[0] = pt;
        vdi_op(107, 1, handle);         /* vst_point() */
        w = extent_of(handle, 'H', 1);
        font_check(first_ink_row(handle, 'H', 2, 8, y, 8 + w, y - 3, y + 3) == y,
                   "alignment to the ascent does not put the top of H there");
        w = extent_of(handle, 'x', 1);
        font_check(first_ink_row(handle, 'x', 1, 8, y, 8 + w, y - 3, y + 3) == y,
                   "alignment to the half line does not put the top of x there");
    }
    intin[0] = 0;
    intin[1] = 0;
    vdi_op(39, 2, handle);              /* vst_alignment(left, baseline) */
    intin[0] = 1;
    vdi_op(21, 1, handle);              /* back to the system font */
}

/*
 * Whether what the text path draws can be read back at all: the system
 * font, drawn and read as the scalable fonts are below.  Where it cannot
 * (v_get_pixel() and the drawing disagreeing on the screen's layout, as
 * on virt-arm: #598), the pixels of a scalable font say nothing either,
 * and are not checked.
 */
static int pixels_readable(short handle)
{
    struct ink ink;

    intin[0] = 1;
    vdi_op(21, 1, handle);              /* vst_font(the system font) */
    intin[0] = 1;
    vdi_op(22, 1, handle);              /* vst_color(1) */
    intin[0] = 0;
    intin[1] = 5;
    vdi_op(39, 2, handle);              /* vst_alignment(left, top) */
    ink_r = 40;
    ink_cx = ink_cy = ink_r + 4;
    ink_draw(handle, 0, 0, 1, 1, &ink);
    intin[0] = 0;
    intin[1] = 0;
    vdi_op(39, 2, handle);              /* vst_alignment(left, baseline) */

    return ink.count > 0;
}

static void check_drawing(short handle, short id)
{
    struct ink plain, turned, bold, clipped;
    short angle, w, h, split;

    intin[0] = id;
    vdi_op(21, 1, handle);              /* vst_font() */
    font_check(intout[0] == id, "vst_font() did not select a scalable font to draw");
    intin[0] = 24;
    vdi_op(107, 1, handle);             /* vst_point(24) */
    h = ptsout[3];                      /* the cell height */
    w = extent_ag(handle);
    intin[0] = 1;
    vdi_op(22, 1, handle);              /* vst_color(1) */
    intin[0] = 0;
    intin[1] = 5;
    vdi_op(39, 2, handle);              /* vst_alignment(left, top) */
    ink_r = w + h + 4;
    ink_cx = ink_cy = ink_r + 4;

    ink_draw(handle, 0, 0, 1, 1, &plain);
    font_check(plain.count > 0, "a scalable font drew nothing");
    font_check(plain.x0 >= ink_cx && plain.y0 >= ink_cy
               && plain.x1 < ink_cx + w + 2 && plain.y1 < ink_cy + h,
               "a scalable font drew outside its extent");

    /* each rotation: the same pixels, turned */
    for (angle = 900; angle <= 2700; angle += 900)
    {
        ink_draw(handle, angle, 0, 1, 1, &turned);
        font_check(about(turned.count, plain.count),
                   "a rotated scalable font has other pixels than unrotated");
        if (angle == 1800)
            font_check(turned.x1 - turned.x0 == plain.x1 - plain.x0
                       && turned.y1 - turned.y0 == plain.y1 - plain.y0,
                       "a scalable font at 1800 is not the same size");
        else
            font_check(turned.x1 - turned.x0 == plain.y1 - plain.y0
                       && turned.y1 - turned.y0 == plain.x1 - plain.x0,
                       "a scalable font at 900 or 2700 is not turned");
    }

    /* the effects apply */
    ink_draw(handle, 0, 1, 1, 1, &bold);
    font_check(bold.count > plain.count, "bold does not thicken a scalable font");
    ink_draw(handle, 0, 0x1f, 1, 1, &bold);
    font_check(bold.count > 0, "a scalable font with all effects drew nothing");

    /* XOR twice leaves nothing */
    ink_draw(handle, 0, 0, 3, 2, &turned);
    font_check(turned.count == 0, "a scalable font drawn twice in XOR mode left pixels");

    /* clipped to its left half: nothing right of the split */
    split = (plain.x0 + plain.x1) / 2;
    intin[0] = 1;
    ptsin[0] = ink_cx - ink_r;
    ptsin[1] = ink_cy - ink_r;
    ptsin[2] = split;
    ptsin[3] = ink_cy + ink_r;
    ptsin_op(129, 2, 1, handle);        /* vs_clip(on) */
    ink_draw(handle, 0, 0, 1, 1, &clipped);
    intin[0] = 0;
    ptsin_op(129, 2, 1, handle);        /* vs_clip(off) */
    font_check(clipped.count > 0 && clipped.count < plain.count
               && clipped.x1 <= split,
               "clipping does not apply to a scalable font");

    intin[0] = 0;
    intin[1] = 0;
    vdi_op(39, 2, handle);              /* vst_alignment(left, baseline) */
    intin[0] = 1;
    vdi_op(21, 1, handle);              /* back to the system font */
}

/*
 * A big size, the biggest the screen has room for: its glyphs go through
 * the text scratch buffer when rotated, outlined or skewed, and are far
 * bigger than the workstation's default buffer.  Bold, skewed and
 * outlined, turned by 90 degrees, it is the same pixels turned (skew
 * alone is not, in the bitmap text path either: #579).  Read back every
 * other pixel, to keep it quick.
 */
static short screen_w, screen_h;

static void check_large(short handle, short id)
{
    struct ink plain, turned, fx, fx_turned;
    short pt, w, h;

    intin[0] = id;
    vdi_op(21, 1, handle);              /* vst_font() */
    font_check(intout[0] == id, "vst_font() did not select a scalable font to draw big");
    for (pt = 240; ; pt -= 12)
    {
        intin[0] = pt;
        vdi_op(107, 1, handle);         /* vst_point() */
        h = ptsout[3];
        w = extent_ag(handle);
        ink_r = w + h + 4;
        if (2 * ink_r + 8 <= screen_w && 2 * ink_r + 8 <= screen_h)
            break;
        if (pt <= 36)
            return;                     /* no room on this screen */
    }
    intin[0] = 1;
    vdi_op(22, 1, handle);              /* vst_color(1) */
    intin[0] = 0;
    intin[1] = 5;
    vdi_op(39, 2, handle);              /* vst_alignment(left, top) */
    ink_cx = ink_cy = ink_r + 4;
    ink_step = 2;

    ink_draw(handle, 0, 0, 1, 1, &plain);
    ink_draw(handle, 900, 0, 1, 1, &turned);
    font_check(plain.count > 0 && about(turned.count, plain.count),
               "a big scalable font rotated has other pixels than unrotated");
    ink_draw(handle, 0, 0x15, 1, 1, &fx);  /* bold, skewed, outlined */
    ink_draw(handle, 900, 0x15, 1, 1, &fx_turned);
    font_check(fx.count > 0 && about(fx_turned.count, fx.count),
               "a big scalable font with effects, rotated, has other pixels");

    ink_step = 1;
    intin[0] = 0;
    intin[1] = 0;
    vdi_op(39, 2, handle);              /* vst_alignment(left, baseline) */
    intin[0] = 1;
    vdi_op(21, 1, handle);              /* back to the system font */
}

/*
 * vst_height() gives exactly the height asked for, whatever the font's
 * metrics, and vst_point() exactly the size in points
 */
static void check_heights(short handle, short id)
{
    short top, pt;

    intin[0] = id;
    vdi_op(21, 1, handle);              /* vst_font() */
    font_check(intout[0] == id, "vst_font() did not select a scalable font to size");
    for (top = 9; top <= 40; top++)
    {
        ptsin[0] = 0;
        ptsin[1] = top;
        ptsin_op(12, 1, 0, handle);     /* vst_height() */
        font_check(ptsout[1] == top, "vst_height() did not give the height asked for");
    }
    for (pt = 6; pt <= 36; pt++)
    {
        intin[0] = pt;
        vdi_op(107, 1, handle);         /* vst_point() */
        font_check(intout[0] == pt, "vst_point() did not give the size asked for");
    }
    intin[0] = 1;
    vdi_op(21, 1, handle);              /* back to the system font */
}

/* a monospaced font stays so in bold, as the system fonts do */
static void check_monospace(short handle, short id)
{
    short narrow, wide, bold;

    intin[0] = id;
    vdi_op(21, 1, handle);              /* vst_font() */
    font_check(intout[0] == id, "vst_font() did not select a scalable font");
    intin[0] = 24;
    vdi_op(107, 1, handle);             /* vst_point(24) */
    narrow = extent_of(handle, 'i', 4);
    wide = extent_of(handle, 'W', 4);
    if (narrow == wide)
    {
        intin[0] = 1;
        vdi_op(106, 1, handle);         /* vst_effects(bold) */
        bold = extent_of(handle, 'W', 4);
        intin[0] = 0;
        vdi_op(106, 1, handle);
        font_check(bold == wide, "bold widens a monospaced scalable font");
        monospaced_fonts++;
    }
    intin[0] = 1;
    vdi_op(21, 1, handle);              /* back to the system font */
}

/* NVDI's XFNT_INFO, as an application has it */
struct xfnt_info
{
    long size;
    short format, id, index;
    char font_name[50], family_name[50], style_name[50];
    char file_name1[200], file_name2[200], file_name3[200];
    short pt_cnt;
    short pt_sizes[64];
};

static struct xfnt_info xfnt;
static char fhdr[1024];                 /* vqt_fontheader()'s buffer, as gemlib advises */

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

/* whether intout[0..n-1], one character each, ends in .TTF or .OTF */
static int font_file(short n)
{
    return n > 4 && intout[n - 4] == '.' && intout[n - 1] == 'F'
        && (intout[n - 3] == 'T' || intout[n - 3] == 'O') && intout[n - 2] == 'T';
}

static short current_font(short handle)
{
    vdi_op(38, 0, handle);              /* vqt_attributes() */
    return intout[0];
}

/* SpeedoGDOS's and NVDI's font calls (#577) */
static void check_speedo(short handle, short system_fonts)
{
    char name[33], joined[110];
    short i, id, n, error;
    unsigned short upem;
    void *p;
    long before, after;
    const short *table;

    /* vqt_ext_name(): the system font is a bitmap font, the first scalable one not */
    intin[0] = 1;
    intin[1] = 0;
    contrl[0] = 130;
    contrl[1] = 0;
    contrl[3] = 2;
    contrl[5] = 1;
    contrl[6] = handle;
    vdi();
    font_check(contrl[4] == 35 && (intout[34] & 0xff) == 1,
               "vqt_ext_name() does not give the system font as a bitmap font");
    intin[0] = system_fonts + 1;
    intin[1] = 0;
    contrl[3] = 2;
    contrl[5] = 1;
    vdi();
    font_check(contrl[4] == 35 && ((intout[34] & 0xff) == 4 || (intout[34] & 0xff) == 8),
               "vqt_ext_name() does not give a scalable font as TrueType or Type 1");
    id = intout[0];
    for (i = 0; i < 32 && intout[i + 1]; i++)
        name[i] = (char)intout[i + 1];
    name[i] = '\0';

    /* vqt_xfntinfo(): by its place in the list */
    xfnt.size = sizeof(xfnt);
    xfnt.font_name[0] = xfnt.family_name[0] = xfnt.style_name[0] = '\0';
    intin[0] = 0x010f;                  /* the names, the first file, the sizes */
    intin[1] = 0;
    intin[2] = system_fonts + 1;
    p = &xfnt;
    memcpy(&intin[3], &p, sizeof(p));
    vdi_op(229, 3 + sizeof(p) / 2, handle);
    font_check(intout[1] == id && intout[2] == system_fonts + 1 && xfnt.id == id,
               "vqt_xfntinfo() did not find the font by its place");
    font_check(str_eq(xfnt.font_name, name), "vqt_xfntinfo() gives another name");
    for (i = 0; xfnt.family_name[i]; i++)
        joined[i] = xfnt.family_name[i];
    if (!str_eq(xfnt.style_name, "Regular"))
    {
        joined[i++] = ' ';
        for (n = 0; xfnt.style_name[n]; n++)
            joined[i++] = xfnt.style_name[n];
    }
    joined[i] = '\0';
    font_check(str_eq(joined, name), "vqt_xfntinfo()'s family and style do not make the name");
    font_check(xfnt.file_name1[0] && xfnt.file_name1[1] == ':',
               "vqt_xfntinfo() has no path to the font file");
    font_check(xfnt.pt_cnt > 0, "vqt_xfntinfo() offers no sizes");

    /* by its id, the system font */
    xfnt.pt_cnt = 0;
    intin[0] = 0x0100;
    intin[1] = 1;
    intin[2] = 0;
    vdi_op(229, 3 + sizeof(p) / 2, handle);
    font_check(intout[0] == 1 && intout[2] == 1 && xfnt.pt_cnt > 0,
               "vqt_xfntinfo() did not find the system font by its id");

    /* vqt_name_and_id() looks it up, vst_name() selects it */
    intin[0] = 0;
    for (n = 0; name[n]; n++)
        intin[n + 1] = name[n];
    intin[n + 1] = 0;
    contrl[0] = 230;
    contrl[1] = 0;
    contrl[3] = n + 2;
    contrl[5] = 100;
    contrl[6] = handle;
    vdi();
    font_check(intout[0] == id, "vqt_name_and_id() did not find the font");
    font_check(current_font(handle) == 1, "vqt_name_and_id() selected the font");
    contrl[0] = 230;
    contrl[3] = n + 2;
    contrl[5] = 0;
    vdi();
    font_check(intout[0] == id && contrl[4] == n + 1, "vst_name() did not answer with the font");
    font_check(current_font(handle) == id, "vst_name() did not select the font");

    /* vqt_fontheader(): its name, its design units, its file */
    for (i = 0; i < (short)sizeof(fhdr); i++)
        fhdr[i] = 0x55;
    p = fhdr;
    memcpy(&intin[0], &p, sizeof(p));
    vdi_op(232, sizeof(p) / 2, handle);
    n = contrl[4];
    font_check(fhdr[0] == 'D' && str_eq(fhdr + 24, name),
               "vqt_fontheader() has another name");
    memcpy(&upem, fhdr + 330, sizeof(upem));
    font_check(upem >= 16, "vqt_fontheader() has no design units per em");
    font_check(font_file(n), "vqt_fontheader() did not give the font's file");
    font_check(fhdr[332] == 0x55, "vqt_fontheader() wrote past its fields");
    intin[0] = 1;
    vdi_op(21, 1, handle);              /* back to the system font */

    /* the cache: a flush leaves at least as much free */
    vdi_op(255, 1, handle);             /* vqt_cachesize() */
    memcpy(&before, &intout[0], sizeof(before));
    vdi_op(251, 0, handle);             /* v_flushcache() */
    vdi_op(255, 1, handle);
    memcpy(&after, &intout[0], sizeof(after));
    font_check(before >= 0 && after >= before, "v_flushcache() freed nothing");

    /* the table to BICS, which agrees with ASCII */
    intout[0] = intout[1] = 0;
    vdi_op(254, 0, handle);             /* vqt_get_table() */
    memcpy(&table, &intout[0], sizeof(table));
    font_check(table && table['A' - 32] == 'A' - 32 && table[0x80 - 32] == -1,
               "vqt_get_table() does not map ASCII to BICS");

    /* vst_error() clears the variable it is given */
    error = 99;
    intin[0] = 0;
    p = &error;
    memcpy(&intin[1], &p, sizeof(p));
    vdi_op(245, 1 + sizeof(p) / 2, handle);
    font_check(error == 0, "vst_error() did not clear the error variable");
    intin[0] = 0;
    p = NULL;
    memcpy(&intin[1], &p, sizeof(p));
    vdi_op(245, 1 + sizeof(p) / 2, handle);
}

/* vst_map_mode(), which answers with the mode set */
static short map_mode(short handle, short mode)
{
    intin[0] = mode;
    intin[1] = 1;
    vdi_op(236, 2, handle);
    return intout[0];
}

/* the width of the characters in intin[0..n-1] */
static short extent_n(short handle, short n)
{
    ptsin_op(116, 0, n, handle);        /* vqt_extent() */
    return ptsout[2] - ptsout[0];
}

static short width_of(short handle, short ch)
{
    intin[0] = ch;
    ptsin_op(117, 0, 1, handle);        /* vqt_width() */
    return intout[0] == -1 ? -1 : ptsout[0];
}

static unsigned short char_index(short handle, short c, short src, short dst)
{
    intin[0] = c;
    intin[1] = src;
    intin[2] = dst;
    vdi_op(190, 3, handle);
    return (unsigned short)intout[0];
}

/* text in the character maps, in a scalable font or the system font */
static void check_maps(short handle, int scalable)
{
    short w_atari, w_uni, ag;

    font_check(map_mode(handle, 2) == 2, "vst_map_mode(2) did not set Unicode");
    ag = extent_ag(handle);
    map_mode(handle, 1);
    font_check(extent_ag(handle) == ag, "ASCII is not as wide in Unicode as in the Atari set");
    w_atari = width_of(handle, 0x82);   /* e acute */
    map_mode(handle, 2);
    w_uni = width_of(handle, 0xe9);
    font_check(w_uni == w_atari && w_uni > 0, "e acute is not as wide in Unicode as in the Atari set");
    /* a string of several runs is as wide as its characters */
    {
        short wa = width_of(handle, 'a'), walpha = width_of(handle, 0x3b1);

        intin[0] = 0xe9;
        intin[1] = 'a';
        intin[2] = 0x3b1;               /* alpha: another page */
        intin[3] = 0xe9;
        font_check(walpha > 0 && extent_n(handle, 4) == 2 * w_uni + wa + walpha,
                   "a string of several pages is not as wide as its characters");
    }
    if (!scalable)
    {
        /* the system font has no CJK: a '?' */
        w_atari = width_of(handle, '?');
        font_check(width_of(handle, 0x4e00) == w_atari, "the system font does not show U+4E00 as '?'");
    }

    /* UTF-16: a pair is one character, here one without a glyph */
    intin[0] = 0xdbff;
    intin[1] = 0xdffd;
    w_uni = extent_n(handle, 2);        /* in UCS-2: two characters */
    font_check(map_mode(handle, 3) == 3, "vst_map_mode(3) did not set UTF-16");
    intin[0] = 0xdbff;
    intin[1] = 0xdffd;
    font_check(extent_n(handle, 2) * 2 == w_uni && w_uni > 0,
               "a surrogate pair is not one character in UTF-16");

    /* BICS: its ASCII part */
    map_mode(handle, 0);
    intin[0] = 'A' - 32;
    intin[1] = 'g' - 32;
    font_check(extent_n(handle, 2) == ag, "BICS is not ASCII where they agree");

    /* drawn through runs, in each map (a page of several runs) */
    map_mode(handle, 2);
    intin[0] = 'a';
    intin[1] = 0xe9;
    intin[2] = 0x3b1;                   /* alpha */
    intin[3] = 'z';
    ptsin[0] = 20;
    ptsin[1] = 60;
    ptsin_op(8, 1, 4, handle);          /* v_gtext() */
    map_mode(handle, 1);
}

/* vst_kern() and vst_track_offset() */
static void kern_mode(short handle, short tmode, short pmode, long offset)
{
    intin[0] = tmode;
    intin[1] = pmode;
    memcpy(&intin[2], &offset, sizeof(offset));
    vdi_op(237, tmode == 255 ? 4 : 2, handle);
}

static int fonts_kerned;                /* fonts whose pairs were seen to apply */

/*
 * Kerning: where the font has pairs, "AV" kerned is as wide as it is
 * plain plus vqt_pairkern()'s amount; a track offset of half a pixel
 * makes ten characters five pixels wider (nine after the first, rounded)
 */
static void check_kerning(short handle)
{
    short plain, kerned, pairs, i;
    long x, y;

    intin[0] = 36;
    vdi_op(107, 1, handle);             /* vst_point(36): kerning of whole pixels */
    intin[0] = 'A';
    intin[1] = 'V';
    plain = extent_n(handle, 2);
    kern_mode(handle, 0, 1, 0);
    pairs = intout[1];
    intin[0] = 'A';
    intin[1] = 'V';
    vdi_op(235, 2, handle);             /* vqt_pairkern() */
    memcpy(&x, &intout[0], sizeof(x));
    memcpy(&y, &intout[2], sizeof(y));
    intin[0] = 'A';
    intin[1] = 'V';
    kerned = extent_n(handle, 2);
    font_check(y == 0, "vqt_pairkern() kerns up or down");
    font_check(pairs || x == 0, "vqt_pairkern() kerns in a font without pairs");
    font_check(kerned == plain + (short)((x + 0x8000L) >> 16)
               || kerned == plain + (short)((x + 0x7fffL) >> 16),
               "kerning does not make \"AV\" as wide as vqt_pairkern() says");
    if (pairs && x <= -0x10000L)
        fonts_kerned++;
    kern_mode(handle, 0, 0, 0);

    for (i = 0; i < 10; i++)
        intin[i] = 'n';
    plain = extent_n(handle, 10);
    kern_mode(handle, 255, 0, 0x8000L);
    vdi_op(234, 0, handle);             /* vqt_trackkern() */
    memcpy(&x, &intout[0], sizeof(x));
    font_check(x == 0x8000L, "vqt_trackkern() does not give the track offset");
    for (i = 0; i < 10; i++)
        intin[i] = 'n';
    kerned = extent_n(handle, 10);
    font_check(kerned >= plain + 4 && kerned <= plain + 5,
               "a track offset of half a pixel does not add up");
    kern_mode(handle, 0, 0, 0);
    intin[0] = 12;
    vdi_op(107, 1, handle);
}

/* vst_arbpt() and the other size calls, and the metrics */
static void check_metrics(short handle, short id)
{
    short top12, top13, w, h, i;
    long fix;

    intin[0] = id;
    vdi_op(21, 1, handle);              /* vst_font() */
    intin[0] = 12;
    vdi_op(107, 1, handle);             /* vst_point(12) */
    top12 = ptsout[1];
    intin[0] = 13;
    ptsin_op(246, 0, 1, handle);        /* vst_arbpt(13) */
    top13 = ptsout[1];
    font_check(intout[0] == 13 && contrl[4] == 1, "vst_arbpt() did not give 13 points");

    /* 12.5 points, in 16.16 */
    fix = 0xc8000L;
    memcpy(&intin[0], &fix, sizeof(fix));
    ptsin_op(246, 0, 2, handle);        /* vst_arbpt32() */
    memcpy(&fix, &intout[0], sizeof(fix));
    font_check(contrl[4] == 2 && fix > 0xc8000L - 0x800 && fix < 0xc8000L + 0x800,
               "vst_arbpt32() did not give 12.5 points");
    font_check(ptsout[1] >= top12 && ptsout[1] <= top13, "12.5 points is not between 12 and 13");

    /* twice as wide, the height staying */
    intin[0] = 12;
    vdi_op(107, 1, handle);
    w = extent_of(handle, 'M', 4);
    h = ptsout[5] - ptsout[1];
    intin[0] = 24;
    ptsin_op(252, 0, 1, handle);        /* vst_setsize(24) */
    font_check(intout[0] == 24, "vst_setsize() did not answer 24 points");
    font_check(extent_of(handle, 'M', 4) > w * 3 / 2 && ptsout[5] - ptsout[1] == h,
               "vst_setsize() did not widen the font alone");
    intin[0] = 12;
    vdi_op(107, 1, handle);             /* the width follows the height again */
    font_check(extent_of(handle, 'M', 4) == w, "vst_point() did not reset the width");
    ptsin[0] = 6;
    ptsin_op(231, 1, 0, handle);        /* vst_width(6) */
    font_check(extent_of(handle, 'M', 4) < w, "vst_width() did not narrow the font");
    intin[0] = 12;
    vdi_op(107, 1, handle);

    /* slanted, the advances staying */
    intin[0] = 150;
    vdi_op(253, 1, handle);             /* vst_skew(15 degrees) */
    font_check(intout[0] == 150, "vst_skew() did not answer the slant");
    font_check(extent_of(handle, 'M', 4) == w, "vst_skew() changed the advances");
    intin[0] = 0;
    vdi_op(253, 1, handle);

    /* vqt_advance() is vqt_width()'s, the 16.16 one after it */
    w = width_of(handle, 'W');
    intin[0] = 'W';
    ptsin_op(247, 0, 1, handle);
    memcpy(&fix, &ptsout[4], sizeof(fix));
    font_check(ptsout[0] == w && ptsout[1] == 0 && fix == (long)w << 16,
               "vqt_advance() is not the character's width");

    /* vqt_f_extent() is vqt_extent(); vqt_real_extent() is at the point */
    w = extent_ag(handle);
    intin[0] = 'A';
    intin[1] = 'g';
    ptsin_op(240, 0, 2, handle);
    font_check(ptsout[2] - ptsout[0] == w, "vqt_f_extent() is not vqt_extent()");
    intin[0] = 'A';
    intin[1] = 'g';
    ptsin[0] = 100;
    ptsin[1] = 100;
    contrl[0] = 240;
    contrl[1] = 1;
    contrl[3] = 2;
    contrl[5] = 4200;
    contrl[6] = handle;
    vdi();                              /* vqt_real_extent() */
    font_check(ptsout[0] == 100 && ptsout[2] == 100 + w && ptsout[1] > 100 && ptsout[5] < 100,
               "vqt_real_extent() is not the box at the point");

    /* v_ftext(), and v_ftext_offset() with an offset per character */
    intin[0] = 'A';
    intin[1] = 'g';
    ptsin[0] = 20;
    ptsin[1] = 40;
    ptsin_op(241, 1, 2, handle);
    for (i = 0; i < 2; i++)
    {
        ptsin[2 + 2 * i] = i * 30;
        ptsin[3 + 2 * i] = 0;
    }
    ptsin_op(241, 3, 2, handle);

    /* vqt_char_index(): through Unicode, and the font's glyphs */
    font_check(char_index(handle, 0xe9, 2, 1) == 0x82 && char_index(handle, 0x82, 1, 2) == 0xe9,
               "vqt_char_index() does not map e acute between Unicode and Atari");
    font_check(char_index(handle, 10, 1, 1) == 0xffff, "vqt_char_index() maps a control code");
    font_check(char_index(handle, char_index(handle, 'A', 2, 0), 0, 2) == 'A',
               "vqt_char_index() does not find 'A' through its glyph");

    check_maps(handle, 1);
    check_kerning(handle);
    intin[0] = 1;
    vdi_op(21, 1, handle);              /* back to the system font */
    check_maps(handle, 0);
}

/*
 * Text in runs (#577) draws as the same text in one: "a" and e acute
 * in Unicode (e acute in the size of page 0) as in the Atari set, also
 * centred and rotated, where the runs must follow each other and the
 * alignment take them all
 */
static void check_run_pixels(short handle, short id)
{
    struct ink atari, uni;
    short angle, align;

    intin[0] = id;
    vdi_op(21, 1, handle);              /* vst_font() */
    intin[0] = 18;
    vdi_op(107, 1, handle);             /* vst_point(18) */
    ink_r = 40;
    ink_cx = ink_cy = ink_r + 4;
    for (align = 0; align <= 1; align++)
    {
        intin[0] = align;
        intin[1] = 0;
        vdi_op(39, 2, handle);          /* vst_alignment() */
        for (angle = 0; angle <= 900; angle += 900)
        {
            ink_text[0] = 'a';
            ink_text[1] = 0x82;
            ink_draw(handle, angle, 0, 1, 1, &atari);
            map_mode(handle, 2);
            ink_text[1] = 0xe9;
            ink_draw(handle, angle, 0, 1, 1, &uni);
            map_mode(handle, 1);
            font_check(atari.count > 0 && uni.count == atari.count
                       && uni.x0 == atari.x0 && uni.x1 == atari.x1
                       && uni.y0 == atari.y0 && uni.y1 == atari.y1,
                       "text in two runs is not drawn as in one");
        }
    }
    intin[0] = 0;
    intin[1] = 0;
    vdi_op(39, 2, handle);

    /* kerned, the V is drawn that much further left, the A where it was */
    ink_text[0] = 'A';
    ink_text[1] = 'V';
    ink_draw(handle, 0, 0, 1, 1, &atari);
    kern_mode(handle, 0, 1, 0);
    if (intout[1])
    {
        long x;

        intin[0] = 'A';
        intin[1] = 'V';
        vdi_op(235, 2, handle);         /* vqt_pairkern() */
        memcpy(&x, &intout[0], sizeof(x));
        ink_draw(handle, 0, 0, 1, 1, &uni);
        font_check(uni.x0 == atari.x0 && uni.x1 - atari.x1 == (short)((x + 0x8000L) >> 16),
                   "kerned text is not drawn where its width says");
    }
    kern_mode(handle, 0, 0, 0);

    ink_text[0] = 'A';
    ink_text[1] = 'g';
    intin[0] = 1;
    vdi_op(21, 1, handle);              /* back to the system font */
}

static void check_fonts(void)
{
    short handle, system_fonts, i, aes_there, drive, id;

    /*
     * Under the AES (the suite started from the desktop) the screen is
     * the AES's: open a virtual workstation on it.  Opening and closing
     * the physical one would take it from the desktop.  Without the AES
     * (EmuCON), the physical workstation is free.
     */
    aes_global[0] = 0;
    aes(10, 0, 1);                      /* appl_init() */
    aes_there = aes_global[0] != 0;

    for (i = 0; i < 10; i++)
        intin[i] = 1;
    intin[10] = 2;                      /* raster coordinates */
    if (aes_there)
    {
        short phys;

        aes(77, 0, 5);                  /* graf_handle() */
        phys = intout[0];
        for (i = 0; i < 10; i++)
            intin[i] = 1;
        intin[10] = 2;
        vdi_op(100, 11, phys);          /* v_opnvwk() */
    }
    else
        vdi_op(1, 11, 0);               /* v_opnwk(), the screen */
    handle = contrl[6];
    font_check(handle > 0, "opening a workstation failed");
    if (handle <= 0)
    {
        if (aes_there)
            aes(19, 0, 1);              /* appl_exit() */
        return;
    }
    system_fonts = intout[10];
    screen_w = intout[0] + 1;
    screen_h = intout[1] + 1;

    intin[0] = 0;
    vdi_op(119, 1, handle);             /* vst_load_fonts() */
    fonts_loaded = intout[0];
    font_check(fonts_loaded >= 0, "vst_load_fonts() returned less than 0");

    for (i = 1; i <= system_fonts + fonts_loaded; i++)
    {
        intin[0] = i;
        intout[33] = -1;
        vdi_op(130, 1, handle);         /* vqt_name() */
        font_check(contrl[4] >= 34, "vqt_name() has no outline flag");
        if (i <= system_fonts)
            font_check(intout[33] == 0, "a system font flagged as outline");
        else
        {
            font_check(intout[33] == 1, "a scalable font not flagged as outline");
            font_check(intout[0] >= 10000, "a scalable font's id is below 10000");
        }
    }

    /*
     * The font files are opened again as fonts and sizes are selected,
     * on behalf of this program: from another current drive than the
     * one they were found on, they must still be found.
     */
    drive = Dgetdrv();
    Dsetdrv(drive ? 0 : 1);
    if (fonts_loaded > 0)
        check_sizes(handle, system_fonts);
    if (fonts_loaded > 0)
    {
        check_speedo(handle, system_fonts);
        intin[0] = system_fonts + 1;
        vdi_op(130, 1, handle);         /* vqt_name() */
        check_metrics(handle, intout[0]);
    }
    for (i = 1; i <= fonts_loaded; i++)
    {
        intin[0] = system_fonts + i;
        vdi_op(130, 1, handle);         /* vqt_name() */
        id = intout[0];
        check_monospace(handle, id);
        check_heights(handle, id);
    }
    pixels_checked = fonts_loaded > 0 && pixels_readable(handle);
    for (i = 1; pixels_checked && i <= fonts_loaded; i++)
    {
        intin[0] = system_fonts + i;
        vdi_op(130, 1, handle);         /* vqt_name() */
        id = intout[0];
        check_drawing(handle, id);
        check_alignment(handle, id);
        if (i == 1)
        {
            check_large(handle, id);
            check_run_pixels(handle, id);
        }
    }

    Dsetdrv(drive);

    intin[0] = 0;
    vdi_op(119, 1, handle);             /* again: nothing more */
    font_check(intout[0] == 0, "a second vst_load_fonts() added fonts");

    vdi_op(120, 1, handle);             /* vst_unload_fonts() */
    if (aes_there)
    {
        vdi_op(101, 0, handle);         /* v_clsvwk() */
        aes(19, 0, 1);                  /* appl_exit() */
    }
    else
        vdi_op(2, 0, handle);           /* v_clswk() */
}

/* vq_gdos(): what trap #2 leaves in d0/r0 when called with -2 */
static long vq_vgdos(void)
{
#ifdef __mc68000__
    register long d0 __asm__("d0") = -2;

    __asm__ volatile (
        "trap #2"
        : "+d" (d0)
        :
        : "d1", "d2", "a0", "a1", "memory", "cc");

    return d0;
#else
    register long r0 __asm__("r0") = -2;

    __asm__ volatile (
        "svc 2"
        : "+r" (r0)
        :
        : "r1", "r2", "r3", "r12", "lr", "memory", "cc");

    return r0;
#endif
}

void test_gdos_answer(void)
{
    unsigned long value = 0;
    const struct fsmc *fsmc = NULL;
    long gdos;
    int builtin;

    gdos = vq_vgdos();
    if (Ssystem(-1, 0, 0L) != EINVFN)
    {
        if (Ssystem(SS_GETCOOKIE, TAG('F', 'S', 'M', 'C'), (long)&value) == 0)
            fsmc = (const struct fsmc *)value;
    }
    else
    {
        /* no Ssystem(): the jar itself, through _p_cookies */
        const struct cookie *jar;

        jar = (const struct cookie *)Setexc(0x5a0 / 4, (void (*)(void))-1L);
        for (; jar && jar->tag; jar++)
            if (jar->tag == TAG('F', 'S', 'M', 'C'))
                fsmc = (const struct fsmc *)jar->value;
    }

    /*
     * The built-in GDOS: '_FSM', and its own FSMC cookie or (a fault the
     * suite reports) none.  Another FSMC cookie is another GDOS's.
     */
    builtin = gdos == TAG('_', 'F', 'S', 'M')
              && (!fsmc || (fsmc->magic == (unsigned long)TAG('_', 'S', 'P', 'D')
                            && fsmc->version == 0x0400));
    if (builtin)
    {
        check_fonts();
        conws("\n");   /* past what the workstation left on the console line */
    }

    ptest_begin("gdos_answer (#582)");

    if (gdos == -2)
    {
        ptest_assert_msg(!fsmc, "an FSMC cookie, but vq_gdos() says no GDOS");
        if (!fsmc)
            conws("(no GDOS) ");
    }
    else if (!builtin)
    {
        /* a GDOS loaded from disk: the built-in one stayed out of its way */
        conws("(a GDOS from disk) ");
    }
    else
    {
        ptest_assert_msg(fsmc != NULL, "the built-in GDOS, but no FSMC cookie");
        ptest_assert_msg(!font_failure, font_failure);
        conws("(SpeedoGDOS, ");
        pnum(fonts_loaded);
        conws(" scalable fonts, ");
        pnum(monospaced_fonts);
        conws(" monospaced, ");
        pnum(fonts_kerned);
        conws(" kerned");
        if (fonts_loaded > 0 && !pixels_checked)
            conws(", pixels not readable here");
        conws(") ");
    }

    ptest_pass();
}

#endif
