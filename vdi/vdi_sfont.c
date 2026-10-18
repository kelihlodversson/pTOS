/*
 * vdi_sfont.c - sizes of the scalable fonts, as bitmap fonts
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * A scalable font (vdi/vdi_gdos.c) is drawn by the VDI's text path like
 * any other font (#576): when a size of it is selected, its glyphs are
 * rendered by FreeType into a form, with an offset table and the other
 * fields of a Fonthead, as a bitmap font of exactly that size would have
 * them.  Writing modes, colours, clipping, alignment, rotation and the
 * effects then work on it as they do on the system fonts, and bitmap
 * text loses nothing in speed.
 *
 * The 256 characters are those of the Atari character set, mapped to
 * Unicode for FreeType; a character the font does not have shows its
 * missing-glyph shape.  Each glyph gets its cell of its advance width:
 * ink that reaches past the cell (an italic's overhang, a negative left
 * bearing) is cut off, as it would be in a bitmap font.  Kerning is not
 * applied.
 *
 * Rendered sizes are kept, up to CONF_VDI_FONT_CACHE KB: the one used
 * longest ago that no workstation has selected goes first.  A size too
 * big for that by itself is rendered smaller until it fits.  The face
 * last used stays open, so that changing sizes does not read its file
 * again.
 */

#include "emutos.h"
#include "kprint.h"
#include "string.h"
#include "vdi_defs.h"
#include "vdi_ftsystem.h"
#include "vdi_gdos.h"
#include "lineavars.h"

#include <ft2build.h>
#include <freetype/freetype.h>

#define SF_CHARS        256
#define SF_MIN_PIXELS   4
#define SF_MAX_PIXELS   400
#define SF_MAX_WIDTH    0xfff0UL    /* the offsets are UWORDs */

/* a rendered size: its Fonthead, offsets and form, in one block */
typedef struct sf_size
{
    struct sf_size *next;
    WORD sfont;                     /* index in vdi_sfonts */
    WORD pixels;                    /* em size */
    ULONG bytes;                    /* of the whole block */
    ULONG used;                     /* when last asked for */
    Fonthead head;
    UWORD off[SF_CHARS + 1];
    /* the form follows */
} SF_SIZE;

static SF_SIZE *sf_sizes;
static ULONG sf_bytes;              /* in all the sizes kept */
static ULONG sf_clock;

/* the face last used */
static FT_Face sf_face;
static WORD sf_face_index = -1;

/* the Atari character set, in Unicode; 0 for the missing-glyph shape */
static const UWORD atari_unicode[SF_CHARS] =
{
    /* 0x00: control characters, the arrows */
    0x0000, 0x21e7, 0x21e9, 0x21e8, 0x21e6, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    /* 0x20: ASCII */
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f,
    0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
    0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    0x0078, 0x0079, 0x007a, 0x007b, 0x007c, 0x007d, 0x007e, 0x2302,
    /* 0x80 */
    0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7,
    0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
    0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9,
    0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x00df, 0x0192,
    /* 0xa0 */
    0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
    0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
    0x00e3, 0x00f5, 0x00d8, 0x00f8, 0x0153, 0x0152, 0x00c0, 0x00c3,
    0x00d5, 0x00a8, 0x00b4, 0x2020, 0x00b6, 0x00a9, 0x00ae, 0x2122,
    /* 0xc0: ij, Hebrew */
    0x0133, 0x0132, 0x05d0, 0x05d1, 0x05d2, 0x05d3, 0x05d4, 0x05d5,
    0x05d6, 0x05d7, 0x05d8, 0x05d9, 0x05db, 0x05dc, 0x05de, 0x05e0,
    0x05e1, 0x05e2, 0x05e4, 0x05e6, 0x05e7, 0x05e8, 0x05e9, 0x05ea,
    0x05df, 0x05da, 0x05dd, 0x05e3, 0x05e5, 0x00a7, 0x2227, 0x221e,
    /* 0xe0: Greek, mathematics */
    0x03b1, 0x03b2, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
    0x03a6, 0x0398, 0x03a9, 0x03b4, 0x222e, 0x03d5, 0x2208, 0x2229,
    0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248,
    0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x00b3, 0x00af
};


WORD vdi_sfont_find(WORD font_id)
{
    WORD i;

    for (i = 0; i < vdi_sfont_count; i++)
        if (vdi_sfonts[i].id == font_id)
            return i;

    return -1;
}

static WORD sf_clamp(LONG pixels)
{
    if (pixels < SF_MIN_PIXELS)
        return SF_MIN_PIXELS;
    if (pixels > SF_MAX_PIXELS)
        return SF_MAX_PIXELS;
    return (WORD)pixels;
}

/* points are 1/72 inch; the screen's pixel height is in microns */
WORD vdi_sfont_pixels_for_points(WORD points)
{
    LONG micron = linea_vars.DEV_TAB[4];

    if (micron <= 0)
        micron = 353;                   /* 72 dpi */

    return sf_clamp((points * 25400L / micron + 36) / 72);
}

