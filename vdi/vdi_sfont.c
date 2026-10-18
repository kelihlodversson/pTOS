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
 * Each size also carries a text scratch buffer big enough for its
 * glyphs: rotation, outlining and skewing copy a glyph there, and the
 * workstation's default buffer is only as big as the 8x16 system font
 * needs.  (A GDOS loaded from disk hands vst_load_fonts() a buffer for
 * its fonts instead.)  The text path takes it from the font.
 *
 * Rendered sizes are kept, up to CONF_VDI_FONT_CACHE KB: the one used
 * longest ago that no workstation has selected goes first.  A size too
 * big for that by itself, or too big for the text path (its form wider
 * than a WORD can address, or its scratch buffer halves bigger than a
 * WORD offset), is rendered smaller until it fits.  The face last used
 * stays open, so that changing sizes does not read its file again.
 */

#include "emutos.h"
#include "kprint.h"
#include "string.h"
#include <stddef.h>
#include "vdi_defs.h"
#include "vdi_ftsystem.h"
#include "vdi_gdos.h"
#include "lineavars.h"

#include <ft2build.h>
#include <freetype/freetype.h>

#define SF_CHARS        256
#define SF_MIN_PIXELS   4
#define SF_MAX_PIXELS   400
#define SF_MAX_WIDTH    0x7ff0L     /* the text path's source x is a WORD */
#define SF_MAX_HALF     0x7ff0L     /* and so is the scratch buffer's SCRPT2 */

/* a rendered size: its Fonthead, offsets, form and scratch buffer, in one block */
typedef struct sf_size
{
    struct sf_size *next;
    WORD sfont;                     /* index in vdi_sfonts */
    WORD pixels;                    /* em size */
    WORD asked;                     /* em size asked for, when too big to have */
    WORD half;                      /* offset of the scratch buffer's second half */
    ULONG bytes;                    /* of the whole block */
    ULONG used;                     /* when last asked for */
    WORD *scratch;                  /* the scratch buffer, after the form */
    Fonthead head;
    UWORD off[SF_CHARS + 1];
    /* the form follows, then the scratch buffer */
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
    WORD pixels;                    /* the em size */
    BOOL render;                    /* FALSE: only measure it */
    ULONG bytes;                    /* the block it takes */
    BOOL drawable;                  /* within what the text path addresses */
    SF_SIZE *size;                  /* the result, when rendered */
} SF_BUILD;

/* what a size's layout comes to */
typedef struct
{
    UWORD off[SF_CHARS + 1];
    UWORD max_cell;
    BOOL mono;                      /* all advances the same */
    WORD top, bottom;
    UWORD form_width, form_height;
    WORD skew, thicken;
    LONG half;                      /* scratch buffer half, in bytes */
    ULONG bytes;
    BOOL drawable;
} SF_LAYOUT;

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

/*
 * The half of the scratch buffer that one glyph of the size needs (see
 * pre_blit() and rotate() in vdi/vdi_textblit.c): thickened, outlined and
 * skewed into the buffer, a row of words per pixel row, and that turned
 * by 90 degrees into the other half, a row of words per pixel column.
 */
static LONG sf_scratch_half(const SF_LAYOUT *l)
{
    LONG w = (LONG)l->max_cell + l->thicken + 3 + l->skew;  /* widest glyph */
    LONG h = (LONG)l->form_height + 3;                      /* tallest */
    LONG flat = (((w + 15) >> 4) * 2 + 2) * h;
    LONG turned = (((h + 15) >> 4) * 2 + 2) * (w + 1);
    LONG half = (flat > turned ? flat : turned) + 16;

    half = (half + 1) & ~1L;
    if (half < SCRATCHBUF_SIZE / 2)
        half = SCRATCHBUF_SIZE / 2;     /* as the default buffer: see vdi_raster.c */

    return half;
}

