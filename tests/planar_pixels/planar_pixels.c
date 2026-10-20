/*
 * planar_pixels.c - regression test for what the VDI draws on a bitplane screen (#598)
 *
 * v_get_pixel() has to return what the drawing calls put on the screen, at
 * the same coordinates: the fills, a line, and the text of the system font
 * with its effects and writing modes.  On the ARM kernels' 4-plane screen
 * (virt-arm) the text blit drew nothing at all, as only its one-plane case
 * (the text buffer) was written, and the other readers and writers of the
 * screen did not agree on the byte order of its words.
 *
 * The test draws in the upper left corner of the screen on a workstation of
 * its own; it is skipped where there is no VDI for programs, and where the
 * screen has more than 16 colours.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <string.h>
#include "include/vdi_speedo.h"

void test_planar_pixels(void);

#if !defined(__mc68000__) && !defined(__arm__)

void test_planar_pixels(void)
{
    ptest_begin("planar_pixels (#598)");
    conws("SKIP (no VDI for programs here)\r\n");
}

#else

#define AREA_W  60                     /* the part of the screen drawn on */
#define AREA_H  26

/* the AES, for the case of a desktop running the suite */
static short aes_control[5], aes_global[15];
static long aes_addr[2];
static void *aespb[6] = { aes_control, aes_global, vs_intin, vs_intout, aes_addr, aes_addr };

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

static short handle;
static short aes_there;
static short wk_planes;

static short get_pixel(short x, short y)
{
    vs_ptsin[0] = x;
    vs_ptsin[1] = y;
    vs_call(handle, 105, 0, 1, 0);

    return vs_intout[1];                /* the colour index */
}

static void recfl(short x0, short y0, short x1, short y1, short colour)
{
    vs_intin[0] = 1;
    vs_call(handle, 23, 0, 0, 1);       /* vsf_interior(solid) */
    vs_intin[0] = colour;
    vs_call(handle, 25, 0, 0, 1);       /* vsf_color() */
    vs_ptsin[0] = x0;
    vs_ptsin[1] = y0;
    vs_ptsin[2] = x1;
    vs_ptsin[3] = y1;
    vs_call(handle, 114, 0, 2, 0);      /* vr_recfl() */
}

static void clear_area(void)
{
    recfl(0, 0, AREA_W, AREA_H, 0);
}

/* how many pixels of the area are not colour 0 */
static long count_ink(void)
{
    short x, y;
    long n = 0;

    for (y = 0; y <= AREA_H; y++)
        for (x = 0; x <= AREA_W; x++)
            if (get_pixel(x, y) != 0)
                n++;

    return n;
}

/* how many pixels of a part of the area are not colour 0 */
static long ink_in(short x0, short y0, short x1, short y1)
{
    short x, y;
    long n = 0;

    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            if (get_pixel(x, y) != 0)
                n++;

    return n;
}

static const char *failure;

static void check(int ok, const char *msg)
{
    if (!ok && !failure)
        failure = msg;
}

static void check_line(void)
{
    short x, hits = 0;

    clear_area();
    vs_intin[0] = 1;
    vs_call(handle, 15, 0, 0, 1);       /* vsl_type(solid) */
    vs_intin[0] = 1;
    vs_call(handle, 17, 0, 0, 1);       /* vsl_color(1) */
    vs_ptsin[0] = 0;
    vs_ptsin[1] = 0;
    vs_ptsin[2] = 50;
    vs_ptsin[3] = 20;
    vs_call(handle, 6, 0, 2, 0);        /* v_pline() */

    /* the line is where it was drawn: on its ends, and in step on the way */
    check(get_pixel(0, 0) == 1, "v_get_pixel() does not see the start of a line");
    check(get_pixel(50, 20) == 1, "v_get_pixel() does not see the end of a line");
    for (x = 0; x <= 50; x++)
        if (get_pixel(x, (short)(x * 20L / 50)) == 1
            || get_pixel(x, (short)(x * 20L / 50 + 1)) == 1
            || (x && get_pixel(x, (short)(x * 20L / 50 - 1)) == 1))
            hits++;
    check(hits >= 49, "v_get_pixel() does not follow a line");
    check(get_pixel(25, 0) == 0 && get_pixel(10, 19) == 0,
          "v_get_pixel() sees ink away from a line");
}

