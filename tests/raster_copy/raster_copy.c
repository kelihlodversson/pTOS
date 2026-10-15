/*
 * raster_copy.c - regression test for VDI screen to screen raster copies
 *
 * Part of the blitter text epic (#498), issue #503.
 *
 * vro_cpyfm() between two areas of the screen has a software path (the
 * assembler fast_bit_blt() on m68k) and, where the machine has a blitter, a
 * hardware path (bit_blt() and the hwblit_* functions in vdi/vdi_raster.c).
 * This test makes the same copies through both and compares the screen
 * memory, for every writing mode, several widths, heights and bit alignments
 * of source and destination, and for destinations that overlap the source in
 * every direction.  The path is selected with Blitmode().
 *
 * The test needs a bitplane screen with the planes interleaved a word at a
 * time (1, 2, 4 or 8 planes, 320 or 640 pixels wide: an ST, an STE, a TT
 * or a Falcon) and a blitter; it is skipped everywhere else.
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

void test_raster_copy(void);

#ifndef __mc68000__

void test_raster_copy(void)
{
    ptest_begin("raster_copy (#503)");
    conws("SKIP (not an m68k machine)\r\n");
}

#else   /* __mc68000__ */

typedef unsigned char U8;

/* the lines of the screen the test uses, at its bottom, and at most the bytes in them */
#define BAND_LINES      24
#define MAX_BPL         640           /* 8 planes of 640 pixels */
#define BAND_MAX        (BAND_LINES * MAX_BPL)

static short vh;
static short scr_w, scr_h, planes;
static long bpl;                /* bytes per scanline */
static long band_y;             /* first scanline of the band */
static U8 *band;                /* its screen memory */
static long band_len;
static int blitter_was_on;

static U8 saved[BAND_MAX] __attribute__((aligned(4)));      /* the screen before the test */
static U8 pattern[BAND_MAX] __attribute__((aligned(4)));    /* what the band is set to before each copy */
static U8 snap_cpu[BAND_MAX] __attribute__((aligned(4)));   /* result of the software path */
static U8 snap_blt[BAND_MAX] __attribute__((aligned(4)));   /* result of the blitter path */

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

/* ---- the machine ---- */

static void set_cursor(int on)
{
    long old;

    old = Cursconf(on ? 1 : 0, 0);
    (void)old;
}

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

    if ((planes != 1 && planes != 2 && planes != 4 && planes != 8)
        || (scr_w != 320 && scr_w != 640))
        return -3;
    bpl = (long)(scr_w >> 4) * 2 * planes;
    if (bpl * BAND_LINES > BAND_MAX || scr_h < 2 * BAND_LINES)
        return -3;

    band_y = scr_h - BAND_LINES - 8;
    band = (U8 *)Logbase() + band_y * bpl;
    band_len = bpl * BAND_LINES;
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

/* the buffers are long aligned and a band is a whole number of longs */
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

/* ---- one case ---- */

typedef struct {
    short mode;                 /* vro_cpyfm() writing mode, 0-15 */
    short w, h;
    short sx, sy;               /* source, x in pixels, y in lines of the band */
    short dx, dy;               /* destination */
    short clip;                 /* if set, clip to the destination less 3 pixels or lines on each side */
} CASE;

static void report(const CASE *c, long off, int got, int want)
{
    if (reports >= 8)
        return;
    reports++;
    conws("\r\n    blitter differs from CPU:");
    pnums("mode", c->mode);
    pnums("w", c->w);
    pnums("h", c->h);
    pnums("src", c->sx);
    conws(",");
    pnum(c->sy);
    pnums("dst", c->dx);
    conws(",");
    pnum(c->dy);
    pnums("clip", c->clip);
    pnums("line", off / bpl);
    pnums("byte", off % bpl);
    pnums("got", got);
    pnums("want", want);
}

static void draw(const CASE *c)
{
    GRECT src, dst, cr;

    src.x = c->sx;
    src.y = (WORD)(band_y + c->sy);
    src.w = c->w;
    src.h = c->h;
    dst.x = c->dx;
    dst.y = (WORD)(band_y + c->dy);
    dst.w = c->w;
    dst.h = c->h;
    if (c->clip)
    {
        cr.x = (WORD)(c->dx + 3);
        cr.y = (WORD)(band_y + c->dy + 1);
        cr.w = (WORD)(c->w - 6 > 1 ? c->w - 6 : 1);
        cr.h = (WORD)(c->h - 2 > 1 ? c->h - 2 : 1);
        vs_clip(vh, 1, &cr);
    }
    vro_cpyfm(vh, c->mode, &src, &dst);
    if (c->clip)
        vs_clip(vh, 0, &cr);
}

