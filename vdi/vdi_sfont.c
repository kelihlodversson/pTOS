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
 * missing-glyph shape.  For text in Unicode (vst_map_mode(), #577), a
 * size also has pages of 256 Unicode characters, each rendered as the
 * size itself is, with the same cell height and alignment lines, so that
 * the text path can draw a string in runs of one page each.  Each glyph
 * gets its cell of its advance width: ink that reaches past the cell (an
 * italic's overhang, a negative left bearing, a slant of vst_skew()) is
 * cut off, as it would be in a bitmap font.  Kerning is not applied.
 * A size can also have its own width (vst_setsize(), vst_width()).
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
#include <freetype/fttrigon.h>

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
    LONG size;                      /* em size, in 1/64 pixel */
    WORD want;                      /* the top asked for (vst_height()), or 0 */
    LONG width;                     /* em width, in 1/64 pixel; 0: as the size */
    WORD skew;                      /* slant, in 1/10 degree */
    WORD mirror;                    /* SF_MIRROR_X, SF_MIRROR_Y */
    WORD page;                      /* Unicode page, or SF_ATARI */
    LONG ask_size;                  /* the request it answers: em size, */
    WORD ask_want;                  /* top, */
    LONG ask_width;                 /* and width, when that was too big to have */
    WORD half;                      /* offset of the scratch buffer's second half */
    ULONG bytes;                    /* of the whole block */
    ULONG used;                     /* when last asked for */
    WORD *scratch;                  /* the scratch buffer, after the form */
    Fonthead head;
    UWORD off[SF_CHARS + 1];
    /* the form follows, then the scratch buffer */
} SF_SIZE;

#define SF_ATARI        (-1)        /* the page of the Atari character set */

static SF_SIZE *sf_sizes;
static ULONG sf_bytes;              /* in all the sizes kept */
static ULONG sf_clock;

/* the face last used, and its font: meaningful while sf_face is set
 * (nothing initialised here, so that the image has no DATA segment) */
static FT_Face sf_face;
static WORD sf_face_index;
static WORD sf_page;                /* the page being rendered */

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
    0x0078, 0x0079, 0x007a, 0x007b, 0x007c, 0x007d, 0x007e, 0x0394, /* the delta of the ROM fonts */
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


/*
 * BICS (vst_map_mode(0)): only its first 95 characters, ASCII's printable
 * ones, are known here
 */
LONG vdi_bics_to_unicode(UWORD c)
{
    return c < 95 ? 0x20 + c : -1;
}

/* the BICS character for a code point, or -1 if it has none known here */
WORD vdi_unicode_to_bics(LONG cp)
{
    return cp >= 0x20 && cp < 0x7f ? (WORD)(cp - 0x20) : -1;
}

/* the Atari character for a code point, or -1 if it has none */
WORD vdi_unicode_to_atari(LONG cp)
{
    WORD ch;

    if (cp >= 0x20 && cp < 0x7f)
        return (WORD)cp;
    if (cp <= 0 || cp > 0xffffL)
        return -1;
    for (ch = 1; ch < SF_CHARS; ch++)
        if (atari_unicode[ch] == (UWORD)cp)
            return ch;

    return -1;
}

/* the code point of an Atari character, 0 if it has none */
UWORD vdi_atari_to_unicode(UBYTE c)
{
    return atari_unicode[c];
}

WORD vdi_sfont_find(WORD font_id)
{
    WORD i;

    for (i = 0; i < vdi_sfont_count; i++)
        if (vdi_sfonts[i].id == font_id)
            return i;

    return -1;
}

/* em sizes are in 1/64 pixel, as FreeType's */
static LONG sf_clamp(LONG size)
{
    if (size < SF_MIN_PIXELS * 64L)
        return SF_MIN_PIXELS * 64L;
    if (size > SF_MAX_PIXELS * 64L)
        return SF_MAX_PIXELS * 64L;
    return size;
}

/* whether a size can get no smaller: its height, and its width if asked */
static BOOL sf_smallest(LONG size, LONG width)
{
    return size == SF_MIN_PIXELS * 64L && (!width || width == SF_MIN_PIXELS * 64L);
}

/*
 * a size made smaller, when it does not fit: a width asked for wider than
 * the height comes down alone first, the height and the top staying, so
 * that only widening a size (vst_setsize(), vst_width()) does not make it
 * lower; then both come down, and the top asked for is dropped
 */
static void sf_shrink(LONG *size, LONG *width, WORD *want)
{
    if (*width > *size)
    {
        *width = sf_clamp(*width * 3 / 4);
        if (*width < *size)
            *width = *size;
        return;
    }
    *size = sf_clamp(*size * 3 / 4);
    *width = *width ? sf_clamp(*width * 3 / 4) : 0;
    *want = 0;
}

/*
 * points are 1/72 inch, and the screen's pixel height is in microns:
 * points * 25400 / 72 / micron pixels, in 1/64 pixel and rounded, the
 * division in two steps so that nothing overflows a LONG
 */
LONG vdi_sfont_size_for_points(WORD points)
{
    LONG micron = linea_vars.DEV_TAB[4];
    LONG num, div;

    if (micron <= 0)
        micron = 353;                   /* 72 dpi */
    if (points < 1)
        points = 1;
    num = points * 25400L;
    div = micron * 72;

    return sf_clamp(num / div * 64 + ((num % div) * 64 + div / 2) / div);
}

/*
 * the cell's top (baseline to top line) is the font's ascender, less
 * one: where to start looking for the size that has that top (sf_set())
 */
LONG vdi_sfont_size_for_top(WORD index, WORD top)
{
    const VDI_SFONT *f = &vdi_sfonts[index];

    if (top < 1)
        top = 1;
    if (top > 2 * SF_MAX_PIXELS)
        top = 2 * SF_MAX_PIXELS;
    if (f->ascender <= 0 || f->upem == 0)
        return sf_clamp((top + 1) * 64L);

    return sf_clamp((top + 1) * 64L * f->upem / f->ascender);
}


/*
 * Rendering, on FreeType's stack
 */

typedef struct
{
    WORD index;                     /* the font */
    LONG size;                      /* the em size, made smaller for want */
    WORD want;                      /* the top asked for, or 0 */
    LONG width;                     /* the em width, 0: as the size */
    WORD skew;                      /* the slant, in 1/10 degree */
    WORD mirror;                    /* SF_MIRROR_X, SF_MIRROR_Y */
    WORD page;                      /* the Unicode page, or SF_ATARI */
    BOOL render;                    /* FALSE: only measure it */
    ULONG bytes;                    /* the block it takes */
    BOOL drawable;                  /* within what the text path addresses */
    SF_SIZE *result;                /* when rendered */
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

/* the glyph of a character of the page being rendered */
static FT_UInt sf_glyph(UWORD ch)
{
    if (sf_page != SF_ATARI)
        return FT_Get_Char_Index(sf_face, ((FT_ULong)sf_page << 8) | ch);

    return atari_unicode[ch] ? FT_Get_Char_Index(sf_face, atari_unicode[ch]) : 0;
}

/* the face of a font, opened unless it is the one already open */
static BOOL sf_open(WORD index)
{
    if (sf_face && sf_face_index == index)
        return TRUE;

    if (sf_face)
    {
        FT_Done_Face(sf_face);
        sf_face = NULL;
    }
    if (FT_New_Face(vdi_ft_library, vdi_sfont_path(index), vdi_sfont_face(index), &sf_face))
    {
        sf_face = NULL;
        return FALSE;
    }
    FT_Select_Charmap(sf_face, FT_ENCODING_UNICODE);
    sf_face_index = index;

    return TRUE;
}

/*
 * The glyphs come from the outlines, never from bitmaps embedded in the
 * font: those can be in grey levels, which FT_LOAD_TARGET_MONO does not
 * make monochrome
 */
#define SF_LOAD     (FT_LOAD_TARGET_MONO | FT_LOAD_NO_BITMAP)

/* the top of the cell at the face's current size: its ascender, less one */
static WORD sf_top(void)
{
    WORD top = (WORD)((sf_face->size->metrics.ascender + 63) >> 6) - 1;

    return top < 1 ? 1 : top;
}

/*
 * The face at the size.  For a top asked for, the biggest size, in steps
 * of a quarter pixel, whose own top is not taller: with the ascender
 * rounded up to whole pixels, and in some TrueType fonts the em too, no
 * size need have exactly that top, so the cell gets it (sf_layout()) and
 * whatever the glyphs leave of it is headroom.
 */
static BOOL sf_set(SF_BUILD *b)
{
    FT_Matrix m;
    FT_Vector delta;

    for (;;)
    {
        if (FT_Set_Char_Size(sf_face, b->width, b->size, 72, 72))
            return FALSE;
        if (!b->want || sf_top() <= b->want || b->size <= SF_MIN_PIXELS * 64L)
            break;
        b->size -= 16;
    }

    /*
     * Slanted (vst_skew()) about a quarter of the em above the baseline,
     * where the middle of most lower case letters is: what leaves the
     * cell (sf_build()) is then shared between its two sides.  A positive
     * angle leans the characters to the left, as NVDI has it.
     */
    if (b->skew)
    {
        m.xx = m.yy = 0x10000L;
        m.yx = 0;
        m.xy = FT_Tan(-(FT_Angle)b->skew * 0x10000L / 10);
        delta.x = -FT_MulFix(b->size / 4, m.xy);
        delta.y = 0;
        FT_Set_Transform(sf_face, &m, &delta);
    }
    else
        FT_Set_Transform(sf_face, NULL, NULL);

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

/*
 * the layout of the current face at a size (already set); FALSE when
 * FreeType ran out of memory on the way, which a retry with less of the
 * font memory in use may not
 */
static BOOL sf_layout(SF_LAYOUT *l, WORD pixels, WORD want)
{
    LONG x = 0;
    UWORD ch, adv;
    FT_Error err;

    l->max_cell = 0;
    l->mono = (vdi_sfonts[sf_face_index].flags & SFONT_MONOSPACE) != 0;
    for (ch = 0; ch < SF_CHARS; ch++)
    {
        l->off[ch] = (UWORD)(x <= SF_MAX_WIDTH ? x : SF_MAX_WIDTH);
        adv = 0;
        err = FT_Load_Glyph(sf_face, sf_glyph(ch), SF_LOAD);
        if (err == FT_Err_Out_Of_Memory)
            return FALSE;
        if (!err)                       /* a broken glyph: no width */
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

    l->top = sf_top();
    if (want > l->top)
        l->top = want;                  /* the rows above are headroom */
    l->bottom = (WORD)((-sf_face->size->metrics.descender + 63) >> 6);
    if (l->bottom < 1)
        l->bottom = 1;                  /* alignment to the descent divides by it */
    l->form_width = (UWORD)(((x + 15) / 16) * 2);
    l->form_height = l->top + l->bottom + 1;
    l->skew = (l->bottom + 1) / 2 + (l->top + 1) / 2;   /* left + right offset */
    l->thicken = 1 + pixels / 24;
    l->half = sf_scratch_half(l);
    l->bytes = sizeof(SF_SIZE) + (ULONG)l->form_width * l->form_height + 2UL * l->half;
    l->drawable = x <= SF_MAX_WIDTH && l->half <= SF_MAX_HALF;

    return TRUE;
}

/*
 * a glyph's height above the baseline, in pixels: as it is rendered and
 * placed (bitmap_top, the outline rounded up), not its bearing rounded,
 * which at a fractional size can be a pixel less
 */
static WORD sf_bearing_y(UWORD ch)
{
    /* an ASCII character, so the same in all the pages: they line up */
    if (FT_Load_Glyph(sf_face, FT_Get_Char_Index(sf_face, ch), FT_LOAD_RENDER | SF_LOAD))
        return 0;

    return (WORD)sf_face->glyph->bitmap_top;
}

/*
 * Mirrored (a negative size in vst_arbpt32() or vst_setsize32()): along
 * the x axis, the rows of the cells upside down; along the y axis, each
 * character's cell left to right.  The cells, and so the advances and the
 * alignment lines, stay where they are.
 */
static void sf_mirror(UBYTE *form, const SF_LAYOUT *l, WORD mirror)
{
    UWORD fw = l->form_width, fh = l->form_height, row, ch;
    UBYTE *a, *b, t;
    LONG x, left, right;
    UWORD i;

    if (mirror & SF_MIRROR_X)
        for (row = 0; row < fh / 2; row++)
        {
            a = form + (ULONG)row * fw;
            b = form + (ULONG)(fh - 1 - row) * fw;
            for (i = 0; i < fw; i++)
            {
                t = a[i];
                a[i] = b[i];
                b[i] = t;
            }
        }

    if (mirror & SF_MIRROR_Y)
        for (ch = 0; ch < SF_CHARS; ch++)
            for (row = 0; row < fh; row++)
            {
                a = form + (ULONG)row * fw;
                for (left = l->off[ch], right = (LONG)l->off[ch + 1] - 1; left < right;
                     left++, right--)
                {
                    BOOL lb = (a[left >> 3] & (0x80 >> (left & 7))) != 0;
                    BOOL rb = (a[right >> 3] & (0x80 >> (right & 7))) != 0;

                    x = left;
                    if (rb)
                        a[x >> 3] |= (UBYTE)(0x80 >> (x & 7));
                    else
                        a[x >> 3] &= (UBYTE)~(0x80 >> (x & 7));
                    x = right;
                    if (lb)
                        a[x >> 3] |= (UBYTE)(0x80 >> (x & 7));
                    else
                        a[x >> 3] &= (UBYTE)~(0x80 >> (x & 7));
                }
            }
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
    sf_page = b->page;
    if (!sf_set(b))
        return -1;
    if (!sf_layout(&l, (WORD)(b->size >> 6), b->want))
        return -1;
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
        FT_Error err;

        err = FT_Load_Glyph(sf_face, sf_glyph(ch), FT_LOAD_RENDER | SF_LOAD);
        if (err == FT_Err_Out_Of_Memory)
        {
            vdi_ft_free(s);             /* not a size with holes in it */
            return -1;
        }
        if (err)
            continue;                   /* a broken glyph: left blank */
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

    sf_mirror(form, &l, b->mirror);

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
    if (vdi_sfonts[b->index].flags & SFONT_MONOSPACE)
        h->flags |= F_SFONT_FIXED;
    h->off_table = s->off;
    h->dat_table = form;
    h->form_width = l.form_width;
    h->form_height = l.form_height;

    s->sfont = b->index;
    s->size = b->size;
    s->want = b->want;
    s->width = b->width;
    s->skew = b->skew;
    s->mirror = b->mirror;
    s->page = b->page;
    s->ask_size = b->size;
    s->ask_want = b->want;
    s->ask_width = b->width;
    s->half = (WORD)l.half;
    s->scratch = (WORD *)(form + (ULONG)l.form_width * l.form_height);
    s->bytes = l.bytes;
    b->result = s;

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

/*
 * a size's height in points, for vst_point() to return: from the screen's
 * pixel size now, which a change of resolution can have changed since the
 * size was rendered
 */
static const Fonthead *sf_head(SF_SIZE *s)
{
    LONG micron = linea_vars.DEV_TAB[4] > 0 ? linea_vars.DEV_TAB[4] : 353;

    s->head.point = (WORD)((s->size * 72L * micron + 64 * 12700L) / (64 * 25400L));
    s->used = ++sf_clock;

    return &s->head;
}

/* a size of a font already rendered: as asked for, or what that came to */
static SF_SIZE *sf_find(const SF_BUILD *b)
{
    SF_SIZE *s;

    for (s = sf_sizes; s; s = s->next)
        if (s->sfont == b->index && s->skew == b->skew && s->mirror == b->mirror
            && s->page == b->page
            && ((s->size == b->size && s->want == b->want && s->width == b->width)
                || (s->ask_size == b->size && s->ask_want == b->want
                    && s->ask_width == b->width)))
            return s;

    return NULL;
}

/* make room for a size of that many bytes: the unused ones go, oldest first */
static void sf_make_room(ULONG bytes)
{
    while (sf_bytes + bytes > CONF_VDI_FONT_CACHE * 1024UL && sf_evict())
        ;
}

/* keep a size just rendered */
static void sf_keep(SF_SIZE *s)
{
    s->next = sf_sizes;
    sf_sizes = s;
    sf_bytes += s->bytes;
}

/*
 * A scalable font at an em size (in 1/64 pixel), or with the top 'want'
 * when that is not 0 (vst_height()), an em width (0 for the size), a
 * slant and a mirroring: from the cache, or rendered
 */
const Fonthead *vdi_sfont_size(WORD index, LONG size, WORD want, LONG width, WORD skew,
                               WORD mirror)
{
    SF_BUILD b;
    SF_SIZE *s;
    ULONG limit = CONF_VDI_FONT_CACHE * 1024UL;
    LONG asked, asked_width, fit, fit_width;
    WORD asked_want, fit_want;
    BOOL emptied = FALSE;

    if (index < 0 || index >= vdi_sfont_count || !vdi_ft_library)
        return NULL;
    if (want < 0 || want > 2 * SF_MAX_PIXELS)
        want = 0;
    asked = size = sf_clamp(size);
    asked_want = want;
    asked_width = width = width ? sf_clamp(width) : 0;

    b.index = index;
    b.size = size;
    b.want = want;
    b.width = width;
    b.skew = skew;
    b.mirror = mirror;
    b.page = SF_ATARI;
    s = sf_find(&b);
    if (s)
        return sf_head(s);

    /*
     * The size, or the biggest one below it that fits by itself: within
     * the limit, and within what the text path can address.  Smaller than
     * asked for, it has its own top, not the one asked for.
     */
    b.render = FALSE;
    b.result = NULL;
    for (;;)
    {
        b.size = size;
        b.want = want;
        b.width = width;
        if (vdi_ft_call(sf_build, &b) != 0)
        {
            /* opening the face takes the whole file in font memory */
            if (emptied)
                goto fail;
            vdi_sfont_flush();
            emptied = TRUE;
            continue;
        }
        if ((b.bytes <= limit && b.drawable) || sf_smallest(size, width))
            break;
        sf_shrink(&size, &width, &want);
    }
    if (!b.drawable)
        goto fail;
    fit = size;
    fit_want = want;
    fit_width = width;
    if (fit != asked || fit_want != asked_want || fit_width != asked_width)
    {
        b.size = fit;
        b.want = fit_want;
        b.width = fit_width;
        s = sf_find(&b);
        if (s)
        {
            s->ask_size = asked;
            s->ask_want = asked_want;
            s->ask_width = asked_width;
            return sf_head(s);
        }
    }

    sf_make_room(b.bytes);

    /*
     * Rendered.  If the memory is not there after all (taken by sizes in
     * use, or by FreeType, or in pieces), the other sizes go, and then it
     * is rendered smaller, but only for now: it is not kept as what the
     * size asked for came to.
     */
    b.render = TRUE;
    for (;;)
    {
        b.size = size;
        b.want = want;
        b.width = width;
        b.result = NULL;
        if (vdi_ft_call(sf_build, &b) == 0 && b.result)
            break;
        if (!emptied)
        {
            vdi_sfont_flush();
            emptied = TRUE;
            continue;
        }
        if (sf_smallest(size, width))
            goto fail;
        sf_shrink(&size, &width, &want);
    }

    s = b.result;
    if (size == fit && want == fit_want && width == fit_width)
    {
        s->ask_size = asked;
        s->ask_want = asked_want;
        s->ask_width = asked_width;
    }
    sf_keep(s);
    KDEBUG(("sfont: %s at %ld/64 pixels, top %d (%ld/64, %d asked): %lu bytes, %lu in all\n",
            vdi_sfonts[index].name, (long)s->size, s->head.top, (long)asked, asked_want,
            (unsigned long)s->bytes, (unsigned long)sf_bytes));

    return sf_head(s);

fail:
    KINFO(("sfont: cannot render %s at %ld/64 pixels\n", vdi_sfonts[index].name, (long)asked));
    return NULL;
}

/* the size a Fonthead of the cache is */
static SF_SIZE *sf_of(const Fonthead *font)
{
    return (SF_SIZE *)((const UBYTE *)font - offsetof(SF_SIZE, head));
}

/*
 * A Unicode page of a size (vst_map_mode()), its 256 characters as the
 * size's, with the same cell height, top and alignment lines, so that
 * text in several pages lines up.  It is rendered at exactly the size,
 * however big: NULL if it does not fit (its glyphs can be much wider than
 * the Atari character set's, CJK ones say), or the memory is not there.
 * The size it is of must be a workstation's current one, so that it stays
 * while the page is rendered.
 */
const Fonthead *vdi_sfont_page(const Fonthead *font, UWORD page)
{
    SF_SIZE *base = sf_of(font), *s;
    SF_BUILD b;

    b.index = base->sfont;
    b.size = base->size;
    b.want = base->want;
    b.width = base->width;
    b.skew = base->skew;
    b.mirror = base->mirror;
    b.page = (WORD)page;
    b.result = NULL;
    s = sf_find(&b);
    if (s)
        return sf_head(s);

    b.render = FALSE;
    if (vdi_ft_call(sf_build, &b) != 0 || !b.drawable
        || b.bytes > CONF_VDI_FONT_CACHE * 1024UL)
        return NULL;
    sf_make_room(b.bytes);
    b.render = TRUE;
    if (vdi_ft_call(sf_build, &b) != 0 || !b.result)
    {
        vdi_sfont_flush();
        b.size = base->size;
        b.want = base->want;
        if (vdi_ft_call(sf_build, &b) != 0 || !b.result)
            return NULL;
    }
    sf_keep(b.result);

    return sf_head(b.result);
}

/* the em width a size has, in 1/64 pixel */
LONG vdi_sfont_em_width(const Fonthead *font)
{
    const SF_SIZE *s = sf_of(font);

    return s->width ? s->width : s->size;
}

/* the em size a size has, in 1/64 pixel */
LONG vdi_sfont_em(const Fonthead *font)
{
    return sf_of(font)->size;
}


/*
 * Back within the limit, once a workstation has selected another size or
 * font, or closed: what the sizes then in use kept over it can go
 */
void vdi_sfont_trim(void)
{
    while (sf_bytes > CONF_VDI_FONT_CACHE * 1024UL && sf_evict())
        ;
}

/*
 * whether a font has a character: a code point's glyph in it (0 if it has
 * none), on FreeType's stack
 */
typedef struct
{
    WORD index;
    LONG cp;
    UWORD glyph;
} SF_CHARMAP;

static long sf_char_glyph(void *arg)
{
    SF_CHARMAP *m = arg;
    FT_UInt glyph;

    if (!sf_open(m->index))
        return -1;
    glyph = FT_Get_Char_Index(sf_face, (FT_ULong)m->cp);
    m->glyph = glyph > 0xfffe ? 0 : (UWORD)glyph;

    return 0;
}

UWORD vdi_sfont_char_glyph(WORD index, LONG cp)
{
    SF_CHARMAP m;

    m.index = index;
    m.cp = cp;
    m.glyph = 0;
    if (cp < 0 || vdi_ft_call(sf_char_glyph, &m) != 0)
        return 0;

    return m.glyph;
}

/* kerning, on FreeType's stack */
typedef struct
{
    const SF_SIZE *s;
    LONG prev_cp;
    const LONG *cps;
    WORD n;
    LONG *kern;
} SF_KERN;

static long sf_kern(void *arg)
{
    SF_KERN *k = arg;
    SF_BUILD b;
    FT_UInt prev, glyph;
    FT_Vector v;
    WORD i;

    if (!sf_open(k->s->sfont))
        return -1;
    b.size = k->s->size;
    b.want = k->s->want;
    b.width = k->s->width;
    b.skew = k->s->skew;
    if (!sf_set(&b))
        return -1;

    prev = k->prev_cp > 0 ? FT_Get_Char_Index(sf_face, (FT_ULong)k->prev_cp) : 0;
    for (i = 0; i < k->n; i++)
    {
        glyph = k->cps[i] > 0 ? FT_Get_Char_Index(sf_face, (FT_ULong)k->cps[i]) : 0;
        k->kern[i] = 0;
        if (prev && glyph && !FT_Get_Kerning(sf_face, prev, glyph, FT_KERNING_UNFITTED, &v))
            k->kern[i] = v.x;
        prev = glyph;
    }

    return 0;
}

BOOL vdi_sfont_kerning(const Fonthead *font, LONG prev_cp, const LONG *cps, WORD n, LONG *kern)
{
    SF_KERN k;

    k.s = sf_of(font);
    if (!vdi_sfonts[k.s->sfont].kern_pairs)
        return FALSE;
    k.prev_cp = prev_cp;
    k.cps = cps;
    k.n = n;
    k.kern = kern;

    return vdi_ft_call(sf_kern, &k) == 0;
}

/* v_flushcache(): all the sizes no workstation has selected go */
void vdi_sfont_flush(void)
{
    while (sf_evict())
        ;
}

/* vqt_cachesize(): what is left of CONF_VDI_FONT_CACHE */
ULONG vdi_sfont_cache_free(void)
{
    ULONG limit = CONF_VDI_FONT_CACHE * 1024UL;

    return sf_bytes < limit ? limit - sf_bytes : 0;
}

/* the scratch buffer of a size, for the text path (vdi/vdi_text.c) */
void vdi_sfont_scratch(const Fonthead *font, WORD **buffer, WORD *half)
{
    const SF_SIZE *s = sf_of(font);

    *buffer = s->scratch;
    *half = s->half;
}


/*
 * A system font from a scalable one (#600)
 *
 * The forms of the system fonts of the ROM, which the console, the AES and
 * every program that picks no other font draws with, are replaced by those
 * of a monospaced scalable font, rendered once at the cell size of each:
 * the headers stay where they are (the font ring, the open workstations and
 * the console all point to them), and keep their metrics, so that nothing
 * that has measured them is out of date: only their offsets and forms are
 * new, in memory of their own that is never given back, and not part of
 * the sizes of sf_sizes that are evicted.  A character the face lacks keeps
 * the glyph the ROM font had.
 *
 * The cell is fixed: the face is made as big as the cell's rows above and
 * below the baseline allow, and as wide as its advance needs to be that of
 * the cell, with glyphs cut off at the edge of it.
 */
typedef struct
{
    WORD index;                     /* the scalable font */
    Fonthead *font;                 /* the system font header */
    UWORD *off;                     /* result: the offsets */
    UBYTE *form;                    /* and the form */
} SF_SYSTEM;

/* a bit of the ROM font's form, row 'row' of character 'ch', pixel 'x' of it */
static BOOL sf_rom_bit(const Fonthead *f, UWORD ch, UWORD row, UWORD x)
{
    ULONG bit = (ULONG)f->off_table[ch] + x;

    return (f->dat_table[(ULONG)row * f->form_width + (bit >> 3)] & (0x80 >> (bit & 7))) != 0;
}

static long sf_system_build(void *arg)
{
    SF_SYSTEM *sys = arg;
    Fonthead *f = sys->font;
    UWORD cell = f->max_cell_width, height = f->form_height;
    UWORD top = f->top, bottom = f->bottom;
    UWORD form_width = (UWORD)(((SF_CHARS * cell + 15) / 16) * 2);
    FT_Long em, em_asc, em_fit, wide;
    FT_UInt x_glyph;
    UWORD *off;
    UBYTE *form;
    UWORD ch, row, col;
    ULONG bytes;

    if (!sf_open(sys->index) || !cell || height != top + bottom + 1)
        return -1;
    sf_page = SF_ATARI;

    /*
     * The biggest size whose ascender is within the rows above the baseline
     * of the cell and, if that does not make it a quarter smaller, whose
     * descender is within those below it: the 8x8 cell has one row below
     * the baseline, which a face the size of the cell does not fit in, and
     * a face the size of its descender would be too small to read.  A
     * descender that does not fit is cut off at the cell.
     */
    em_asc = 0;
    em_fit = 0;
    for (em = (FT_Long)height * 64; em >= SF_MIN_PIXELS * 64L; em -= 16)
    {
        if (FT_Set_Char_Size(sf_face, em, em, 72, 72))
            return -1;
        if (((sf_face->size->metrics.ascender + 63) >> 6) > top + 1L)
            continue;
        if (!em_asc)
            em_asc = em;
        if (((-sf_face->size->metrics.descender + 63) >> 6) <= bottom)
        {
            em_fit = em;
            break;
        }
    }
    if (!em_asc)
        return -1;
    em = (em_fit && em_fit * 4 >= em_asc * 3) ? em_fit : em_asc;
    if (FT_Set_Char_Size(sf_face, em, em, 72, 72))
        return -1;

    /* wide enough for the advance to be the cell's */
    x_glyph = FT_Get_Char_Index(sf_face, 'x');
    for (wide = em / 4; wide <= em * 4; wide += 8)
    {
        if (FT_Set_Char_Size(sf_face, wide, em, 72, 72)
            || FT_Load_Glyph(sf_face, x_glyph, SF_LOAD))
            return -1;
        if (((sf_face->glyph->advance.x + 32) >> 6) >= cell)
            break;
    }

    bytes = 2UL * (SF_CHARS + 1) + (ULONG)form_width * height;
    off = vdi_ft_alloc(bytes);
    if (!off)
        return -1;
    form = (UBYTE *)(off + SF_CHARS + 1);
    bzero(form, (ULONG)form_width * height);
    for (ch = 0; ch <= SF_CHARS; ch++)
        off[ch] = (UWORD)(ch * cell);

    for (ch = 0; ch < SF_CHARS; ch++)
    {
        FT_UInt glyph = sf_glyph(ch);
        FT_Bitmap *bm;
        LONG left = (LONG)ch * cell;

        FT_Error err = glyph ? FT_Load_Glyph(sf_face, glyph, FT_LOAD_RENDER | SF_LOAD) : 1;

        if (err == FT_Err_Out_Of_Memory)
        {
            vdi_ft_free(off);           /* all or nothing */
            return -1;
        }
        if (!err && sf_face->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_MONO)
        {
            LONG x0 = left + sf_face->glyph->bitmap_left;
            WORD y0 = (WORD)(top + 1 - sf_face->glyph->bitmap_top);

            bm = &sf_face->glyph->bitmap;
            /* wholly below the baseline (the underscore) and off the cell: on its last rows */
            if (sf_face->glyph->bitmap_top <= 0 && y0 + (WORD)bm->rows > (WORD)height
                && (WORD)bm->rows <= (WORD)height)
                y0 = (WORD)(height - bm->rows);
            for (row = 0; row < bm->rows; row++)
            {
                const UBYTE *src = bm->buffer + row * bm->pitch;
                WORD y = (WORD)(y0 + row);

                if (y < 0 || y >= (WORD)height)
                    continue;
                for (col = 0; col < bm->width; col++)
                {
                    LONG x = x0 + col;

                    if (!(src[col >> 3] & (0x80 >> (col & 7))) || x < left || x >= left + cell)
                        continue;       /* clear, or outside its cell */
                    form[(ULONG)y * form_width + (x >> 3)] |= (UBYTE)(0x80 >> (x & 7));
                }
            }
        }
        else
        {
            /* the face has none: the ROM font's glyph, in a cell of the same size */
            for (row = 0; row < height; row++)
                for (col = 0; col < cell; col++)
                    if (sf_rom_bit(f, ch, row, col))
                    {
                        LONG x = left + col;

                        form[(ULONG)row * form_width + (x >> 3)] |= (UBYTE)(0x80 >> (x & 7));
                    }
        }
    }

    sys->off = off;
    sys->form = form;

    return 0;
}

/*
 * The system font 'font' (the RAM header of one of the ROM's sizes) from
 * the scalable font with that index in vdi_sfonts; FALSE if it cannot be
 * made, and then nothing is changed.
 */
BOOL vdi_sfont_make_system(WORD index, Fonthead *font)
{
    SF_SYSTEM sys;

    sys.index = index;
    sys.font = font;
    if (!vdi_ft_library || vdi_ft_call(sf_system_build, &sys) != 0)
        return FALSE;

    font->off_table = sys.off;
    font->form_width = (UWORD)(((SF_CHARS * font->max_cell_width + 15) / 16) * 2);
    font->dat_table = sys.form;

    return TRUE;
}