static void check_fill(void)
{
    short c;

    for (c = 1; c < (1 << wk_planes) && c < 16; c++)
    {
        clear_area();
        recfl(5, 5, 14, 14, c);
        check(get_pixel(5, 5) == c && get_pixel(14, 14) == c && get_pixel(9, 9) == c,
              "v_get_pixel() does not read the colour of a filled rectangle");
        check(get_pixel(4, 4) == 0 && get_pixel(15, 15) == 0 && get_pixel(4, 9) == 0
              && get_pixel(15, 9) == 0, "a filled rectangle spills over its edge");
    }
}

/* rectangles of any alignment: filled, then cleared inside a fill of ink */
static void check_edges(void)
{
    short x0, w;

    for (x0 = 0; x0 < 33; x0++)
        for (w = 0; w < 20; w += 7)
        {
            recfl(0, 0, 80, 4, 1);
            recfl(x0, 1, x0 + w, 3, 0);
            check(get_pixel(x0, 2) == 0 && get_pixel(x0 + w, 2) == 0
                  && get_pixel(x0 + w / 2, 1) == 0 && get_pixel(x0 + w / 2, 3) == 0,
                  "a cleared rectangle keeps some of its ink");
            check((x0 == 0 || get_pixel(x0 - 1, 2) == 1) && get_pixel(x0 + w + 1, 2) == 1
                  && get_pixel(x0 + w / 2, 0) == 1 && get_pixel(x0 + w / 2, 4) == 1,
                  "a cleared rectangle takes more than its area");
        }
}

static void check_text(void)
{
    long ink;

    clear_area();
    clear_area();
    vs_intin[0] = 1;
    vs_call(handle, 22, 0, 0, 1);       /* vst_color(1) */
    vs_intin[0] = 0;
    vs_intin[1] = 5;
    vs_call(handle, 39, 0, 0, 2);       /* vst_alignment(left, top) */
    vs_ptsin[0] = 4;
    vs_ptsin[1] = 4;
    vs_intin[0] = 'H';
    vs_intin[1] = 'i';
    vs_call(handle, 8, 0, 1, 2);        /* v_gtext() */

    ink = count_ink();
    check(ink > 10, "v_get_pixel() sees no text");
    /* the ink is within the two characters' cells from (4, 4), not left of it or above it */
    check(ink_in(4, 4, 4 + 2 * 8 + 1, 4 + 16) == ink, "text is drawn outside its cell");
}

static long draw_hi(short effects, short mode)
{
    clear_area();
    vs_intin[0] = effects;
    vs_call(handle, 106, 0, 0, 1);      /* vst_effects() */
    vs_intin[0] = mode;
    vs_call(handle, 32, 0, 0, 1);       /* vswr_mode() */
    vs_ptsin[0] = 4;
    vs_ptsin[1] = 4;
    vs_intin[0] = 'H';
    vs_intin[1] = 'i';
    vs_call(handle, 8, 0, 1, 2);        /* v_gtext() */
    vs_intin[0] = 0;
    vs_call(handle, 106, 0, 0, 1);
    vs_intin[0] = 1;
    vs_call(handle, 32, 0, 0, 1);

    return count_ink();
}

/* whether the leftmost ink of the first row with any is not that of the last */
static int skew_leans(void)
{
    short x, y, first = -1, last = -1, left_first = 0, left_last = 0;

    for (y = 0; y < 30; y++)
        for (x = 0; x < 60; x++)
            if (get_pixel(x, y))
            {
                if (first < 0)
                {
                    first = y;
                    left_first = x;
                }
                last = y;
                left_last = x;
                break;
            }

    return first >= 0 && last > first && left_first != left_last;
}