static void check(const CASE *c)
{
    long d;

    cases_run++;

    copy(band, pattern);
    set_blitter(0);
    draw(c);
    copy(snap_cpu, band);

    copy(band, pattern);
    set_blitter(1);
    draw(c);
    copy(snap_blt, band);

    /*
     * mode 5 leaves the destination as it is, whatever the path: the
     * blitter is checked against the band as it was, and then the CPU path
     * against the blitter's
     */
    if (c->mode == 5)
    {
        d = first_diff(snap_blt, pattern);
        if (d >= 0)
        {
            report(c, d, snap_blt[d], pattern[d]);
            cases_failed++;
            return;
        }
    }

    d = first_diff(snap_cpu, snap_blt);
    if (d >= 0)
    {
        report(c, d, snap_blt[d], snap_cpu[d]);
        cases_failed++;
    }
}

/* ---- the test cases ---- */

static const short widths[] = { 1, 16, 17, 33, 70 };
static const short heights[] = { 1, 7 };
static const short src_x[] = { 0, 5, 13 };
static const short dst_x[] = { 0, 3, 9, 15 };

/*
 * where the destination is in relation to the source: far to the right,
 * below, on the same lines (it overlaps where the width reaches), one line
 * down, one line up (the overlap goes the other way)
 */
#define NLAYOUTS 5

static void place(CASE *c, int layout, int sx, int dx)
{
    c->sx = (short)(8 + sx);
    c->sy = 2;
    switch (layout)
    {
    case 0:
        c->dx = (short)(100 + dx);
        c->dy = 2;
        break;
    case 1:
        c->dx = (short)(8 + dx);
        c->dy = 12;
        break;
    case 2:
        c->dx = (short)(8 + dx);
        c->dy = 2;
        break;
    case 3:
        c->dx = (short)(8 + dx);
        c->dy = 3;
        break;
    default:
        c->dx = (short)(8 + dx);
        c->dy = 1;
        break;
    }
}

static void group_copies(void)
{
    CASE c;
    int mode, wi, hi, si, di, layout;

    for (mode = 0; mode < 16; mode++)
        for (wi = 0; wi < (int)(sizeof(widths) / sizeof(widths[0])); wi++)
            for (hi = 0; hi < (int)(sizeof(heights) / sizeof(heights[0])); hi++)
                for (si = 0; si < (int)(sizeof(src_x) / sizeof(src_x[0])); si++)
                    for (di = 0; di < (int)(sizeof(dst_x) / sizeof(dst_x[0])); di++)
                        for (layout = 0; layout < NLAYOUTS; layout++)
                        {
                            c.mode = (short)mode;
                            c.w = widths[wi];
                            c.h = heights[hi];
                            c.clip = 0;
                            place(&c, layout, src_x[si], dst_x[di]);
                            check(&c);
                        }
}

/* the same under a clip rectangle, for the sizes that have room for one */
static void group_clipped(void)
{
    CASE c;
    int mode, wi, si, di, layout;

    for (mode = 0; mode < 16; mode += 3)
        for (wi = 1; wi < (int)(sizeof(widths) / sizeof(widths[0])); wi++)
            for (si = 0; si < (int)(sizeof(src_x) / sizeof(src_x[0])); si++)
                for (di = 0; di < (int)(sizeof(dst_x) / sizeof(dst_x[0])); di++)
                    for (layout = 0; layout < NLAYOUTS; layout++)
                    {
                        c.mode = (short)mode;
                        c.w = widths[wi];
                        c.h = 7;
                        c.clip = 1;
                        place(&c, layout, src_x[si], dst_x[di]);
                        check(&c);
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

void test_raster_copy(void)
{
    int rc;

    rc = setup();
    if (rc)
    {
        ptest_begin("raster_copy (#503)");
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

    /* the mouse and the console's cursor would change the band between the renderings */
    v_hide_c(vh);
    set_cursor(0);
    copy(saved, band);
    make_pattern();

    run_group("raster_copy: screen copies, CPU vs blitter (#503)", group_copies);
    run_group("raster_copy: clipped screen copies, CPU vs blitter (#503)", group_clipped);

    copy(band, saved);
    set_cursor(1);
    v_show_c(vh, 1);
    set_blitter(blitter_was_on);
    v_clsvwk(vh);
    appl_exit();
}

#endif  /* __mc68000__ */
