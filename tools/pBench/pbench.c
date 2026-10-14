/*
 * pbench.c - pBench, a graphics benchmark for pTOS
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * pBench is a GEM program that times AES and VDI work.  Its tests model the
 * graphics tests of GEM Bench 6: the workload of each one was reconstructed
 * by recording the AES/VDI calls GEM Bench makes under pTOS, and is replayed
 * here through the documented OS interfaces only -- no direct hardware access
 * -- so it runs on every pTOS target.  See doc/pbench.txt.
 *
 * Usage:  pbench.prg [-q] [-r N] [-l] [TESTS]
 *   -q          quit when done instead of waiting for a key
 *   -r N        repeat each test N times (the time reported is per run)
 *               Without -r, a test that finishes in under two seconds is
 *               repeated, so that fast machines still get a meaningful time
 *   -l          list the tests
 *   TESTS       letters selecting tests (default: all)
 *
 * Started without arguments, as from the desktop, pBench takes them from the
 * first line of PBENCH.CFG, if there is one.
 *
 * Results go to the screen, the serial port (AUX:) and PBENCH.TXT.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mint/osbind.h>
#include "gem.h"

/* the test region, in the coordinates GEM Bench's tests draw in */
static GRECT area = { 8, 23, 624, 159 };

static WORD vh;                 /* VDI handle */
static WORD scr_w, scr_h;       /* screen size */
static WORD gl_wchar, gl_hchar;
static WORD ncolors = 2;        /* colours the screen can show */
static int blitter;             /* is the blitter in use? */
static WORD planes;             /* bits per pixel */
static long repeat = 0;         /* repeat count asked for with -r, 0 = auto */

#define MIN_TICKS   400L        /* 2 s at 200 Hz */
#define MAX_MULT    4000L

/* the text GEM Bench's text tests print (91 and 94 characters) */
static const char text91[] =
    "VDI v_gtext() speed test: the quick brown fox jumps over the lazy dog 0123456789 ABCDEFGHIJ";
static const char blank91[] =
    "                                                                                           ";
static const char text94[] =
    "vst_effects() speed test: the quick brown fox jumps over the lazy dog 0123456789 ABCDEFGHIJKL";
static const char text69[] =
    "VDI scroll test: the quick brown fox jumps over the lazy dog 0123456";
static const char textj[] = "Justified text";

/* a text colour index the screen has: 1 .. ncolors - 1 */
static WORD tcol(WORD n)
{
    return ncolors > 2 ? (n - 1) % (ncolors - 1) + 1 : 1;
}

static void con(const char *s)
{
    (void)Cconws(s);
}

static void clear_area(void)
{
    vswr_mode(vh, MD_REPLACE);
    vsf_color(vh, 0);
    vsf_interior(vh, 1);
    vr_recfl(vh, area.x, area.y, area.x + area.w - 1, area.y + area.h - 1);
}

static void set_clip(void)
{
    vs_clip(vh, 1, &area);
}

/* ---- the tests ---- */

/*
 * GEM Dialog Box: draw a large dialog the way form_do() setup does --
 * form_dial() start/grow, objc_draw(), form_dial() shrink/finish.
 */
#define NOBJ        24
static OBJECT dlg[NOBJ];
static TEDINFO dlg_ted[2];
static char dlg_text[2][16] = { "pBench", "123456" };
static char dlg_tmplt[2][20] = { "Name: ________", "Num: ______" };
static char dlg_valid[2][4] = { "X", "9" };
static const char *dlg_labels[] = {
    "pBench dialog box", "Display", "Resolution", "Colours", "Colour depth",
    "Mouse", "Keyboard", "Sound", "System", "Memory", "Disk"
};
static const char *dlg_buttons[] = { "OK", "Cancel", "Apply", "Help", "More" };

