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
 * (tools/run-regression-tests.sh copies DejaVu, TrueType, and Inter,
 * OpenType with CFF outlines, there when the host has them).  Each is
 * selected, sized and drawn (#576), and what it drew is read back to see
 * that rotation, clipping, the effects and XOR mode apply to it -- where
 * the system font's text can be read back with v_get_pixel(); where it
 * cannot, the result line says so.
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

static short contrl[16], intin[16], ptsin[16], intout[64], ptsout[16];
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

    /* exact heights, unlike a bitmap font's nearest one */
    ptsin[0] = 0;
    ptsin[1] = 37;
    ptsin_op(12, 1, 0, handle);         /* vst_height(37) */
    font_check(ptsout[1] >= 35 && ptsout[1] <= 39, "vst_height(37) is not 37 high");

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

static void ink_scan(short handle, short x0, short y0, short x1, short y1,
                     struct ink *ink)
{
    short x, y;

    ink->count = 0;
    ink->x0 = ink->y0 = 32767;
    ink->x1 = ink->y1 = -1;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
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

/* clear the area around the origin, draw "Ag" there and see what came out */
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
        intin[0] = 'A';
        intin[1] = 'g';
        ptsin_op(8, 1, 2, handle);      /* v_gtext() */
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

static void check_fonts(void)
{
    short handle, system_fonts, i, aes_there;

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

    if (fonts_loaded > 0)
        check_sizes(handle, system_fonts);
    pixels_checked = fonts_loaded > 0 && pixels_readable(handle);
    for (i = 1; pixels_checked && i <= fonts_loaded; i++)
    {
        intin[0] = system_fonts + i;
        vdi_op(130, 1, handle);         /* vqt_name() */
        check_drawing(handle, intout[0]);
    }

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
        conws(" scalable fonts");
        if (fonts_loaded > 0 && !pixels_checked)
            conws(", pixels not readable here");
        conws(") ");
    }

    ptest_pass();
}

#endif