/* the layout of the current face at a size (already set) */
static void sf_layout(SF_LAYOUT *l, WORD pixels)
{
    LONG x = 0;
    UWORD ch, adv;

    l->max_cell = 0;
    l->mono = (vdi_sfonts[sf_face_index].flags & SFONT_MONOSPACE) != 0;
    for (ch = 0; ch < SF_CHARS; ch++)
    {
        l->off[ch] = (UWORD)(x <= SF_MAX_WIDTH ? x : SF_MAX_WIDTH);
        adv = 0;
        if (!FT_Load_Glyph(sf_face, sf_glyph(ch), FT_LOAD_TARGET_MONO))
            adv = (UWORD)((sf_face->glyph->advance.x + 32) >> 6);
        if (ch && adv != l->max_cell)
            l->mono = FALSE;
        if (adv > l->max_cell)
            l->max_cell = adv;
        x += adv;
    }
    l->off[SF_CHARS] = (UWORD)(x <= SF_MAX_WIDTH ? x : SF_MAX_WIDTH);
    if (!l->max_cell)
        l->mono = FALSE;

    l->top = (WORD)((sf_face->size->metrics.ascender + 63) >> 6) - 1;
    l->bottom = (WORD)((-sf_face->size->metrics.descender + 63) >> 6);
    if (l->top < 1)
        l->top = 1;
    if (l->bottom < 0)
        l->bottom = 0;
    l->form_width = (UWORD)(((x + 15) / 16) * 2);
    l->form_height = l->top + l->bottom + 1;
    l->skew = (l->bottom + 1) / 2 + (l->top + 1) / 2;   /* left + right offset */
    l->thicken = 1 + pixels / 24;
    l->half = sf_scratch_half(l);
    l->bytes = sizeof(SF_SIZE) + (ULONG)l->form_width * l->form_height + 2UL * l->half;
    l->drawable = x <= SF_MAX_WIDTH && l->half <= SF_MAX_HALF;
}

/* a glyph's height above the baseline, in pixels */
static WORD sf_bearing_y(UWORD ch)
{
    if (FT_Load_Glyph(sf_face, sf_glyph(ch), FT_LOAD_TARGET_MONO))
        return 0;

    return (WORD)((sf_face->glyph->metrics.horiBearingY + 32) >> 6);
}

/*
 * Measure a size, or render it: exactly that size, which the caller has
 * found fits (b->bytes and b->drawable say whether it does)
 */