static void build_dialog(void)
{
    int i;
    int n = 0;

    memset(dlg, 0, sizeof(dlg));
    memset(dlg_ted, 0, sizeof(dlg_ted));

    /* the root box: black thick border, white interior */
    dlg[0].ob_next = -1;
    dlg[0].ob_head = 1;
    dlg[0].ob_type = G_BOX;
    dlg[0].ob_flags = 0;
    dlg[0].ob_state = OS_OUTLINED;
    dlg[0].ob_spec = (0xFEL << 16) | 0x11F0;     /* thick (-2) frame */
    dlg[0].ob_x = 0;
    dlg[0].ob_y = 0;
    dlg[0].ob_w = 532;
    dlg[0].ob_h = 164;
    n = 1;

    for (i = 0; i < 11; i++)
    {
        dlg[n].ob_type = G_STRING;
        dlg[n].ob_spec = (LONG)dlg_labels[i];
        dlg[n].ob_x = 16 + (i / 4) * 170;
        dlg[n].ob_y = 12 + (i % 4) * 18;
        dlg[n].ob_w = 150;
        dlg[n].ob_h = 16;
        n++;
    }
    for (i = 0; i < 5; i++)
    {
        dlg[n].ob_type = G_BUTTON;
        dlg[n].ob_flags = OF_SELECTABLE | OF_EXIT;
        dlg[n].ob_state = i == 0 ? OS_SELECTED : 0;
        dlg[n].ob_spec = (LONG)dlg_buttons[i];
        dlg[n].ob_x = 20 + i * 100;
        dlg[n].ob_y = 130;
        dlg[n].ob_w = 80;
        dlg[n].ob_h = 22;
        n++;
    }
    for (i = 0; i < 2; i++)
    {
        dlg_ted[i].te_ptext = dlg_text[i];
        dlg_ted[i].te_ptmplt = dlg_tmplt[i];
        dlg_ted[i].te_pvalid = dlg_valid[i];
        dlg_ted[i].te_font = 3;
        dlg_ted[i].te_just = 0;
        dlg_ted[i].te_color = 0x1180;
        dlg_ted[i].te_thickness = 1;
        dlg_ted[i].te_txtlen = 7;
        dlg_ted[i].te_tmplen = 15;
        dlg[n].ob_type = G_FTEXT;
        dlg[n].ob_flags = OF_EDITABLE;
        dlg[n].ob_spec = (LONG)&dlg_ted[i];
        dlg[n].ob_x = 20 + i * 250;
        dlg[n].ob_y = 90;
        dlg[n].ob_w = 200;
        dlg[n].ob_h = 20;
        n++;
    }
    /* a couple of boxes with a border and a shadow */
    for (i = 0; i < 4; i++)
    {
        dlg[n].ob_type = G_BOX;
        dlg[n].ob_state = i == 1 ? OS_SHADOWED : 0;
        dlg[n].ob_spec = (0x01L << 16) | 0x1170 | (i & 3);   /* +1 frame */
        dlg[n].ob_x = 360 + (i % 2) * 80;
        dlg[n].ob_y = 12 + (i / 2) * 40;
        dlg[n].ob_w = 70;
        dlg[n].ob_h = 30;
        n++;
    }

    /* chain every object to the root; they have no children of their own */
    for (i = 1; i < n; i++)
    {
        dlg[i].ob_next = i + 1;
        dlg[i].ob_head = -1;
        dlg[i].ob_tail = -1;
    }
    dlg[n - 1].ob_next = 0;
    dlg[0].ob_tail = n - 1;
    dlg[n - 1].ob_flags |= OF_LASTOB;
}

static long test_dialog(void)
{
    WORD i, count = 32;
    WORD w = dlg[0].ob_w, h = dlg[0].ob_h;
    WORD x = (scr_w - w) / 2, y = (scr_h - h) / 2;

    dlg[0].ob_x = x;
    dlg[0].ob_y = y;
    for (i = 0; i < count; i++)
    {
        form_dial(FMD_START, 0, 0, 0, 0, x, y, w, h);
        form_dial(FMD_GROW, x + w / 2, y + h / 2, 0, 0, x, y, w, h);
        objc_draw(dlg, 0, MAX_DEPTH, x, y, w, h);
        form_dial(FMD_SHRINK, x + w / 2, y + h / 2, 0, 0, x, y, w, h);
        form_dial(FMD_FINISH, 0, 0, 0, 0, x, y, w, h);
    }
    return count;
}

