/*
 * scalable_fonts.c - regression test for the scalable fonts of the VDI (#579)
 *
 * With the built-in GDOS (CONF_WITH_VDI_SCALABLE_FONTS), run against a font
 * that is committed with the tests, so that nothing depends on the fonts a
 * host has: PTOS Test Mono (tests/fonts/PTOSTEST.TTF, a subset of DejaVu
 * Sans Mono, which tools/make-test-font.py makes).  It checks that
 *
 *  - the font is listed by vst_load_fonts() and vqt_name(), and selected
 *    by vst_font();
 *  - the sizes vst_point(), vst_height() and vst_arbpt() set give the same
 *    sizes as vqt_attributes() reports, and the measuring calls,
 *    vqt_fontinfo(), vqt_width(), vqt_advance(), vqt_extent() and
 *    vqt_f_extent(), agree with each other, and with the extent of what is
 *    drawn;
 *  - drawing is as known: a hash of the pixels of a few strings at a few
 *    sizes, in each writing mode, with each effect, rotated and clipped,
 *    as it was when the results were checked by eye.  The hashes are of
 *    the pixels v_get_pixel() reads, so that they are the same on every
 *    target with scalable fonts; FreeType's hinting and rasterizer are
 *    part of what they cover, so a new FreeType version may change them,
 *    on purpose (run with the hashes printed: see PRINT_HASHES);
 *  - loading, drawing and unloading fonts in a loop leaves no font memory
 *    behind (vqt_cachesize(2), pTOS's extension, is the font memory in
 *    use);
 *  - the calls of the NVDI/Speedo interface that have nothing to answer
 *    (v_getoutline() with no outline, v_savecache()) give the documented
 *    values.
 *
 * The blitter paths of the Atari are not part of this: the targets with
 * scalable fonts have no blitter text output, and text_blit checks it with
 * the bitmap fonts of the ROMs.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <string.h>
#include "include/vdi_session.h"

void test_scalable_fonts(void);

#if !defined(__mc68000__) && !defined(__arm__)

void test_scalable_fonts(void)
{
    ptest_begin("scalable_fonts (#579)");
    conws("SKIP (no VDI for programs here)\r\n");
}

#else

#define FONT_NAME   "PTOS Test Mono"

/* define to print the hashes of the drawing cases, to be pasted into the table */
/* #define PRINT_HASHES */

static short handle;
static short font_id;

/* the first thing that was wrong, and the case it was in */
static char failure[96];

static void fail_with(const char *msg, long a, long b)
{
    char *p = failure;
    int n;
    long v[2];

    if (failure[0])
        return;
    while (*msg && p < failure + sizeof failure - 24)
        *p++ = *msg++;
    v[0] = a;
    v[1] = b;
    for (n = 0; n < 2; n++)
    {
        char digits[12];
        int d = 0;
        long x = v[n];
        int neg = x < 0;

        if (neg)
            x = -x;
        *p++ = n ? '/' : ' ';
        if (neg)
            *p++ = '-';
        do
            digits[d++] = (char)('0' + x % 10);
        while ((x /= 10) != 0);
        while (d)
            *p++ = digits[--d];
    }
    *p = '\0';
}

#define CHECK(cond, msg, a, b) do { if (!(cond)) fail_with(msg, (long)(a), (long)(b)); } while (0)

/* --- VDI calls --- */

static short vdi_font(short id)
{
    vs_intin[0] = id;

    vs_call(handle, 21, 0, 0, 1);

    return vs_intout[0];
}

static void set_effects(short effects)
{
    vs_intin[0] = effects;
    vs_call(handle, 106, 0, 0, 1);
}

static void set_mode(short mode)
{
    vs_intin[0] = mode;
    vs_call(handle, 32, 0, 0, 1);
}

static void set_rotation(short tenths)
{
    vs_intin[0] = tenths;
    vs_call(handle, 13, 0, 0, 1);
}

static void set_clip(short on, short x0, short y0, short x1, short y1)
{
    vs_intin[0] = on;
    vs_ptsin[0] = x0;
    vs_ptsin[1] = y0;
    vs_ptsin[2] = x1;
    vs_ptsin[3] = y1;
    vs_call(handle, 129, 0, 2, 1);
}

static void set_color(short c)
{
    vs_intin[0] = c;
    vs_call(handle, 22, 0, 0, 1);
}

static void set_height(short px)
{
    vs_ptsin[0] = 0;
    vs_ptsin[1] = px;
    vs_call(handle, 12, 0, 1, 0);           /* vst_height() */
}

