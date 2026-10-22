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
 *  - with TEST_WITH_FONT_RESCAN (CONF_VDI_FONT_RESCAN), a font copied into the directory is added
 *    by the next vst_unload_fonts() and vst_load_fonts() (#648), and the
 *    fonts that were there keep their ids;
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
#define AREA_W  144
#define AREA_H  64
#define PLACE_MARGIN 6                  /* of the text from the edge of the area */

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

/* the same for a window of the area: what is set in it, as a hash and a count */
static void scan_in(struct ink *ink, short x0, short y0, short x1, short y1)
{
    short x, y;

    ink->hash = 2166136261UL;
    ink->count = 0;
    ink->x0 = ink->y0 = 32767;
    ink->x1 = ink->y1 = -1;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
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

/* the id of a font whose name has 'part' in it, or 0 */
static short find_named(const char *part)
{
    short n, count, i;

    count = system_fonts + loaded_fonts;
    for (n = 1; n <= count; n++)
    {
        char name[34];

        vs_intin[0] = n;
        vs_call(handle, 130, 0, 0, 1);      /* vqt_name() */
        for (i = 0; i < 32; i++)
            name[i] = (char)vs_intout[1 + i];
        name[32] = '\0';
        if (strstr(name, part))
            return vs_intout[0];
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
            CHECK(adv == s.cell_w, "vqt_advance() is not the cell's width at pt", adv, s.cell_w);
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
    unsigned long hash_true;                /* and on one of 16 or more: the light text is drawn by another path */
};

#define NO_CLIP 0
#define CLIPPED 1

static const struct draw_case cases[] = {
    { "plain 10pt",     "Hamburg", 13, 0,  1, 0,    NO_CLIP, 0xb0582166UL, 0xb0582166UL },
    { "plain 18pt",     "Wxyz{|",      24, 0,  1, 0,    NO_CLIP, 0x962d9200UL, 0x962d9200UL },
    { "descenders 14",  "gjpqy_",      18, 0,  1, 0,    NO_CLIP, 0x88ddab73UL, 0x88ddab73UL },
    { "bold",           "Bold",     16, 1,  1, 0,    NO_CLIP, 0x9b4228ffUL, 0x9b4228ffUL },
    { "light",          "Light",    16, 2,  1, 0,    NO_CLIP, 0x2bad3f31UL, 0x02290ddfUL },
    { "italic",         "Italic",   16, 4,  1, 0,    NO_CLIP, 0xb6f4cdefUL, 0xb6f4cdefUL },
    { "underline",      "Under",    16, 8,  1, 0,    NO_CLIP, 0xbc22e82cUL, 0xbc22e82cUL },
    { "outline",        "Outline",       24, 16, 1, 0,    NO_CLIP, 0x4b7642cfUL, 0x4b7642cfUL },
    { "shadow",         "Shadow",        18, 32, 1, 0,    NO_CLIP, 0x91a26df1UL, 0x91a26df1UL },
    { "bold italic",    "Both",          18, 5,  1, 0,    NO_CLIP, 0xf0601091UL, 0xf0601091UL },
    { "transparent",    "Transp",   16, 0,  2, 0,    NO_CLIP, 0x77e3359eUL, 0x77e3359eUL },
    { "xor",            "XOR",      16, 0,  3, 0,    NO_CLIP, 0x68b4b124UL, 0x68b4b124UL },
    { "reverse transp", "Reverse",       16, 0,  4, 0,    NO_CLIP, 0xa2be9178UL, 0xa2be9178UL },
    { "rotated 90",     "Up",            18, 0,  1, 900,  NO_CLIP, 0x8f86c30dUL, 0x8f86c30dUL },
    { "rotated 180",    "Upside",        18, 0,  1, 1800, NO_CLIP, 0xec5e54daUL, 0xec5e54daUL },
    { "rotated 270",    "Down",          18, 0,  1, 2700, NO_CLIP, 0x78a18864UL, 0x78a18864UL },
    { "clipped",        "Clipped",  18, 0,  1, 0,    CLIPPED, 0x18b3aeddUL, 0x18b3aeddUL },
};

#define N_CASES ((int)(sizeof cases / sizeof cases[0]))

/*
 * where the text is to be drawn for all of it, with its effects and turned,
 * to be inside the area (and so on the screen: with clipping off, what is
 * outside it is written to whatever memory is there).  It is measured
 * unturned, vqt_extent()'s corners being the same for every angle; turned
 * a quarter, text goes up from the origin (the cells to the right of it), a
 * half to the left and up, and three quarters to the left and down
 */
static void place(const char *text, short rotation, short *x, short *y)
{
    short n = 0, width, height;

    set_rotation(0);
    while (text[n])
    {
        vs_intin[n] = (unsigned char)text[n];
        n++;
    }
    vs_call(handle, 116, 0, 0, n);          /* vqt_extent() */
    width = vs_ptsout[2] - vs_ptsout[0];
    height = vs_ptsout[1] - vs_ptsout[7];
    if (height < 0)
        height = -height;
    if (rotation == 900 || rotation == 2700)
    {
        short t = width;

        width = height;
        height = t;
    }
    CHECK(width <= AREA_W - 2 * PLACE_MARGIN && height <= AREA_H - 2 * PLACE_MARGIN,
          "a drawing case does not fit its area:", width, height);
    switch (rotation)
    {
    case 900:
        *x = AREA_X + PLACE_MARGIN;
        *y = AREA_Y + PLACE_MARGIN + height;
        break;
    case 1800:
        *x = AREA_X + PLACE_MARGIN + width;
        *y = AREA_Y + PLACE_MARGIN + height;
        break;
    case 2700:
        *x = AREA_X + PLACE_MARGIN + width;
        *y = AREA_Y + PLACE_MARGIN;
        break;
    default:
        *x = AREA_X + PLACE_MARGIN;
        *y = AREA_Y + PLACE_MARGIN;
        break;
    }
    set_rotation(rotation);
}

static void draw_case(const struct draw_case *c, struct ink *ink)
{
    short x, y;

    clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
    if (c->mode >= 2)
    {
        /*
         * The modes that keep what is behind the text (transparent, XOR,
         * reverse transparent) draw over a pattern: on blank screen they
         * would give what replace mode gives, or the glyph the other way
         * round, and clearing the pixels they must keep would go unseen
         */
        vs_intin[0] = 2;
        vs_call(handle, 23, 0, 0, 1);       /* vsf_interior(pattern) */
        vs_intin[0] = 5;
        vs_call(handle, 24, 0, 0, 1);       /* vsf_style(5) */
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
    place(c->text, c->rotation, &x, &y);
    gtext(x, y, c->text);
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
    short x, y;

    /* (all of it on the screen, by place(): with clipping off, what is outside it is written to memory) */
    set_height(22);
    set_top_left();
    set_color(1);
    set_effects(0x1f);
    place("W@", 900, &x, &y);
    gtext(x, y, "W@");
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

/* --- ink that leaves its cell (#607) --- */

static void set_skew(short tenths)
{
    vs_intin[0] = tenths;
    vs_call(handle, 253, 0, 0, 1);          /* vst_skew() */
}

/* the text, in the writing mode, drawn at (x, y) with rotation 0 */
static void gtext_mode(short mode, short x, short y, const char *s)
{
    set_mode(mode);
    gtext(x, y, s);
}

/*
 * Slanted (vst_skew()), the top bar of a T goes past its advance, to the left
 * of its origin when the slant is positive (the characters lean to the left)
 * and to the right of its advance when it is negative, and the foot of a j
 * past the other.  That ink is the glyph's as much as the rest: it is there,
 * a character that follows does not erase it in replace mode, and turned it
 * is the same ink turned.
 */
static void check_overhang(void)
{
    static const short angles[] = { 0, 900, 1800, 2700 };
    struct ink a, b, turned[4];
    short x = AREA_X + 40, y = AREA_Y + 10, adv, i, planes;

    vs_intin[0] = 1;
    vs_call(handle, 102, 0, 0, 1);          /* vq_extnd(1): the planes */
    planes = vs_intout[4];

    set_height(30);
    set_top_left();
    set_color(1);
    set_effects(0);
    set_rotation(0);
    set_skew(0);
    adv = (short)string_extent("T");
    CHECK(adv > 8, "the advance of a T is", adv, 0);

    /* leaning to the left: ink to the left of the origin */
    set_skew(150);
    clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
    gtext_mode(1, x, y, "T");
    scan(&a);
    CHECK(a.count > 0, "a skewed T is not drawn:", 0, 0);
    CHECK(a.x0 < x, "the ink of a T leaning left is cut at its cell:", a.x0, x);

    /* leaning to the right: ink past the advance */
    set_skew(-150);
    clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
    gtext_mode(1, x, y, "T");
    scan(&a);
    CHECK(a.x1 >= x + adv, "the ink of a T leaning right is cut at its cell:", a.x1, x + adv);

    /*
     * two of them in replace mode: the second's cell, background and all,
     * must not cover the ink of the first that is over it; the same as
     * each in transparent mode
     */
    clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
    gtext_mode(1, x, y, "TT");
    scan(&a);
    clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
    gtext_mode(2, x, y, "T");
    gtext_mode(2, x + adv, y, "T");
    scan(&b);
    CHECK(a.hash == b.hash && a.count == b.count,
          "replace mode erases the ink of a character that leaves its cell:", a.count, b.count);

    /* turned: the same ink, in the same number of pixels, a quarter at a time */
    for (i = 0; i < 4; i++)
    {
        short tx, ty;

        clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
        place("T", angles[i], &tx, &ty);
        set_mode(1);
        gtext(tx, ty, "T");
        set_rotation(0);
        scan(&turned[i]);
        CHECK(turned[i].count > 0, "a turned skewed T is not drawn:", angles[i], 0);
    }
    for (i = 1; i < 4; i++)
    {
        short w0 = turned[0].x1 - turned[0].x0, h0 = turned[0].y1 - turned[0].y0;
        short w = turned[i].x1 - turned[i].x0, h = turned[i].y1 - turned[i].y0;

        if (i == 2)
            CHECK(w == w0 && h == h0, "a skewed T turned a half has another size:", i, 0);
        else
            CHECK(w == h0 && h == w0, "a skewed T turned a quarter has another size:", i, 0);
        if (planes <= 8)
            CHECK(turned[i].count == turned[0].count,
                  "a skewed T turned has another number of pixels:", i,
                  turned[i].count - turned[0].count);
    }
    set_skew(0);
    set_mode(1);

    /*
     * bold: thickening adds ink, to the glyph and to what leaves its cell
     * (in a fixed-pitch font the cell stays as it is, but that ink is not
     * cut off by it): the ink of the plain one is in the bold one's, on the
     * side that the slant puts it
     */
    for (i = 0; i < 8; i++)
    {
        static const short slants[] = { -150, 150, -70, 70, -40, 40, -25, 25 };
        struct ink plain, bold;

        set_skew(slants[i]);
        set_effects(0);
        clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
        gtext_mode(1, x, y, "T");
        scan(&plain);
        set_effects(1);
        clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
        gtext_mode(1, x, y, "T");
        scan(&bold);
        set_effects(0);
        CHECK(bold.count > plain.count, "bold has no more ink than plain:", bold.count, plain.count);
        if (slants[i] > 0)
            CHECK(bold.x0 <= plain.x0, "bold loses the ink of a T that leaves its cell on the left:",
                  bold.x0, plain.x0);
        else
            CHECK(bold.x1 >= plain.x1, "bold loses the ink of a T that leaves its cell on the right:",
                  bold.x1, plain.x1);
    }

    /*
     * a steep slant puts ink further out than a cell is wide: all of it is
     * drawn (the bar of the T is over 2 cells away from where it is plain)
     */
    set_skew(-700);
    clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
    gtext_mode(1, x, y, "T");
    scan(&a);
    CHECK(a.x1 > x + 2 * adv, "the ink of a steeply slanted T is cut short:", a.x1, x + 2 * adv);
    set_skew(0);
}

/*
 * Ink that leaves the cell is drawn where the cell is not: at the edge of
 * the screen a j (or the T of a slant to the left) has ink at a negative x,
 * which is not drawn (with clipping off, which is the default, it would
 * come back at the other end of the line above)
 */
static void check_overhang_edge(void)
{
    struct ink right_edge, left_edge;
    short xres, y = AREA_Y + 10;

    vs_intin[0] = 0;
    vs_call(handle, 102, 0, 0, 1);          /* vq_extnd(0): the width of the screen, less 1 */
    xres = vs_intout[0];
    set_height(30);
    set_top_left();
    set_color(1);
    set_effects(0);
    set_rotation(0);
    set_skew(150);                          /* leaning left: ink left of the origin */
    clear_area(xres - 15, y - 3, xres, y + 50);
    clear_area(0, y - 3, 40, y + 50);
    gtext_mode(1, 0, y, "T");
    scan_in(&right_edge, xres - 15, y - 3, xres, y + 50);
    scan_in(&left_edge, 0, y - 3, 40, y + 50);
    CHECK(left_edge.count > 0, "a T at the left edge is not drawn:", 0, 0);
    CHECK(right_edge.count == 0, "ink left of the screen comes back at the right:",
          right_edge.count, 0);
    clear_area(0, y - 3, 40, y + 50);
    set_skew(0);
}

/* XOR: bold text, whose ink goes outside its cells, is drawn, and twice it is gone */
static void check_overhang_xor(void)
{
    struct ink a;
    short x = AREA_X + 40, y = AREA_Y + 10, effects;
    static const short styles[] = { 1 };

    set_height(30);
    set_top_left();
    set_color(1);
    set_rotation(0);
    set_skew(-150);
    for (effects = 0; effects < 1; effects++)
    {
        set_effects(styles[effects]);
        clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
        gtext_mode(3, x, y, "T");
        scan(&a);
        CHECK(a.count > 0, "XOR text with an effect is not drawn:", styles[effects], 0);
        gtext_mode(3, x, y, "T");
        scan(&a);
        CHECK(a.count == 0, "XOR text with an effect drawn twice is not gone:", styles[effects],
              a.count);
    }
    set_effects(0);
    set_skew(0);
    set_mode(1);
}

/*
 * a size in points, 16.16, as vst_arbpt32() (opcode 246) or vst_setsize32()
 * (252) take it: a long as it is in memory, the high word first only on a
 * big endian machine
 */
static void set_size32(short opcode, long size)
{
    memcpy(&vs_intin[0], &size, sizeof size);
    vs_call(handle, opcode, 0, 0, 2);
}

/*
 * Mirrored (a negative size): the ink of a slanted T that leaves its cell
 * is mirrored with it, and nothing of it is lost: the same number of pixels
 * as the T that is not.  Mirrored along the x axis (the rows upside down) it
 * still goes out on the right of the advance; mirrored along y (each cell
 * left to right) on the left of the origin.  (vst_arbpt32() gives the width
 * of the size back to the default, vst_setsize32() sets it: the T that a
 * mirrored one is compared with is set up as it is.)
 */
static void check_overhang_mirror(void)
{
    struct ink norm, flipped;
    long size;
    struct size sz;
    short x = AREA_X + 40, y = AREA_Y + 10, adv;

    /* the size in points that is about 30 pixels: the points a pixel is varies with the screen */
    set_point(12);
    attributes(&sz);
    CHECK(sz.ch > 0, "no character height at 12 points:", sz.ch, 0);
    size = (12L * 30 / sz.ch) << 16;
    set_top_left();
    set_color(1);
    set_effects(0);
    set_rotation(0);
    set_skew(0);
    set_size32(246, size);
    adv = (short)string_extent("T");
    set_skew(-150);

    clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
    gtext_mode(1, x, y, "T");
    scan(&norm);
    CHECK(norm.count > 0, "a skewed T is not drawn:", 0, 0);
    set_size32(246, -(size));
    clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
    gtext_mode(1, x, y, "T");
    scan(&flipped);
    CHECK(flipped.count == norm.count, "a T mirrored along x has other pixels:",
          flipped.count, norm.count);
    CHECK(flipped.x1 >= x + adv, "the ink of a T mirrored along x is cut at its cell:",
          flipped.x1, x + adv);

    set_size32(246, size);
    set_size32(252, size);
    clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
    gtext_mode(1, x, y, "T");
    scan(&norm);
    set_size32(252, -(size));
    clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
    gtext_mode(1, x, y, "T");
    scan(&flipped);
    CHECK(flipped.count == norm.count, "a T mirrored along y has other pixels:",
          flipped.count, norm.count);
    CHECK(flipped.x0 < x, "the ink of a T mirrored along y is cut at its cell:", flipped.x0, x);

    set_size32(246, size);
    set_skew(0);
}

/*
 * An italic face (Inter Italic, when the test disk has it) has ink that
 * leaves its cells without any slant asked for: the hook of an f to the
 * right of it, the tail of a j to the left.  Nothing of either is lost, and
 * in replace mode a character that follows does not erase the hook.
 */
static void check_overhang_italic(void)
{
    struct ink a, b;
    short id = find_named("Italic");
    short x = AREA_X + 40, y = AREA_Y + 10, adv;

    if (!id)
        return;                             /* no italic face on this disk */
    vdi_font(id);
    set_height(30);
    set_top_left();
    set_color(1);
    set_effects(0);
    set_rotation(0);
    set_skew(0);

    adv = (short)string_extent("f");
    clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
    gtext_mode(1, x, y, "f");
    scan(&a);
    CHECK(a.x1 >= x + adv, "the hook of an italic f is cut at its cell:", a.x1, x + adv);

    clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
    gtext_mode(1, x, y, "j");
    scan(&a);
    CHECK(a.x0 < x, "the tail of an italic j is cut at its cell:", a.x0, x);

    clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
    gtext_mode(1, x, y, "ffj");
    scan(&a);
    clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
    gtext_mode(2, x, y, "f");
    gtext_mode(2, x + adv, y, "f");
    adv = (short)string_extent("ff");
    gtext_mode(2, x + adv, y, "j");
    scan(&b);
    CHECK(a.hash == b.hash && a.count == b.count,
          "replace mode erases the ink of an italic that leaves its cell:", a.count, b.count);


    /*
     * bold: the hook goes on being where it is, thickened to the right, and
     * not as many pixels further as thickening adds (about two here)
     */
    {
        struct ink plain, bold;

        clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
        gtext_mode(1, x, y, "f");
        scan(&plain);
        set_effects(1);
        clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
        gtext_mode(1, x, y, "f");
        scan(&bold);
        set_effects(0);
        CHECK(bold.x1 >= plain.x1 && bold.x1 <= plain.x1 + 3,
              "the hook of a bold italic f is not where thickening puts it:", bold.x1, plain.x1);
    }

    /*
     * clipped at the top, by any number of rows: what is in the window is
     * what is there when it is not clipped (an f has ink on both sides of
     * its cell, drawn one after the other from the same rows of the form)
     */
    {
        short cut;

        for (cut = 2; cut <= 12; cut += 2)
        {
            struct ink all, part;

            clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
            gtext_mode(1, x, y, "f");
            scan_in(&all, AREA_X, y + cut, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
            clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
            set_clip(1, AREA_X, y + cut, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
            gtext_mode(1, x, y, "f");
            set_clip(0, 0, 0, 0, 0);
            scan_in(&part, AREA_X, y + cut, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
            CHECK(part.hash == all.hash && part.count == all.count,
                  "an italic f clipped at the top is not what is left of the whole one:",
                  cut, part.count - all.count);
        }
    }

    /*
     * the face last used stays open (and its file in font memory): back to
     * the test font's, so that whatever is left of this is not another
     * test's baseline
     */
    set_mode(1);
    vdi_font(font_id);
    v_flushcache(handle);
    set_height(13);
    clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
    gtext_mode(1, x, y, "x");
    v_flushcache(handle);
}

/* --- memory --- */

static long font_memory(void)
{
    long size;

    vqt_cachesize(handle, 2, &size);        /* font memory in use: pTOS's */

    return size;
}

/*
 * Every vertical alignment draws the same pixels, moved to put the line it
 * stands for (the baseline, the half line, the ascent line, the bottom line,
 * the descent line, the top) at the point given: so the text drawn with
 * alignment v at the top's point moved by what the font says that line is
 * away from its top must be the text drawn with the top alignment (#638:
 * the baseline's came out in the bitmap system font)
 */


static void check_alignment_at(short points);

static void check_alignment(void)
{
    short n, i, saved = font_id, ids[32], count = 0;

    /* every scalable font that is there, not only the test font */
    for (n = system_fonts + 1; n <= system_fonts + loaded_fonts && count < 32; n++)
    {
        vs_intin[0] = n;
        vs_call(handle, 130, 0, 0, 1);      /* vqt_name() */
        ids[count++] = vs_intout[0];
    }
    for (i = 0; i < count; i++)
    {
        font_id = ids[i];
        check_alignment_at(24);
        check_alignment_at(-24);
    }
    font_id = saved;
    check_alignment_at(0);                  /* a size in pixels */
    check_alignment_at(24);                 /* and in points, as an application sets it */
    check_alignment_at(10);
    check_alignment_at(-24);                /* exactly 24 points, vst_arbpt(): as Type Shelf sets it */
}

static void check_alignment_at(short points)
{
    static const char text[] = "Aa";
    struct ink top_ink, ink;
    short x, y, v, top, ascent, half, descent, bottom;

    set_effects(0);
    set_mode(1);
    set_rotation(0);
    set_color(1);
    CHECK(vdi_font(font_id) == font_id, "alignment: the font is not selected", 0, 0);
    if (points < 0)
    {
        short cw, ch, cell_w, cell_h;

        vst_arbpt(handle, -points, &cw, &ch, &cell_w, &cell_h);
    }
    else if (points)
        set_point(points);
    else
        set_height(16);
    vs_call(handle, 131, 0, 0, 0);          /* vqt_fontinfo() */
    bottom = vs_ptsout[1];
    descent = vs_ptsout[3];
    half = vs_ptsout[5];
    ascent = vs_ptsout[7];
    top = vs_ptsout[9];

    place(text, 0, &x, &y);
    clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
    set_top_left();
    gtext(x, y, text);
    scan(&top_ink);
    CHECK(top_ink.count > 0, "alignment: nothing drawn at the top", 0, 0);

    for (v = 0; v <= 5; v++)
    {
        short delv = v == 0 ? top : v == 1 ? top - half : v == 2 ? top - ascent
                     : v == 3 ? top + bottom : v == 4 ? top + descent : 0;

        vs_intin[0] = 0;
        vs_intin[1] = v;
        vs_call(handle, 39, 0, 0, 2);       /* vst_alignment(left, v) */
        clear_area(AREA_X, AREA_Y, AREA_X + AREA_W - 1, AREA_Y + AREA_H - 1);
        gtext(x, y + delv, text);
        scan(&ink);
        CHECK(ink.count == top_ink.count && ink.hash == top_ink.hash,
              "alignment: not the text of the top alignment, vertical alignment", v, ink.count);
    }
    set_top_left();
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
    /* a counter that stayed at 0 or at one value would pass the checks below: it has to see the sizes */
    CHECK(before > 0, "vqt_cachesize(2) reports no font memory in use:", before, 0);
    CHECK(with > before, "drawing uses no font memory?", with, before);
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

/* --- fonts that appear in the directory while the system runs (#648) --- */

/*
 * tests/fonts/PTOSTES2.TTF is the test font under another name ("PTOS Test
 * Mon2"), on the test disk as PTOSTES2.TMP so that the scan at boot does not
 * list it.  The test renames it to .TTF, as an installer would copy a font in
 * (a rename: the guest does not write a new file, which QEMU's vvfat does not
 * always take back to the host directory).  It then stays, as the list's entry
 * does until the next boot, so that later runs can still render it; a second
 * run finds the font listed and skips this check.
 */
#define FONT_TMP    "\\GEMSYS\\FONTS\\PTOSTES2.TMP"
#define FONT_NEW    "\\GEMSYS\\FONTS\\PTOSTES2.TTF"
#define COPY_NAME   "PTOS Test Mon2"

static short font_by_name(const char *wanted, short count)
{
    short n, i;

    for (n = 1; n <= count; n++)
    {
        char name[34];

        vs_intin[0] = n;
        vs_call(handle, 130, 0, 0, 1);      /* vqt_name() */
        for (i = 0; i < 32; i++)
            name[i] = (char)vs_intout[1 + i];
        name[32] = '\0';
        if (!strcmp(name, wanted))
            return vs_intout[0];
    }

    return 0;
}

static short unload_load(void)
{
    vs_call(handle, 120, 0, 0, 0);          /* vst_unload_fonts() */
    vs_intin[0] = 0;
    vs_call(handle, 119, 0, 0, 1);          /* vst_load_fonts() */

    return vs_intout[0];
}

static void check_rescan(void)
{
    short n0, n1, n2, id_mono, id_copy, other, i;
    long rc, fd;

    n0 = unload_load();
    id_mono = font_by_name(FONT_NAME, system_fonts + n0);
    CHECK(id_mono != 0, "rescan: the test font is not there before:", n0, 0);
    if (font_by_name(COPY_NAME, system_fonts + n0) != 0)
        return;                             /* an earlier run in this boot did it */
    fd = Fopen(FONT_TMP, 0);
    if (fd < 0)
        return;                             /* not on this disk */
    Fclose((short)fd);

    /* a second workstation that loaded its fonts before: the new font is not its own */
    for (i = 0; i < 10; i++)
        vs_intin[i] = 1;
    vs_intin[10] = 2;
    vs_call(handle, 100, 0, 0, 11);         /* v_opnvwk() */
    other = vs_contrl[6];
    if (other > 0)
    {
        short main_handle = handle;

        handle = other;
        vs_intin[0] = 0;
        vs_call(handle, 119, 0, 0, 1);
        handle = main_handle;
    }

    rc = Frename(0, FONT_TMP, FONT_NEW);
    CHECK(rc == 0, "rescan: cannot rename the font in:", rc, 0);
    if (rc != 0)
    {
        if (other > 0)
            vs_call(other, 101, 0, 0, 0);   /* v_clsvwk() */
        return;
    }

    n1 = unload_load();
    id_copy = font_by_name(COPY_NAME, system_fonts + n1);
    CHECK(n1 == n0 + 1, "rescan: the font was not added to the list:", n1, n0 + 1);
    CHECK(id_copy != 0 && id_copy != id_mono, "rescan: the added font has no id of its own:", id_copy, id_mono);
    CHECK(font_by_name(FONT_NAME, system_fonts + n1) == id_mono, "rescan: the first font's id changed:",
          font_by_name(FONT_NAME, system_fonts + n1), id_mono);

    if (other > 0)
    {
        short main_handle = handle, selected;

        handle = other;
        CHECK(font_by_name(COPY_NAME, system_fonts + n1) == 0, "rescan: another workstation lists the added font:",
              font_by_name(COPY_NAME, system_fonts + n1), 0);
        selected = vdi_font(id_copy);
        CHECK(selected != id_copy, "rescan: another workstation selects the added font:", selected, id_copy);
        handle = main_handle;
        vs_call(other, 101, 0, 0, 0);       /* v_clsvwk() */
    }

    /* nothing new: the same list */
    n2 = unload_load();
    CHECK(n2 == n1, "rescan: a second reload changed the list:", n2, n1);
    CHECK(font_by_name(COPY_NAME, system_fonts + n2) == id_copy, "rescan: the added font's id changed:",
          font_by_name(COPY_NAME, system_fonts + n2), id_copy);
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
    (void)Cursconf(0, 0);                   /* no text cursor: it blinks among the pixels that are read */
    SECTION(check_selection);
    SECTION(check_sizes);
    SECTION(check_extent_drawn);
    SECTION(check_rotation);
    SECTION(check_alignment);
    SECTION(check_drawing);
    SECTION(check_overhang);
    SECTION(check_overhang_mirror);
    SECTION(check_overhang_edge);
    SECTION(check_overhang_xor);
    SECTION(check_memory);
    SECTION(check_unsupported);
    SECTION(check_overhang_italic);
#ifdef TEST_WITH_FONT_RESCAN
    SECTION(check_rescan);
#endif

    vs_intin[0] = 1;
    vs_call(handle, 122, 0, 0, 1);          /* v_show_c(reset) */
    (void)Cursconf(1, 0);
    vs_close_screen(handle);

    ptest_begin("scalable_fonts (#579)");
    ptest_assert_msg(!failure[0], failure);
    ptest_pass();
}

#endif