/*
 * VDI Text: v_gtext() of full lines in the 8x8 font, a page of text then a
 * page of blanks over it, with the text colour changing every pass.
 */
static long test_text(void)
{
    WORD pass, line, passes = 11;
    long calls = 0;

    set_clip();
    clear_area();
    vst_point(vh, 9);
    for (pass = 0; pass < passes; pass++)
    {
        vst_color(vh, tcol(pass % 3 + 1));
        for (line = 0; line < 21; line++)
            v_gtext(vh, 2, 31 + line * 9, text91);
        for (line = 0; line < 21; line++)
            v_gtext(vh, 2, 31 + line * 9, blank91);
        calls += 42;
    }
    return calls;
}

/*
 * VDI Text Effects: the same lines in each of the text effects 1..16.
 */
static long test_effects(void)
{
    WORD effect, line;
    long calls = 0;

    set_clip();
    clear_area();
    vst_point(vh, 9);
    vsf_color(vh, 0);
    for (effect = 1; effect <= 16; effect++)
    {
        vst_color(vh, tcol((effect - 1) % 3 + 1));
        vst_effects(vh, effect);
        for (line = 0; line < 22; line++)
            v_gtext(vh, 1, 31 + line * 9, text94);
        vr_recfl(vh, 1, 22, 640, 187);
        calls += 22;
    }
    vst_effects(vh, 0);
    return calls;
}

/*
 * VDI Small Text: as VDI Text, in the 6x6 font.
 */
static long test_small(void)
{
    WORD pass, line, passes = 11;
    long calls = 0;

    set_clip();
    clear_area();
    vst_point(vh, 6);
    vst_color(vh, 1);
    for (pass = 0; pass < passes; pass++)
    {
        for (line = 0; line < 28; line++)
            v_gtext(vh, 2, 28 + line * 6, text91);
        for (line = 0; line < 28; line++)
            v_gtext(vh, 2, 28 + line * 6, blank91);
        calls += 56;
    }
    vst_point(vh, 9);
    return calls;
}

/*
 * VDI Graphics: filled bars and ellipses sliding across the area, then three
 * fans of two-point polylines.
 */
static long test_graphics(void)
{
    WORD i, k, fan, y1, count = 81, lines = 131;
    WORD x, xr, yr;
    long calls = 0;

    set_clip();
    clear_area();
    vsl_color(vh, 1);
    vsf_style(vh, 1);
    vsf_interior(vh, 1);
    for (i = 0; i < count; i++)
    {
        if (i)
            vsf_interior(vh, 2 - (i & 1));
        x = 320 + 2 * i;
        xr = 50 + i;
        yr = 50 - i / 2;
        vsf_color(vh, 1);
        v_bar(vh, 10 + 2 * i, 30, 260 + i, 100 + i);
        vsf_color(vh, 0);
        v_bar(vh, 10 + 2 * i, 30, 260 + i, 100 + i);
        vsf_color(vh, 1);
        v_ellipse(vh, x, 110, xr, yr);
        vsf_color(vh, 0);
        v_ellipse(vh, x, 110, xr, yr);
        calls += 4;
    }
    /* fans of two-point lines, drawn XOR over the shapes */
    vswr_mode(vh, 3);
    for (fan = 0; fan < 14; fan++)
    {
        y1 = 50 + (fan % 7) * 20;
        for (k = 0; k < lines; k++)
        {
            if (fan < 7)
                v_pline2(vh, 8, y1, 607, 50 + k);
            else
                v_pline2(vh, 607, y1, 8, 50 + k);
        }
        calls += lines;
    }
    vswr_mode(vh, MD_REPLACE);
    return calls;
}

/*
 * GEM Window: create, open (AES animates the opening), close and delete a
 * full-screen window, over and over.
 */
/*
 * Opening and closing windows makes the AES send this program redraw and
 * other messages, which it never reads: left alone they pile up, and with
 * some AES versions (TOS 4.04) the AES eventually stops waiting for room in
 * the queue.  So read and drop them.
 */