/* the effects of the text blit on the screen's planes */
static void check_effects(void)
{
    long plain = draw_hi(0, 1), bold = draw_hi(1, 1), light = draw_hi(2, 1);
    long skew = draw_hi(4, 1);
    int leans = skew_leans();
    long under = draw_hi(8, 1), outline = draw_hi(16, 1);
    long trans, xor1, xor2;
    short bgc;

    check(plain > 10, "v_get_pixel() sees no text");
    check(bold > plain, "bold text is not thicker");
    check(light < plain && light > 0, "light text is not lighter");
    /* a skewed row is the row of plain text moved: no pixel is lost */
    check(skew == plain, "skewed text has lost or gained pixels");
    check(leans, "skewed text does not lean");
    check(under > plain, "underlined text has no underline");
    check(outline > plain, "outlined text has no outline");

    /* transparent over a filled rectangle keeps the rectangle around the text */
    bgc = wk_planes > 1 ? 2 : 1;       /* a one-plane screen has colours 0 and 1 */
    recfl(0, 0, 40, 20, bgc);
    vs_intin[0] = 2;
    vs_call(handle, 32, 0, 0, 1);       /* vswr_mode(transparent) */
    vs_ptsin[0] = 4;
    vs_ptsin[1] = 4;
    vs_intin[0] = 'H';
    vs_call(handle, 8, 0, 1, 1);
    trans = count_ink();
    check(get_pixel(39, 19) == bgc && get_pixel(0, 0) == bgc && trans > 40 * 20 / 2,
          "transparent text does not leave the background");

    /* XOR twice gives the background back */
    recfl(0, 0, 40, 20, 0);
    vs_intin[0] = 3;
    vs_call(handle, 32, 0, 0, 1);       /* vswr_mode(XOR) */
    vs_ptsin[0] = 4;
    vs_ptsin[1] = 4;
    vs_intin[0] = 'H';
    vs_call(handle, 8, 0, 1, 1);
    xor1 = count_ink();
    vs_ptsin[0] = 4;
    vs_ptsin[1] = 4;
    vs_intin[0] = 'H';
    vs_call(handle, 8, 0, 1, 1);
    xor2 = count_ink();
    vs_intin[0] = 1;
    vs_call(handle, 32, 0, 0, 1);       /* replace again */
    check(xor1 > 5 && xor2 == 0, "text in XOR mode twice does not cancel");
}

static void restore_and_close(void)
{
    if (aes_there)
    {
        vs_call(handle, 101, 0, 0, 0);  /* v_clsvwk() */
        aes(19, 0, 1);                  /* appl_exit() */
    }
    else
        vs_call(handle, 2, 0, 0, 0);    /* v_clswk() */
}

void test_planar_pixels(void)
{
    short i;

    /*
     * Under the AES (the suite started from the desktop) the screen is the
     * AES's: open a virtual workstation on it, as gdos_answer does; opening
     * and closing the physical one would take it from the desktop.
     */
    aes_global[0] = 0;
    aes(10, 0, 1);                      /* appl_init() */
    aes_there = aes_global[0] != 0;
    for (i = 0; i < 10; i++)
        vs_intin[i] = 1;
    vs_intin[10] = 2;                   /* raster coordinates */
    if (aes_there)
    {
        short phys;

        aes(77, 0, 5);                  /* graf_handle() */
        phys = vs_intout[0];
        for (i = 0; i < 10; i++)
            vs_intin[i] = 1;
        vs_intin[10] = 2;
        vs_call(phys, 100, 0, 0, 11);   /* v_opnvwk() */
    }
    else
        vs_call(0, 1, 0, 0, 11);        /* v_opnwk(), the screen */
    handle = vs_contrl[6];
    if (handle <= 0)
    {
        if (aes_there)
            aes(19, 0, 1);              /* appl_exit() */
        ptest_begin("planar_pixels (#598)");
        conws("SKIP (no workstation to open)\r\n");
        return;
    }
    vs_call(handle, 102, 0, 0, 1);      /* vq_extnd(): the planes */
    wk_planes = vs_intout[4];
    if (wk_planes < 1 || wk_planes > 4)
    {
        restore_and_close();
        ptest_begin("planar_pixels (#598)");
        conws("SKIP (not a screen of 16 colours or fewer)\r\n");
        return;
    }

    check_fill();
    check_edges();
    check_line();
    check_text();
    check_effects();
    restore_and_close();

    ptest_begin("planar_pixels (#598)");
    ptest_assert_msg(!failure, failure ? failure : "");
    ptest_pass();
}

#endif