/* the cell's top (baseline to top line) is the font's ascender, less one */
WORD vdi_sfont_pixels_for_top(WORD index, WORD top)
{
    const VDI_SFONT *f = &vdi_sfonts[index];

    if (f->ascender <= 0)
        return sf_clamp(top + 1);

    return sf_clamp(((LONG)top + 1) * f->upem / f->ascender);
}


/*
 * Rendering, on FreeType's stack
 */

typedef struct
{
    WORD index;                     /* the font */
    WORD pixels;                    /* asked for, then had */
    ULONG limit;                    /* the most the size may take */
    SF_SIZE *size;                  /* the result */
} SF_BUILD;

static FT_UInt sf_glyph(UWORD ch)
{
    return atari_unicode[ch] ? FT_Get_Char_Index(sf_face, atari_unicode[ch]) : 0;
}

/* the face of a font, opened unless it is the one already open */
static BOOL sf_open(WORD index)
{
    if (sf_face_index == index)
        return TRUE;

    if (sf_face)
    {
        FT_Done_Face(sf_face);
        sf_face = NULL;
        sf_face_index = -1;
    }
    if (FT_New_Face(vdi_ft_library, vdi_sfont_path(index), 0, &sf_face))
    {
        sf_face = NULL;
        return FALSE;
    }
    FT_Select_Charmap(sf_face, FT_ENCODING_UNICODE);
    sf_face_index = index;

    return TRUE;
}

/* the advances at a size, into off[]; returns the form's width in pixels */
static ULONG sf_layout(UWORD *off, UWORD *max_cell)
{
    ULONG x = 0;
    UWORD ch, adv;

    *max_cell = 0;
    for (ch = 0; ch < SF_CHARS; ch++)
    {
        off[ch] = (UWORD)x;
        adv = 0;
        if (!FT_Load_Glyph(sf_face, sf_glyph(ch), FT_LOAD_TARGET_MONO))
            adv = (UWORD)((sf_face->glyph->advance.x + 32) >> 6);
        if (adv > *max_cell)
            *max_cell = adv;
        x += adv;
        if (x > SF_MAX_WIDTH)
            x = SF_MAX_WIDTH;           /* only at absurd sizes; no overflow */
    }
    off[SF_CHARS] = (UWORD)x;

    return x;
}

/* a glyph's height above the baseline, in pixels */
static WORD sf_bearing_y(UWORD ch)
{
    if (FT_Load_Glyph(sf_face, sf_glyph(ch), FT_LOAD_TARGET_MONO))
        return 0;

    return (WORD)((sf_face->glyph->metrics.horiBearingY + 32) >> 6);
}