static void drain_messages(void)
{
    WORD msg[8];

    while (evnt_mesag_poll(msg))
        ;
}

static long test_window(void)
{
    WORD i, count = 65;
    WORD handle, x, y, w, h;
    WORD wx, wy, ww, wh;

    wind_get(0, WF_WORKXYWH, &x, &y, &w, &h);
    for (i = 0; i < count; i++)
    {
        handle = wind_create(0xFEF, x, y, w, h);
        if (handle < 0)
            break;
        wind_set_str(handle, WF_NAME, " pBench window ");
        wind_set_str(handle, WF_INFO, " pBench window test ");
        wind_open(handle, x, y, w, h);
        wind_get(handle, WF_WORKXYWH, &wx, &wy, &ww, &wh);
        drain_messages();
        wind_close(handle);
        wind_delete(handle);
        drain_messages();
        wind_get(0, WF_WORKXYWH, &wx, &wy, &ww, &wh);
    }
    return i;
}

/*
 * Blitting: screen to screen vro_cpyfm() of a box, S_ONLY.
 */
static long test_blit(void)
{
    WORD i, k, t, count = 1200;
    GRECT src, dst;

    set_clip();
    clear_area();
    vsf_color(vh, 1);
    vsf_interior(vh, 2);
    vsf_style(vh, 1);
    v_bar(vh, 12, 24, 208, 70);
    src.x = 10;
    src.y = 22;
    src.w = 201;
    src.h = 51;
    dst.w = 201;
    dst.h = 51;
    /* the destination wanders right and up, then back again */
    for (i = 0; i < count; i++)
    {
        t = i % 400;
        k = t <= 200 ? t : 400 - t;
        dst.x = 10 + 2 * k;
        dst.y = 120 - k / 3;
        vro_cpyfm(vh, S_ONLY, &src, &dst);
    }
    vsf_interior(vh, 1);
    return count;
}

/*
 * VDI Scroll: print a line at the bottom, scroll the area up by one line.
 */
static long test_scroll(void)
{
    WORD i, count = 219;
    GRECT src, dst;

    set_clip();
    clear_area();
    vst_point(vh, 6);
    vst_color(vh, 1);
    vsf_color(vh, 0);
    vsf_interior(vh, 1);
    src.x = 1;
    src.y = 22;
    src.w = 607;
    src.h = 158;
    dst.x = 1;
    dst.y = 14;
    dst.w = 607;
    dst.h = 158;
    for (i = 0; i < count; i++)
    {
        v_gtext(vh, 10, 179, text69);
        vro_cpyfm(vh, S_ONLY, &src, &dst);
        vr_recfl(vh, 9, 171, 10, 179);
    }
    vst_point(vh, 9);
    return count;
}

/*
 * Justified Text: v_justified() lines, a page at a time.
 */
static long test_justified(void)
{
    WORD pass, line, passes = 73;
    long calls = 0;

    set_clip();
    clear_area();
    vst_point(vh, 9);
    vst_color(vh, 1);
    vst_effects(vh, 0);
    vsf_color(vh, 0);
    vsf_interior(vh, 1);
    vst_alignment(vh, 1, 0);
    for (pass = 0; pass < passes; pass++)
    {
        for (line = 0; line < 20; line++)
            v_justified(vh, 300, 33 + line * 8, textj, 23 + pass * 8, 1, 1);
        vr_recfl(vh, 2, 23, 607, 187);
        calls += 20;
    }
    vst_alignment(vh, 0, 0);
    return calls;
}

/*
 * VDI Enquire: the queries applications make all the time.
 */
static long test_enquire(void)
{
    WORD i, count = 4794;
    WORD ext[57], attr[10], a, b, dist[5], eff[3], pts[8], st, mx, my, ks;

    for (i = 0; i < count; i++)
    {
        vq_extnd(vh, 0, ext);
        vql_attributes(vh, attr);
        vqm_attributes(vh, attr);
        vqf_attributes(vh, attr);
        vqt_attributes(vh, attr);
        vqt_fontinfo(vh, &a, &b, dist, &mx, eff);
        vq_mouse(vh, &st, &mx, &my);
        vq_key_s(vh, &ks);
        vqt_extent(vh, textj, pts);
    }
    return count * 9L;
}

