/*
 * system_font.c - regression test for the system font (#600)
 *
 * The system font (font id 1: the console's, the AES's, and that of every
 * program that picks no other font) is a ROM font, or, when a monospaced
 * scalable font has been made the system font (CONF_VDI_SYSTEM_FONT, or
 * SYSTEMFONT= in \GEMSYS\PTOS.INF, with the built-in GDOS), a rendering of
 * it at the cells of the ROM fonts.  Either way it has to be what the ROM
 * fonts are to a program: cells of 8 pixels by 8 or 16 rows that the
 * attributes and the sizes agree on, characters that draw inside them,
 * none of the printable ones blank, and the arrows of the low characters
 * (which not every scalable font has: those keep the ROM font's glyph).
 *
 * tools/run-regression-tests.sh runs the suite once with the ROM fonts and
 * once with a scalable system font, and checks the log of the second for
 * the message that the system font was made.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include "include/vdi_session.h"

void test_system_font(void);

#if !defined(__mc68000__) && !defined(__arm__)

void test_system_font(void)
{
    ptest_begin("system_font (#600)");
    conws("SKIP (no VDI for programs here)\r\n");
}

#else

#define MARGIN  4                       /* looked at around a cell */

static short handle;

static short get_pixel(short x, short y)
{
    vs_ptsin[0] = x;
    vs_ptsin[1] = y;
    vs_call(handle, 105, 0, 1, 0);

    return vs_intout[1];
}

/* the cell and its margin cleared, then the character drawn at the corner of the cell */
static void draw(short x, short y, short w, short h, short ch)
{
    vs_intin[0] = 1;
    vs_call(handle, 23, 0, 0, 1);       /* vsf_interior(solid) */
    vs_intin[0] = 0;
    vs_call(handle, 25, 0, 0, 1);       /* vsf_color(0) */
    vs_ptsin[0] = x - MARGIN;
    vs_ptsin[1] = y - MARGIN;
    vs_ptsin[2] = x + w + MARGIN;
    vs_ptsin[3] = y + h + MARGIN;
    vs_call(handle, 114, 0, 2, 0);      /* vr_recfl() */
    vs_ptsin[0] = x;
    vs_ptsin[1] = y;
    vs_intin[0] = ch;
    vs_call(handle, 8, 0, 1, 1);        /* v_gtext() */
}

/* what the first character that was wrong looks like, with its margin, for the log */
static char art[16 + 2 * MARGIN][8 + 2 * MARGIN + 1];
static short art_rows;

/*
 * The character drawn at (x, y) as it is on the screen, read once: with
 * the margin around the cell, or only the cell.  Returns the pixels set in
 * the cell, and in *outside those set around it.
 */
static long read_cell(short x, short y, short w, short h, int margin, long *outside)
{
    static char snap[16 + 2 * MARGIN][8 + 2 * MARGIN + 1];
    short m = margin ? MARGIN : 0, xx, yy;
    long in = 0, out = 0;

    for (yy = 0; yy < h + 2 * m; yy++)
    {
        for (xx = 0; xx < w + 2 * m; xx++)
        {
            int set = get_pixel(x - m + xx, y - m + yy) != 0;
            int inside = xx >= m && xx < m + w && yy >= m && yy < m + h;

            snap[yy][xx] = set ? '#' : '.';
            if (set)
            {
                if (inside)
                    in++;
                else
                    out++;
            }
        }
        snap[yy][w + 2 * m] = '\0';
    }
    *outside = out;
    if ((!in || out) && !art_rows && margin)
    {
        art_rows = h + 2 * m;
        for (yy = 0; yy < art_rows; yy++)
            for (xx = 0; xx <= w + 2 * m; xx++)
                art[yy][xx] = snap[yy][xx];
    }

    return in;
}

static const char *failure;
static char failure_buf[64];

/* the message is copied: about() reuses its buffer */
static void check(int ok, const char *msg)
{
    char *p = failure_buf;

    if (ok || failure)
        return;
    while (*msg && p < failure_buf + sizeof failure_buf - 1)
        *p++ = *msg++;
    *p = '\0';
    failure = failure_buf;
}

/* a message with the character it is about */
static char char_msg[64];

static const char *about(const char *what, short ch)
{
    char *p = char_msg;

    while (*what)
        *p++ = *what++;
    *p++ = ' ';
    *p++ = '0';
    *p++ = 'x';
    *p++ = "0123456789abcdef"[(ch >> 4) & 15];
    *p++ = "0123456789abcdef"[ch & 15];
    *p = '\0';

    return char_msg;
}

static void set_cursor(int on)
{
    long old;

    old = Cursconf(on ? 1 : 0, 0);
    (void)old;
}

void test_system_font(void)
{
    static const char shown[] = "AWilgjqQ@#%&{}[]|_~^0189.,;:'\\\"!?/-+=<>()$`*";
    short x = 24, y = 12, w, h, screen_h, i, ch;

    handle = vs_open_screen();
    if (handle <= 0)
    {
        ptest_begin("system_font (#600)");
        conws("SKIP (no workstation to open)\r\n");
        return;
    }
    screen_h = vs_intout[1] + 1;

    set_cursor(0);
    vs_call(handle, 123, 0, 0, 0);      /* v_hide_c() */

    vs_intin[0] = 1;
    vs_call(handle, 21, 0, 0, 1);       /* vst_font(the system font) */
    vs_intin[0] = 1;
    vs_call(handle, 22, 0, 0, 1);       /* vst_color(1) */
    vs_intin[0] = 0;
    vs_intin[1] = 5;
    vs_call(handle, 39, 0, 0, 2);       /* vst_alignment(left, top) */
    vs_call(handle, 38, 0, 0, 0);       /* vqt_attributes() */
    w = vs_ptsout[2];
    h = vs_ptsout[3];
    check(w == 8, "the system font's cells are not 8 pixels wide");
    check(h == (screen_h < 400 ? 8 : 16), "the system font's cells are not those of the screen");
    check(vs_intout[0] == 1, "the system font is not font 1");

    if (w == 8 && (h == 8 || h == 16))
    {
        /* each printable character has ink, in its cell; the wide and deep ones nothing around it */
        for (i = 0; shown[i]; i++)
        {
            long outside;
            int wide = shown[i] == 'W' || shown[i] == 'g' || shown[i] == 'j' || shown[i] == 'Q'
                       || shown[i] == '@' || shown[i] == '|' || shown[i] == '_' || shown[i] == '~';

            draw(x, y, w, h, shown[i]);
            check(read_cell(x, y, w, h, wide, &outside) > 0,
                  about("a printable character is blank:", shown[i]));
            check(!outside, about("a character is drawn outside its cell:", shown[i]));
        }
        /* the arrows */
        for (ch = 1; ch <= 4; ch++)
        {
            long outside;

            draw(x, y, w, h, ch);
            check(read_cell(x, y, w, h, 0, &outside) > 0,
                  about("an arrow character is blank:", ch));
        }
    }

    vs_intin[0] = 1;
    vs_call(handle, 122, 0, 0, 1);      /* v_show_c(reset) */
    set_cursor(1);
    vs_close_screen(handle);

    ptest_begin("system_font (#600)");
    if (failure && art_rows)
    {
        short r;

        conws("\r\n");
        for (r = 0; r < art_rows; r++)
        {
            conws(art[r]);
            conws("\r\n");
        }
    }
    ptest_assert_msg(!failure, failure ? failure : "");
    ptest_pass();
}

#endif