static short set_point(short pt)
{
    vs_intin[0] = pt;
    vs_call(handle, 107, 0, 0, 1);

    return vs_intout[0];
}

/* what vqt_attributes() says: the cell and the character sizes */
struct size { short cw, ch, cell_w, cell_h; };

static void attributes(struct size *s)
{
    vs_call(handle, 38, 0, 0, 0);
    s->cw = vs_ptsout[0];
    s->ch = vs_ptsout[1];
    s->cell_w = vs_ptsout[2];
    s->cell_h = vs_ptsout[3];
}

static void sizes_of_call(struct size *s)
{
    s->cw = vs_ptsout[0];
    s->ch = vs_ptsout[1];
    s->cell_w = vs_ptsout[2];
    s->cell_h = vs_ptsout[3];
}

static long string_extent(const char *s)
{
    short n = 0;

    while (s[n])
    {
        vs_intin[n] = (unsigned char)s[n];
        n++;
    }
    vs_call(handle, 116, 0, 0, n);          /* vqt_extent() */

    return vs_ptsout[2] - vs_ptsout[0];
}

/* the text, top left aligned at (x, y) */
static void gtext(short x, short y, const char *s)
{
    short n = 0;

    while (s[n])
    {
        vs_intin[n] = (unsigned char)s[n];
        n++;
    }
    vs_ptsin[0] = x;
    vs_ptsin[1] = y;
    vs_call(handle, 8, 0, 1, n);
}

static void set_top_left(void)
{
    vs_intin[0] = 0;
    vs_intin[1] = 5;
    vs_call(handle, 39, 0, 0, 2);           /* vst_alignment(left, top) */
}

static short get_pixel(short x, short y)
{
    vs_ptsin[0] = x;
    vs_ptsin[1] = y;
    vs_call(handle, 105, 0, 1, 0);

    return vs_intout[1];
}

static void clear_area(short x0, short y0, short x1, short y1)
{
    vs_intin[0] = 1;
    vs_call(handle, 23, 0, 0, 1);           /* vsf_interior(solid) */
    vs_intin[0] = 0;
    vs_call(handle, 25, 0, 0, 1);           /* vsf_color(0) */
    vs_ptsin[0] = x0;
    vs_ptsin[1] = y0;
    vs_ptsin[2] = x1;
    vs_ptsin[3] = y1;
    vs_call(handle, 114, 0, 2, 0);          /* vr_recfl() */
}

/* --- the screen as ink: the area, a hash of its pixels, and its bounds --- */

#define AREA_X  8
#define AREA_Y  8
#define AREA_W  104
#define AREA_H  56

struct ink
{
    unsigned long hash;
    long count;
    short x0, y0, x1, y1;                   /* the bounds of what is set, -1 if none */
};