typedef long (*test_fn)(void);

struct test {
    char key;
    const char *name;
    test_fn fn;
    const char *unit;
};

static const struct test tests[] = {
    { 'd', "GEM Dialog Box",   test_dialog,    "dialogs" },
    { 't', "VDI Text",         test_text,      "lines" },
    { 'f', "VDI Text Effects", test_effects,   "lines" },
    { 's', "VDI Small Text",   test_small,     "lines" },
    { 'g', "VDI Graphics",     test_graphics,  "objects" },
    { 'w', "GEM Window",       test_window,    "windows" },
    { 'b', "Blitting",         test_blit,      "blits" },
    { 'r', "VDI Scroll",       test_scroll,    "scrolls" },
    { 'j', "Justified Text",   test_justified, "lines" },
    { 'e', "VDI Enquire",      test_enquire,   "calls" }
};

#define NTESTS ((int)(sizeof(tests) / sizeof(tests[0])))

/*
 * Reference times: pBench's own results, in ms, for TOS 2.06 under Hatari in
 * each screen mode, with and without the blitter (see tools/pBench/baselines
 * and doc/pbench.txt).  A test's speed is shown relative to the reference
 * for the screen mode in use, like GEM Bench does with its benchmark files.
 */
#include "refs.h"

static const struct pb_ref *reference;

static const struct pb_ref *find_reference(void)
{
    int i, best = -1;

    /* true colour is compared with the true colour reference, if any */
    if (planes > 8)
        for (i = 0; i < PB_NREFS; i++)
            if (pb_refs[i].planes > 8)
                return &pb_refs[i];
    for (i = 0; i < PB_NREFS; i++)
        if (pb_refs[i].planes == 0 && pb_refs[i].blitter == blitter)
        {
            if (pb_refs[i].w == scr_w && pb_refs[i].h == scr_h)
                return &pb_refs[i];
            if (pb_refs[i].w == 640 && pb_refs[i].h == 200)
                best = i;       /* the mode GEM Bench was designed for */
        }
    return best >= 0 ? &pb_refs[best] : &pb_refs[0];
}

static char report[NTESTS + 3][96];
static int nreport;

/* the report also goes to the serial port, for unattended runs */
static void aux_puts(const char *s)
{
    while (*s)
        (void)Cauxout(*s++);
}

static void emit(const char *line)
{
    aux_puts(line);
    aux_puts("\r\n");
    if (nreport < NTESTS + 3)
    {
        size_t n = strlen(line);

        if (n > sizeof(report[0]) - 1)
            n = sizeof(report[0]) - 1;
        memcpy(report[nreport], line, n);
        report[nreport][n] = 0;
        nreport++;
    }
}

static void save_report(void)
{
    int fd, i;

    fd = Fcreate("PBENCH.TXT", 0);
    if (fd < 0)
        return;
    for (i = 0; i < nreport; i++)
    {
        Fwrite(fd, strlen(report[i]), report[i]);
        Fwrite(fd, 2, "\r\n");
    }
    Fclose(fd);
}

/* draw the report in the test area */
static void show_report(void)
{
    int i;

    set_clip();
    clear_area();
    vst_point(vh, 9);
    vst_color(vh, 1);
    for (i = 0; i < nreport; i++)
        v_gtext(vh, area.x + 8, area.y + 16 + i * 10, report[i]);
}