static long sf_build(void *arg)
{
    SF_BUILD *b = arg;
    UWORD off[SF_CHARS + 1];
    UWORD max_cell, form_width, form_height, ch;
    WORD top, bottom, pixels;
    ULONG width, bytes;
    SF_SIZE *s;
    Fonthead *h;
    UBYTE *form;
    UWORD max_ink = 0;

    if (!sf_open(b->index))
        return -1;

    /* the size, made smaller until it fits in the limit */
    for (pixels = b->pixels; ; pixels = sf_clamp(pixels * 3L / 4))
    {
        if (FT_Set_Pixel_Sizes(sf_face, 0, pixels))
            return -1;
        top = (WORD)((sf_face->size->metrics.ascender + 63) >> 6) - 1;
        bottom = (WORD)((-sf_face->size->metrics.descender + 63) >> 6);
        if (top < 1)
            top = 1;
        if (bottom < 0)
            bottom = 0;
        width = sf_layout(off, &max_cell);
        form_width = (UWORD)(((width + 15) / 16) * 2);
        form_height = top + bottom + 1;
        bytes = sizeof(SF_SIZE) + (ULONG)form_width * form_height;
        if (bytes <= b->limit || pixels == SF_MIN_PIXELS)
            break;
    }

    s = vdi_ft_alloc(bytes);
    if (!s)
        return -1;
    form = (UBYTE *)(s + 1);
    bzero(form, (ULONG)form_width * form_height);
    memcpy(s->off, off, sizeof(off));

    /* the glyphs, each in its cell, the baseline on row 'top' */
    for (ch = 0; ch < SF_CHARS; ch++)
    {
        FT_Bitmap *bm;
        WORD row, col, x0, y0, cell;

        if (FT_Load_Glyph(sf_face, sf_glyph(ch), FT_LOAD_RENDER | FT_LOAD_TARGET_MONO))
            continue;
        bm = &sf_face->glyph->bitmap;
        if (bm->pixel_mode != FT_PIXEL_MODE_MONO)
            continue;
        cell = off[ch + 1] - off[ch];
        if (bm->width > max_ink)
            max_ink = (UWORD)bm->width;
        x0 = off[ch] + sf_face->glyph->bitmap_left;
        y0 = top + 1 - sf_face->glyph->bitmap_top;
        for (row = 0; row < (WORD)bm->rows; row++)
        {
            const UBYTE *src = bm->buffer + row * bm->pitch;
            WORD y = y0 + row;
            UBYTE *dst;

            if (y < 0 || y >= (WORD)form_height)
                continue;
            dst = form + (ULONG)y * form_width;
            for (col = 0; col < (WORD)bm->width; col++)
            {
                WORD x = x0 + col;

                if (!(src[col >> 3] & (0x80 >> (col & 7))))
                    continue;
                if (x < (WORD)off[ch] || x >= (WORD)off[ch] + cell)
                    continue;           /* outside its cell */
                dst[x >> 3] |= (UBYTE)(0x80 >> (x & 7));
            }
        }
    }

    h = &s->head;
    bzero(h, sizeof(*h));
    h->font_id = vdi_sfonts[b->index].id;
    memcpy(h->name, vdi_sfonts[b->index].name, FONT_NAME_LEN);
    h->first_ade = 0;
    h->last_ade = SF_CHARS - 1;
    h->top = top;
    h->ascent = sf_bearing_y('H');
    h->half = sf_bearing_y('x');
    if (h->ascent > top || !h->ascent)
        h->ascent = top;
    if (h->half > top || !h->half)
        h->half = top * 2 / 3;
    h->descent = bottom;
    h->bottom = bottom;
    h->max_char_width = max_ink ? max_ink : max_cell;
    h->max_cell_width = max_cell;
    h->left_offset = (bottom + 1) / 2;  /* how far skewing moves the rows */
    h->right_offset = (top + 1) / 2;
    h->thicken = 1 + pixels / 24;
    h->ul_size = 1 + pixels / 24;
    h->lighten = 0x5555;
    h->skew = 0x5555;
    h->flags = F_STDFORM | F_SFONT;
    h->off_table = s->off;
    h->dat_table = form;
    h->form_width = form_width;
    h->form_height = form_height;

    s->sfont = b->index;
    s->pixels = pixels;
    s->bytes = bytes;
    b->pixels = pixels;
    b->size = s;

    return 0;
}


/*
 * The cache
 */

/* drop the size used longest ago that no workstation has selected */
static BOOL sf_evict(void)
{
    SF_SIZE **link, **oldest = NULL, *s;

    for (link = &sf_sizes; (s = *link) != NULL; link = &s->next)
        if (!vdi_font_in_use(&s->head) && (!oldest || s->used < (*oldest)->used))
            oldest = link;
    if (!oldest)
        return FALSE;

    s = *oldest;
    *oldest = s->next;
    sf_bytes -= s->bytes;
    vdi_ft_free(s);

    return TRUE;
}

const Fonthead *vdi_sfont_size(WORD index, WORD pixels)
{
    SF_BUILD b;
    SF_SIZE *s;
    ULONG limit = CONF_VDI_FONT_CACHE * 1024UL;

    if (index < 0 || index >= vdi_sfont_count || !vdi_ft_library)
        return NULL;
    pixels = sf_clamp(pixels);

    for (s = sf_sizes; s; s = s->next)
    {
        if (s->sfont == index && s->pixels == pixels)
        {
            s->used = ++sf_clock;
            return &s->head;
        }
    }

    /* make room for a size of up to the whole limit */
    while (sf_sizes && sf_bytes > limit / 2 && sf_evict())
        ;

    b.index = index;
    b.pixels = pixels;
    b.limit = (limit > sf_bytes) ? limit - sf_bytes : limit / 4;
    b.size = NULL;
    if (vdi_ft_call(sf_build, &b) != 0 || !b.size)
    {
        /* perhaps it fits once the rest is gone */
        while (sf_evict())
            ;
        b.limit = limit;
        if (vdi_ft_call(sf_build, &b) != 0 || !b.size)
        {
            KINFO(("sfont: cannot render %s at %d pixels\n",
                   vdi_sfonts[index].name, pixels));
            return NULL;
        }
    }

    s = b.size;
    /* its size in points, for vst_point() to return */
    s->head.point = (WORD)((b.pixels * 72L * (linea_vars.DEV_TAB[4] > 0 ? linea_vars.DEV_TAB[4] : 353)
                            + 12700) / 25400);
    s->pixels = pixels;                 /* found under the size asked for */
    s->used = ++sf_clock;
    s->next = sf_sizes;
    sf_sizes = s;
    sf_bytes += s->bytes;
    KDEBUG(("sfont: %s at %d pixels (%d had): %lu bytes, %lu in all\n",
            vdi_sfonts[index].name, pixels, b.pixels, s->bytes, sf_bytes));

    return &s->head;
}