static void scan(struct ink *ink)
{
    short x, y;

    ink->hash = 2166136261UL;
    ink->count = 0;
    ink->x0 = ink->y0 = 32767;
    ink->x1 = ink->y1 = -1;
    for (y = AREA_Y; y < AREA_Y + AREA_H; y++)
        for (x = AREA_X; x < AREA_X + AREA_W; x++)
        {
            int set = get_pixel(x, y) != 0;

            ink->hash = (ink->hash ^ (unsigned long)set) * 16777619UL;
            if (!set)
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

/* --- the font --- */

static short system_fonts, loaded_fonts;

static int find_font(void)
{
    short n, count, i;

    vs_intin[0] = 0;
    vs_call(handle, 119, 0, 0, 1);          /* vst_load_fonts() */
    if (vs_intout[0] > 0)
        loaded_fonts = vs_intout[0];        /* 0 when they were already */
    count = system_fonts + loaded_fonts;
    for (n = 1; n <= count; n++)
    {
        char name[34];

        vs_intin[0] = n;
        vs_call(handle, 130, 0, 0, 1);      /* vqt_name() */
        for (i = 0; i < 32; i++)
            name[i] = (char)vs_intout[1 + i];
        name[32] = '\0';
        if (!strcmp(name, FONT_NAME))
        {
            font_id = vs_intout[0];
            return 1;
        }
    }

    return 0;
}

static void check_selection(void)
{
    struct size s;

    CHECK(vdi_font(font_id) == font_id, "vst_font() gives another id:", vs_intout[0], font_id);
    vs_call(handle, 38, 0, 0, 0);
    CHECK(vs_intout[0] == font_id, "vqt_attributes() font:", vs_intout[0], font_id);
    set_point(12);
    attributes(&s);
    CHECK(s.cell_w > 0 && s.cell_h > 0, "no cell:", s.cell_w, s.cell_h);
}

/* the sizes the three setters give, and what the measuring calls say of them */
static void check_sizes(void)
{
    static const short points[] = { 6, 9, 10, 12, 18, 24, 36 };
    static const short heights[] = { 8, 13, 20, 32 };
    unsigned i;
    struct size s, a;

    for (i = 0; i < sizeof points / sizeof points[0]; i++)
    {
        short got = set_point(points[i]);

        sizes_of_call(&s);
        attributes(&a);
        CHECK(got == points[i], "vst_point() size:", got, points[i]);
        CHECK(s.cw == a.cw && s.ch == a.ch && s.cell_w == a.cell_w && s.cell_h == a.cell_h,
              "vst_point() and vqt_attributes() differ at pt", points[i], 0);
        CHECK(s.cell_h >= s.ch, "cell shorter than the character: cell/char", s.cell_h, s.ch);
        /* the character's width is its ink's, which may reach past the advance of the cell */
        CHECK(s.cw > 0 && s.cell_w > 0, "no width: character/cell", s.cw, s.cell_w);
        /* a monospaced font: every width is the cell's, and the extent is a multiple of it */
        vs_intin[0] = 'i';
        vs_call(handle, 117, 0, 0, 1);      /* vqt_width() */
        CHECK(vs_ptsout[0] == s.cell_w, "vqt_width('i') is not the cell at pt", vs_ptsout[0], s.cell_w);
        vs_intin[0] = 'W';
        vs_call(handle, 117, 0, 0, 1);
        CHECK(vs_ptsout[0] == s.cell_w, "vqt_width('W') is not the cell at pt", vs_ptsout[0], s.cell_w);
        CHECK(string_extent("Hamburg") == 7L * s.cell_w, "extent of 7 characters at pt",
              string_extent("Hamburg"), 7L * s.cell_w);
        {
            short adv, advy, rx, ry;

            vqt_advance(handle, 'a', &adv, &advy, &rx, &ry);
            CHECK(advy == 0, "vqt_advance() moves vertically at pt", advy, points[i]);
            CHECK(adv == s.cell_w * 16 || adv == s.cell_w,
                  "vqt_advance() is not the cell's at pt", adv, s.cell_w);
        }
        /* vqt_fontinfo(): the first and last character, and the distances are in order */
        vs_call(handle, 131, 0, 0, 0);
        CHECK(vs_intout[0] <= 32 && vs_intout[1] >= 126, "vqt_fontinfo() characters:",
              vs_intout[0], vs_intout[1]);
        CHECK(vs_ptsout[9] >= vs_ptsout[7], "vqt_fontinfo() top/ascent:", vs_ptsout[9], vs_ptsout[7]);
        CHECK(vs_ptsout[7] >= vs_ptsout[5], "vqt_fontinfo() ascent/half:", vs_ptsout[7], vs_ptsout[5]);
        CHECK(vs_ptsout[5] > 0, "vqt_fontinfo() half:", vs_ptsout[5], points[i]);
        CHECK(vs_ptsout[1] >= vs_ptsout[3], "vqt_fontinfo() bottom/descent:", vs_ptsout[1], vs_ptsout[3]);
        CHECK(vs_ptsout[3] >= 0, "vqt_fontinfo() descent:", vs_ptsout[3], points[i]);
        CHECK(vs_ptsout[0] == s.cell_w, "vqt_fontinfo() widest cell at pt", vs_ptsout[0], s.cell_w);
    }
    for (i = 0; i < sizeof heights / sizeof heights[0]; i++)
    {
        vs_ptsin[0] = 0;
        vs_ptsin[1] = heights[i];
        vs_call(handle, 12, 0, 1, 0);       /* vst_height() */
        sizes_of_call(&s);
        attributes(&a);
        CHECK(s.ch == heights[i], "vst_height() character height:", s.ch, heights[i]);
        CHECK(s.cw == a.cw && s.cell_h == a.cell_h, "vst_height() and vqt_attributes() differ at",
              heights[i], 0);
    }
    {
        short cw, ch, cell_w, cell_h, got;

        got = vst_arbpt(handle, 14, &cw, &ch, &cell_w, &cell_h);
        attributes(&a);
        CHECK(got == 14, "vst_arbpt() size:", got, 14);
        CHECK(a.cw == cw && a.ch == ch && a.cell_w == cell_w && a.cell_h == cell_h,
              "vst_arbpt() and vqt_attributes() differ", 0, 0);
    }
}

/* what is drawn is as wide as vqt_extent() says, and where it says */
static void check_extent_drawn(void)
{
    static const char text[] = "Hamburg";
    static const short sizes[] = { 8, 10, 12, 16, 24 };
    unsigned i;

    set_top_left();
    set_effects(0);
    set_mode(1);
    set_rotation(0);
    set_color(1);
    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
    {
        struct ink ink;
        struct size s;
        long extent;

        set_point(sizes[i]);
        attributes(&s);
        extent = string_extent(text);
        if (extent > AREA_W - 12)
            continue;                       /* the sizes depend on the screen: not one that is too big for the area */
        clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
        gtext(AREA_X + 4, AREA_Y + 4, text);
        scan(&ink);
        CHECK(ink.count > 0, "nothing drawn at pt", sizes[i], 0);
        CHECK(ink.x0 >= AREA_X + 4 && ink.x1 < AREA_X + 4 + extent,
              "ink outside its extent at pt", sizes[i], extent);
        /* 'H' starts the string and 'g' ends it: ink in the first and the last cell */
        CHECK(ink.x1 >= AREA_X + 4 + extent - s.cell_w, "the last character is blank at pt",
              sizes[i], 0);
        CHECK(ink.y0 >= AREA_Y + 4 && ink.y1 < AREA_Y + 4 + s.cell_h,
              "ink outside the cell's rows at pt", sizes[i], 0);
    }
}

/* --- drawing, against known results --- */

struct draw_case
{
    const char *name;
    const char *text;
    short height;                           /* in pixels: the size in points depends on the screen */
    short effects;
    short mode;
    short rotation;
    short clip;                             /* a clip rectangle, in the area */
    unsigned long hash;                     /* of the pixels on a screen of up to 8 planes, 0 when not yet known */
    unsigned long hash_true;                /* and on one of 16 or more: the effects are drawn by another path */
};

#define NO_CLIP 0
#define CLIPPED 1

static const struct draw_case cases[] = {
    { "plain 10pt",     "Hamburgefonts", 13, 0,  1, 0,    NO_CLIP, 0xe0b4722bUL, 0xe0b4722bUL },
    { "plain 18pt",     "Wxyz{|}~",      24, 0,  1, 0,    NO_CLIP, 0xde6f8448UL, 0xde6f8448UL },
    { "descenders 14",  "gjpqy_,;",      18, 0,  1, 0,    NO_CLIP, 0xb4c787b5UL, 0xb4c787b5UL },
    { "bold",           "Bold Text",     16, 1,  1, 0,    NO_CLIP, 0xfae3b872UL, 0xfae3b872UL },
    { "light",          "Light Text",    16, 2,  1, 0,    NO_CLIP, 0x1627d1a8UL, 0x17155a42UL },
    { "italic",         "Italic Text",   16, 4,  1, 0,    NO_CLIP, 0xb15bab29UL, 0x0cdc13fcUL },
    { "underline",      "Underlined",    16, 8,  1, 0,    NO_CLIP, 0x499e1fc9UL, 0x499e1fc9UL },
    { "outline",        "Outline",       24, 16, 1, 0,    NO_CLIP, 0x22795be7UL, 0x22795be7UL },
    { "shadow",         "Shadow",        18, 32, 1, 0,    NO_CLIP, 0xe1841991UL, 0xe1841991UL },
    { "bold italic",    "Both",          18, 5,  1, 0,    NO_CLIP, 0x2de16e19UL, 0xe977814fUL },
    { "transparent",    "Transparent",   16, 0,  2, 0,    NO_CLIP, 0x9b5e5cfcUL, 0x9b5e5cfcUL },
    { "xor",            "XOR text",      16, 0,  3, 0,    NO_CLIP, 0xc84963f5UL, 0xc84963f5UL },
    { "reverse transp", "Reverse",       16, 0,  4, 0,    NO_CLIP, 0xa8847e19UL, 0xa8847e19UL },
    { "rotated 90",     "Up",            18, 0,  1, 900,  NO_CLIP, 0xfed7e74dUL, 0xfed7e74dUL },
    { "rotated 180",    "Upside",        18, 0,  1, 1800, NO_CLIP, 0xd6f22d8cUL, 0xd6f22d8cUL },
    { "rotated 270",    "Down",          18, 0,  1, 2700, NO_CLIP, 0xa9e6e215UL, 0xa9e6e215UL },
    { "clipped",        "Clipped text",  18, 0,  1, 0,    CLIPPED, 0x2c94a5b1UL, 0x2c94a5b1UL },
};

#define N_CASES ((int)(sizeof cases / sizeof cases[0]))

static void draw_case(const struct draw_case *c, struct ink *ink)
{
    clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
    if (c->mode == 3)
    {
        /* XOR on black behind the text, so that it is seen to invert and not only to set */
        vs_intin[0] = 1;
        vs_call(handle, 25, 0, 0, 1);       /* vsf_color(1) */
        vs_ptsin[0] = AREA_X;
        vs_ptsin[1] = AREA_Y + 4;
        vs_ptsin[2] = AREA_X + AREA_W - 1;
        vs_ptsin[3] = AREA_Y + 40;
        vs_call(handle, 114, 0, 2, 0);
    }
    set_height(c->height);
    set_top_left();
    set_color(1);
    set_effects(c->effects);
    set_mode(c->mode);
    set_rotation(c->rotation);
    if (c->clip)
        set_clip(1, AREA_X + 14, AREA_Y + 10, AREA_X + 47, AREA_Y + 34);
    gtext(AREA_X + (c->rotation == 900 || c->rotation == 2700 ? 30 : 8),
          AREA_Y + (c->rotation ? 28 : 10), c->text);
    if (c->clip)
        set_clip(0, 0, 0, 0, 0);
    set_effects(0);
    set_mode(1);
    set_rotation(0);
    scan(ink);
}

/* only the four angles: the others are rounded to them, as on the ROM's fonts */
static void check_rotation(void)
{
    static const short asked[] = { 0, 449, 450, 1349, 1350, 1800, 2700, 3599, -900 };
    static const short given[] = { 0, 0, 900, 900, 1800, 1800, 2700, 0, 2700 };
    unsigned i;

    for (i = 0; i < sizeof asked / sizeof asked[0]; i++)
    {
        vs_intin[0] = asked[i];
        vs_call(handle, 13, 0, 0, 1);
        CHECK(vs_intout[0] % 3600 == given[i], "vst_rotation() gives another angle for", asked[i],
              vs_intout[0]);
    }
    set_rotation(0);
}

/*
 * text of another size, with every effect and turned, so that whatever text
 * output keeps between calls (its scratch buffers) holds something else
 * when the next string is drawn: the result must not depend on it
 */
static void dirty_buffers(void)
{
    /* (all of it on the screen: with clipping off, what is outside it is written to memory) */
    set_height(28);
    set_top_left();
    set_color(1);
    set_effects(0x1f);
    set_rotation(900);
    gtext(AREA_X + 60, AREA_Y + 50, "W@Q");
    set_rotation(0);
    set_effects(0);
}

/* the two things that need no reference: it is the same each time, and clipped it stays in the clip */
static void check_drawing(void)
{
    int i;
    short planes;

    vs_intin[0] = 1;
    vs_call(handle, 102, 0, 0, 1);          /* vq_extnd(1): the planes */
    planes = vs_intout[4];

    for (i = 0; i < N_CASES; i++)
    {
        struct ink a, b;
        const struct draw_case *c = &cases[i];

#ifdef PRINT_HASHES
        conws("case ");
        conws(c->name);
        conws("\r\n");
#endif
        draw_case(c, &a);
        dirty_buffers();
        draw_case(c, &b);
#ifdef PRINT_HASHES
        {
            char line[64];
            char *p = line;
            int d;

            *p++ = '0';
            *p++ = 'x';
            for (d = 28; d >= 0; d -= 4)
                *p++ = "0123456789abcdef"[(a.hash >> d) & 15];
            *p++ = 'U';
            *p++ = 'L';
            *p++ = ' ';
            *p = '\0';
            conws("    ");
            conws(c->name);
            conws(" ");
            conws(line);
            conws("\r\n");
        }
#endif
        CHECK(a.hash == b.hash, "drawing depends on what was drawn before:", i, 0);
        if (c->mode != 3)
            CHECK(a.count > 0, "nothing drawn:", i, 0);
        if (c->clip)
            CHECK(a.x0 >= AREA_X + 14 && a.x1 <= AREA_X + 47 && a.y0 >= AREA_Y + 10
                  && a.y1 <= AREA_Y + 34, "ink outside the clip:", i, 0);
        {
            unsigned long known = planes > 8 ? c->hash_true : c->hash;

            if (known)
                CHECK(a.hash == known, "drawing differs from the known:", i, a.hash & 0xffff);
        }
    }
}

/* --- memory --- */

static long font_memory(void)
{
    long size;

    vqt_cachesize(handle, 2, &size);        /* font memory in use: pTOS's */

    return size;
}

static void check_memory(void)
{
    long before, after, with, per_round[20];
    int round;
    struct ink ink;

    /* once, so that what is kept for good (the list, the first sizes) is there before the count */
    vs_intin[0] = 0;
    vs_call(handle, 119, 0, 0, 1);
    draw_case(&cases[0], &ink);
    vs_call(handle, 120, 0, 0, 0);          /* vst_unload_fonts() */
    vs_intin[0] = 0;
    vs_call(handle, 119, 0, 0, 1);
    find_font();
    vdi_font(font_id);
    draw_case(&cases[0], &ink);
    v_flushcache(handle);
    before = font_memory();

    for (round = 0; round < 20; round++)
    {
        short i;

        vs_intin[0] = 0;
        vs_call(handle, 119, 0, 0, 1);      /* vst_load_fonts() */
        find_font();
        vdi_font(font_id);
        for (i = 0; i < 6; i++)
            draw_case(&cases[i], &ink);
        if (round == 0)
            with = font_memory();
        vs_call(handle, 120, 0, 0, 0);      /* vst_unload_fonts() */
        v_flushcache(handle);
        per_round[round] = font_memory();
    }
    /* the font is selected again for what follows */
    vs_intin[0] = 0;
    vs_call(handle, 119, 0, 0, 1);
    find_font();
    vdi_font(font_id);
    after = font_memory();
    CHECK(with >= before, "drawing uses no font memory?", with, before);
    /* what the first rounds keep (lists, sizes) is not a leak; what every round adds is */
    CHECK(per_round[19] <= per_round[2], "font memory grows with every round: round 3/20",
          per_round[2], per_round[19]);
    CHECK(after <= per_round[19] + 1024, "font memory is left behind:", after, per_round[19]);
    (void)before;
}

/* --- the interface's calls with nothing to answer --- */

static void check_unsupported(void)
{
    short points = -1;
    short xy[8];
    char bez[8];

    /* v_getoutline() has no outline to give: no points; there is no cache file */
    v_getoutline(handle, 'a', xy, bez, 4, &points);
    CHECK(points == 0, "v_getoutline() points:", points, 0);
    CHECK(v_savecache(handle, "NOSUCH.CAC") == -1, "v_savecache() result:",
          v_savecache(handle, "NOSUCH.CAC"), -1);
    CHECK(v_loadcache(handle, "NOSUCH.CAC", 0) == -1, "v_loadcache() result:",
          v_loadcache(handle, "NOSUCH.CAC", 0), -1);
}

void test_scalable_fonts(void)
{
    if (vs_gdos() == -2)
    {
        /* no GDOS (the fixed-size images): nothing to test, and no VDI call to risk */
        ptest_begin("scalable_fonts (#579)");
        conws("SKIP (no GDOS)\r\n");
        return;
    }
    handle = vs_open_screen();
    if (handle <= 0)
    {
        ptest_begin("scalable_fonts (#579)");
        conws("SKIP (no workstation to open)\r\n");
        return;
    }
    vs_call(handle, 123, 0, 0, 0);          /* v_hide_c() */
    vs_intin[0] = 0;
    vs_call(handle, 102, 0, 0, 1);          /* vq_extnd(0): the fonts the ROM has */
    system_fonts = vs_intout[10];

    if (!find_font())
    {
        vs_call(handle, 122, 0, 0, 1);
        vs_close_screen(handle);
        /* the font is on the test disk (tests/fonts/PTOSTEST.TTF): a GDOS that lists no fonts is broken */
        ptest_begin("scalable_fonts (#579)");
        ptest_fail(FONT_NAME " is not listed by vst_load_fonts()");
        return;
    }
#ifdef PRINT_HASHES
#define SECTION(f) do { conws(#f "\r\n"); f(); } while (0)
#else
#define SECTION(f) f()
#endif
    SECTION(check_selection);
    SECTION(check_sizes);
    SECTION(check_extent_drawn);
    SECTION(check_rotation);
    SECTION(check_drawing);
    SECTION(check_memory);
    SECTION(check_unsupported);

    vs_intin[0] = 1;
    vs_call(handle, 122, 0, 0, 1);          /* v_show_c(reset) */
    vs_close_screen(handle);

    ptest_begin("scalable_fonts (#579)");
    ptest_assert_msg(!failure[0], failure);
    ptest_pass();
}

#endif