static void run_test(int index)
{
    long start, ticks, ops, ms, mult = repeat ? repeat : 1;
    long i;
    const struct test *t = &tests[index];
    char line[96];

    for (;;)
    {
        ops = 0;
        start = clock();
        for (i = 0; i < mult; i++)
            ops += t->fn();
        ticks = clock() - start;
        if (ticks < 1)
            ticks = 1;
        if (repeat || ticks >= MIN_TICKS || mult >= MAX_MULT)
            break;
        /* too quick to time: go again, aiming for about three seconds */
        mult = mult * (3 * 200 / ticks + 1);
        if (mult > MAX_MULT)
            mult = MAX_MULT;
    }
    ms = ticks * 5 / mult;
    if (ms < 1)
        ms = 1;
    ops /= mult;
    if (reference)
        sprintf(line, "%-18s %5ld.%03ld s %6ld %-8s %5ld%%", t->name,
                ms / 1000, ms % 1000, ops, t->unit,
                reference->ms[index] * 100 / ms);
    else
        sprintf(line, "%-18s %5ld.%03ld s %6ld %-8s", t->name,
                ms / 1000, ms % 1000, ops, t->unit);
    emit(line);
}

static int selected(const struct test *t, const char *sel)
{
    return !sel || strchr(sel, t->key) != NULL;
}

#define MAX_ARGS 16

/* the arguments from the first line of PBENCH.CFG */
static int read_config(char **args)
{
    static char buf[256];
    long n;
    int fd, count = 0;
    char *p;

    fd = Fopen("PBENCH.CFG", 0);
    if (fd < 0)
        return 0;
    n = Fread(fd, sizeof(buf) - 1, buf);
    Fclose(fd);
    if (n <= 0)
        return 0;
    buf[n] = 0;
    for (p = buf; *p && *p != '\r' && *p != '\n' && count < MAX_ARGS - 1; )
    {
        while (*p == ' ' || *p == '\t')
            *p++ = 0;
        if (!*p || *p == '\r' || *p == '\n')
            break;
        args[count++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
            p++;
    }
    if (*p)
        *p = 0;
    return count;
}

int main(int argc, char **argv)
{
    char *cfgargs[MAX_ARGS];
    int i, quit = 0, list = 0;
    const char *sel = NULL;
    WORD ww, hh;
    WORD handle, ext[45];

    if (argc <= 1)
    {
        argc = read_config(cfgargs + 1) + 1;
        argv = cfgargs;
    }
    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-q"))
            quit = 1;
        else if (!strcmp(argv[i], "-l"))
            list = 1;
        else if (!strcmp(argv[i], "-r") && i + 1 < argc)
            repeat = atol(argv[++i]);
        else
            sel = argv[i];
    }
    if (repeat < 0)
        repeat = 0;

    if (list)
    {
        for (i = 0; i < NTESTS; i++)
        {
            con(tests[i].name);
            con("\r\n");
        }
        return 0;
    }

    if (appl_init() < 0)
    {
        con("pBench needs the AES\r\n");
        return 1;
    }
    handle = graf_handle(&gl_wchar, &gl_hchar, &ww, &hh);
    vh = v_opnvwk(handle, ext);
    scr_w = ext[0] + 1;
    scr_h = ext[1] + 1;
    ncolors = ext[13];
    if (ncolors < 2)
        ncolors = 2;
    {
        WORD vext[57];

        vq_extnd(vh, 1, vext);
        planes = vext[4];
    }
    blitter = (Blitmode(-1) & 3) == 3;
    reference = find_reference();

    build_dialog();
    graf_mouse(M_OFF, 0);
    form_dial(FMD_START, 0, 0, 0, 0, 0, 0, scr_w, scr_h);
    v_hide_c(vh);
    {
        char hdr[96];

        sprintf(hdr, "Screen %dx%d, %d planes", scr_w, scr_h, planes);
        emit(hdr);
        sprintf(hdr, "Speed relative to %.60s", reference->name);
        emit(hdr);
    }
    for (i = 0; i < NTESTS; i++)
        if (selected(&tests[i], sel))
        {
            /* the dialog and window tests need the mouse hidden by VDI only */
            run_test(i);
        }
    show_report();
    v_show_c(vh, 0);
    save_report();
    if (!quit)
        evnt_keybd();
    form_dial(FMD_FINISH, 0, 0, 0, 0, 0, 0, scr_w, scr_h);
    graf_mouse(M_ON, 0);
    v_clsvwk(vh);
    appl_exit();
    return 0;
}