static long sf_build(void *arg)
{
    SF_BUILD *b = arg;
    SF_LAYOUT l;
    UWORD ch;
    SF_SIZE *s;
    Fonthead *h;
    UBYTE *form;
    UWORD max_ink = 0;

    if (!sf_open(b->index))
        return -1;
    if (FT_Set_Pixel_Sizes(sf_face, 0, b->pixels))
        return -1;
    sf_layout(&l, b->pixels);
    b->bytes = l.bytes;
    b->drawable = l.drawable;
    if (!b->render)
        return 0;
    if (!l.drawable)
        return -1;

    s = vdi_ft_alloc(l.bytes);
    if (!s)
        return -1;
    form = (UBYTE *)(s + 1);
    bzero(form, (ULONG)l.form_width * l.form_height);
    memcpy(s->off, l.off, sizeof(l.off));

    /* the glyphs, each in its cell, the baseline on row 'top' */
    for (ch = 0; ch < SF_CHARS; ch++)
    {
        FT_Bitmap *bm;
        LONG x, x0, cell_left, cell_right;
        WORD row, col, y0;

        if (FT_Load_Glyph(sf_face, sf_glyph(ch), FT_LOAD_RENDER | FT_LOAD_TARGET_MONO))
            continue;
        bm = &sf_face->glyph->bitmap;
        if (bm->pixel_mode != FT_PIXEL_MODE_MONO)
            continue;
        if (bm->width > max_ink)
            max_ink = (UWORD)bm->width;
        cell_left = l.off[ch];
        cell_right = l.off[ch + 1];
        x0 = cell_left + sf_face->glyph->bitmap_left;
        y0 = l.top + 1 - sf_face->glyph->bitmap_top;
        for (row = 0; row < (WORD)bm->rows; row++)
        {
            const UBYTE *src = bm->buffer + row * bm->pitch;
            WORD y = y0 + row;
            UBYTE *dst;

            if (y < 0 || y >= (WORD)l.form_height)
                continue;
            dst = form + (ULONG)y * l.form_width;
            for (col = 0; col < (WORD)bm->width; col++)
            {
                if (!(src[col >> 3] & (0x80 >> (col & 7))))
                    continue;
                x = x0 + col;
                if (x < cell_left || x >= cell_right)
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
    h->top = l.top;
    /*
     * from the baseline row to the top row of 'H' and of 'x': a glyph
     * that rises n pixels above the baseline has its top row n - 1 rows
     * above it, as top - ascent is where alignment to the ascent puts
     * the top of the cell
     */
    h->ascent = sf_bearing_y('H') - 1;
    h->half = sf_bearing_y('x') - 1;
    if (h->ascent > l.top || h->ascent <= 0)
        h->ascent = l.top;
    if (h->half > l.top || h->half <= 0)
        h->half = l.top * 2 / 3;
    h->descent = l.bottom;
    h->bottom = l.bottom;
    h->max_char_width = max_ink ? max_ink : l.max_cell;
    h->max_cell_width = l.max_cell;
    h->left_offset = (l.bottom + 1) / 2;    /* how far skewing moves the rows */
    h->right_offset = (l.top + 1) / 2;
    h->thicken = l.thicken;
    h->ul_size = l.thicken;
    h->lighten = 0x5555;
    h->skew = 0x5555;
    /* fixed pitch: bold stays in the cell, as with the system fonts */
    h->flags = F_STDFORM | F_SFONT | (l.mono ? F_MONOSPACE : 0);
    h->off_table = s->off;
    h->dat_table = form;
    h->form_width = l.form_width;
    h->form_height = l.form_height;

    s->sfont = b->index;
    s->pixels = b->pixels;
    s->asked = b->pixels;
    s->half = (WORD)l.half;
    s->scratch = (WORD *)(form + (ULONG)l.form_width * l.form_height);
    s->bytes = l.bytes;
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

/* a size of a font already rendered: as asked for, or what that came to */
static SF_SIZE *sf_find(WORD index, WORD pixels)
{
    SF_SIZE *s;

    for (s = sf_sizes; s; s = s->next)
        if (s->sfont == index && (s->pixels == pixels || s->asked == pixels))
            return s;

    return NULL;
}

const Fonthead *vdi_sfont_size(WORD index, WORD pixels)
{
    SF_BUILD b;
    SF_SIZE *s;
    ULONG limit = CONF_VDI_FONT_CACHE * 1024UL;
    WORD asked, fit;
    BOOL emptied = FALSE;

    if (index < 0 || index >= vdi_sfont_count || !vdi_ft_library)
        return NULL;
    asked = pixels = sf_clamp(pixels);

    s = sf_find(index, pixels);
    if (s)
    {
        s->used = ++sf_clock;
        return &s->head;
    }

    /*
     * The size, or the biggest one below it that fits by itself: within
     * the limit, and within what the text path can address
     */
    b.index = index;
    b.render = FALSE;
    b.size = NULL;
    for (;;)
    {
        b.pixels = pixels;
        if (vdi_ft_call(sf_build, &b) != 0)
            goto fail;
        if ((b.bytes <= limit && b.drawable) || pixels == SF_MIN_PIXELS)
            break;
        pixels = sf_clamp(pixels * 3L / 4);
    }
    if (!b.drawable)
        goto fail;
    fit = pixels;
    if (fit != asked)
    {
        s = sf_find(index, fit);
        if (s)
        {
            s->asked = asked;
            s->used = ++sf_clock;
            return &s->head;
        }
    }

    /* room for it: the sizes no workstation has selected go, oldest first */
    while (sf_bytes + b.bytes > limit && sf_evict())
        ;

    /*
     * Rendered.  If the memory is not there after all (taken by sizes in
     * use, or by FreeType, or in pieces), the other sizes go, and then it
     * is rendered smaller, but only for now: it is not kept as what the
     * size asked for came to.
     */
    b.render = TRUE;
    for (;;)
    {
        b.pixels = pixels;
        b.size = NULL;
        if (vdi_ft_call(sf_build, &b) == 0 && b.size)
            break;
        if (!emptied)
        {
            while (sf_evict())
                ;
            emptied = TRUE;
            continue;
        }
        if (pixels == SF_MIN_PIXELS)
            goto fail;
        pixels = sf_clamp(pixels * 3L / 4);
    }

    s = b.size;
    s->asked = (pixels == fit) ? asked : pixels;
    /* its size in points, for vst_point() to return */
    s->head.point = (WORD)((pixels * 72L * (linea_vars.DEV_TAB[4] > 0 ? linea_vars.DEV_TAB[4] : 353)
                            + 12700) / 25400);
    s->used = ++sf_clock;
    s->next = sf_sizes;
    sf_sizes = s;
    sf_bytes += s->bytes;
    KDEBUG(("sfont: %s at %d pixels (%d asked): %lu bytes, %lu in all\n",
            vdi_sfonts[index].name, pixels, asked, s->bytes, sf_bytes));

    return &s->head;

fail:
    KINFO(("sfont: cannot render %s at %d pixels\n", vdi_sfonts[index].name, asked));
    return NULL;
}

/* the scratch buffer of a size, for the text path (vdi/vdi_text.c) */
void vdi_sfont_scratch(const Fonthead *font, WORD **buffer, WORD *half)
{
    const SF_SIZE *s = (const SF_SIZE *)((const UBYTE *)font - offsetof(SF_SIZE, head));

    *buffer = s->scratch;
    *half = s->half;
}
