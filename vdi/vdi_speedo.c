/*
 * vdi_speedo.c - the font calls of SpeedoGDOS and NVDI
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * SpeedoGDOS added VDI calls for its outline fonts (opcodes 229 to 255),
 * and NVDI, which took its interface over, some more (#577).  With the
 * built-in GDOS active (vdi/vdi_gdos.c), pTOS answers them for its
 * scalable fonts, and for the bitmap fonts as far as they apply:
 *
 * - vqt_ext_name() (in vqt_name(), vdi/vdi_text.c), vqt_xfntinfo(),
 *   vst_name() and vqt_name_and_id(), and vqt_fontheader() say what
 *   the fonts are and select one by its name.
 *
 * - vst_arbpt() and vst_setsize(), also in their 32-bit forms, vst_width()
 *   and vst_skew() size a scalable font as asked, exactly; vqt_advance()
 *   and v_getbitmap_info() give a character's advance, in whole pixels as
 *   the text path places the characters.  vqt_f_extent(),
 *   vqt_real_extent(), v_ftext() and v_ftext_offset() are in
 *   vdi/vdi_text.c, with the text path.
 *
 * - vst_map_mode() chooses the character map text is in: BICS (of which
 *   only the ASCII part is known), the Atari character set, Unicode's
 *   first plane, or, as mode 3, which is pTOS's, all of Unicode in UTF-16.
 *   vqt_char_index() converts between BICS, the Atari set and Unicode.
 *
 * - vst_kern() switches pair kerning on and off, and vst_track_offset()
 *   (its track mode 255) sets an offset added after each character; the
 *   text path applies both (vdi/vdi_text.c).  The kerning is that of a
 *   font's 'kern' table, which FreeType reads; it has no kerning tracks,
 *   and so no track mode but 255 changes anything.  vqt_pairkern() and
 *   vqt_trackkern() say what applies.
 *
 * - There is no cache of characters to save or load: the sizes of the
 *   fonts are kept as rendered (vdi/vdi_sfont.c), which v_flushcache()
 *   empties and vqt_cachesize() reports on, and v_savecache() and
 *   v_loadcache() fail.  vst_scratch() has no buffer to size, and
 *   v_killoutline() no outline to free.
 *
 * Pointers handed over in intin, or back in intout, take two WORDs, as
 * on the 68000: they are copied in and out with memcpy(), which does not
 * mind that a WORD array need not be aligned for a LONG.
 */

#include "emutos.h"
#include "string.h"
#include <stddef.h>
#include "vdi_defs.h"
#include "vdi_gdos.h"
#include "lineavars.h"

/* the pointer in intin[i] and intin[i + 1] */
static void *intin_ptr(WORD i)
{
    ULONG value;

    memcpy(&value, &INTIN[i], sizeof(value));

    return (void *)(uintptr_t)value;
}

/* a LONG, or a pointer, into intout[i] and intout[i + 1] */
static void intout_long(WORD i, ULONG value)
{
    memcpy(&INTOUT[i], &value, sizeof(value));
}

/* a string into a field of a structure: NUL-terminated if there is room */
static void put_str(char *dst, const char *src, WORD len)
{
    WORD i;

    for (i = 0; i < len && src[i]; i++)
        dst[i] = src[i];
    if (i < len)
        dst[i] = '\0';
}

/* a font's id, its font_format and its name: a bitmap font, or a scalable one */
static WORD font_id(const Fonthead *font, WORD sfont)
{
    return sfont >= 0 ? vdi_sfonts[sfont].id : font->font_id;
}

static WORD font_format(WORD sfont)
{
    return sfont >= 0 ? vdi_sfonts[sfont].format : SFONT_BITMAP;
}

static const char *font_name(const Fonthead *font, WORD sfont)
{
    return sfont >= 0 ? vdi_sfonts[sfont].name : font->name;
}

/* the place in the workstation's list of the font with that id, or 0 */
static WORD font_element(Vwk *vwk, WORD id, const Fonthead **font, WORD *sfont)
{
    WORD element;

    for (element = 1; vdi_font_at(vwk, element, font, sfont); element++)
        if (font_id(*font, *sfont) == id)
            return element;

    return 0;
}


/*
 * vqt_xfntinfo() - what a font is, into an XFNT_INFO
 *
 * intin[0] says which fields, intin[1] and intin[2] which font: its place
 * in the list (as vqt_name() counts) if not 0, else its id if not 0, else
 * the current font.  intout[0..2] are its format, id and place, all 0 if
 * there is no such font.
 */

typedef struct
{
    LONG size;                      /* of the structure, as the caller has it */
    WORD format;
    WORD id;
    WORD index;
    char font_name[50];
    char family_name[50];
    char style_name[50];
    char file_name1[200];
    char file_name2[200];
    char file_name3[200];
    WORD pt_cnt;
    WORD pt_sizes[64];
} XFNT_INFO;

#define XFNT_FONT_NAME      0x0001
#define XFNT_FAMILY_NAME    0x0002
#define XFNT_STYLE_NAME     0x0004
#define XFNT_FILE_NAME1     0x0008
#define XFNT_FILE_NAME2     0x0010
#define XFNT_FILE_NAME3     0x0020
#define XFNT_SIZES          0x0100
#define XFNT_SIZES2         0x0200  /* with those vst_point() doubles */

/* whether the caller's structure goes as far as the end of a field */
#define XFNT_HAS(info, field) \
    ((info)->size >= (LONG)(offsetof(XFNT_INFO, field) + sizeof((info)->field)))

/* the sizes a scalable font is offered in, as NVDI lists them */
static const WORD sfont_sizes[] = { 8, 9, 10, 11, 12, 14, 18, 24, 36, 48 };

/* the point sizes of a bitmap font, and with doubled, those vst_point() doubles */
static WORD bitmap_sizes(WORD id, WORD *sizes, WORD max, BOOL doubled)
{
    const Fonthead *font, **chain_ptr;
    WORD count = 0, i, j, n, point;

    for (chain_ptr = linea_vars.font_ring; (font = *chain_ptr++) != NULL; )
        for ( ; font; font = font->next_font)
            if (font->font_id == id && count < max)
                sizes[count++] = font->point;

    for (i = 0, n = count; doubled && i < n; i++)
    {
        point = sizes[i] * 2;
        for (j = 0; j < count && sizes[j] != point; j++)
            ;
        if (j == count && count < max)
            sizes[count++] = point;
    }

    /* in order, and once each: the chains need not be */
    for (i = 1; i < count; i++)
        for (j = i; j > 0 && sizes[j - 1] > sizes[j]; j--)
        {
            point = sizes[j];
            sizes[j] = sizes[j - 1];
            sizes[j - 1] = point;
        }
    for (i = n = 0; i < count; i++)
        if (!n || sizes[i] != sizes[n - 1])
            sizes[n++] = sizes[i];

    return n;
}

void vdi_vqt_xfntinfo(Vwk *vwk)
{
    WORD flags = INTIN[0], id = INTIN[1], element = INTIN[2];
    XFNT_INFO *info = intin_ptr(3);
    const Fonthead *font;
    WORD sfont, i;
    const char *name;

    if (element)
    {
        if (!vdi_font_at(vwk, element, &font, &sfont))
            element = 0;
    }
    else
        element = font_element(vwk, id ? id : vwk->cur_font->font_id, &font, &sfont);

    INTOUT[0] = INTOUT[1] = INTOUT[2] = 0;
    if (!element)
        return;
    INTOUT[0] = font_format(sfont);
    INTOUT[1] = font_id(font, sfont);
    INTOUT[2] = element;
    if (!info)
        return;

    if (XFNT_HAS(info, index))
    {
        info->format = INTOUT[0];
        info->id = INTOUT[1];
        info->index = element;
    }

    name = font_name(font, sfont);
    if ((flags & XFNT_FONT_NAME) && XFNT_HAS(info, font_name))
        put_str(info->font_name, name, sizeof(info->font_name));

    /* a scalable font's name is "Family Style", a bitmap font's just the family */
    if ((flags & XFNT_FAMILY_NAME) && XFNT_HAS(info, family_name))
    {
        WORD len = (sfont >= 0 && vdi_sfonts[sfont].style)
                   ? vdi_sfonts[sfont].style - 1 : FONT_NAME_LEN;

        for (i = 0; i < len && name[i]; i++)
            info->family_name[i] = name[i];
        info->family_name[i] = '\0';
    }
    if ((flags & XFNT_STYLE_NAME) && XFNT_HAS(info, style_name))
        put_str(info->style_name, (sfont >= 0 && vdi_sfonts[sfont].style)
                ? name + vdi_sfonts[sfont].style : "Regular", sizeof(info->style_name));

    /* a scalable font has one file, a bitmap font none that is known here */
    if ((flags & XFNT_FILE_NAME1) && XFNT_HAS(info, file_name1))
        put_str(info->file_name1, sfont >= 0 ? vdi_sfont_path(sfont) : "",
                sizeof(info->file_name1));
    if ((flags & XFNT_FILE_NAME2) && XFNT_HAS(info, file_name2))
        info->file_name2[0] = '\0';
    if ((flags & XFNT_FILE_NAME3) && XFNT_HAS(info, file_name3))
        info->file_name3[0] = '\0';

    if ((flags & (XFNT_SIZES | XFNT_SIZES2)) && XFNT_HAS(info, pt_sizes))
    {
        if (sfont >= 0)
        {
            info->pt_cnt = ARRAY_SIZE(sfont_sizes);
            memcpy(info->pt_sizes, sfont_sizes, sizeof(sfont_sizes));
        }
        else
            info->pt_cnt = bitmap_sizes(INTOUT[1], info->pt_sizes,
                                        ARRAY_SIZE(info->pt_sizes),
                                        (flags & XFNT_SIZES2) != 0);
    }
}


/*
 * vst_name() - select a font by its name, as vqt_name() gives it
 *
 * intin[0] is a mask of the font formats to consider (0: any), intin[1..]
 * the name; blanks in it, missing or extra, do not count.  intout[0] is the
 * id of the font then current, intout[1..] its name.  A name that is not
 * there selects the system font.  vqt_name_and_id() (subcode 100) only
 * looks the font up, and answers 0 for a name that is not there.
 */

#define NAME_SHOWN  (FONT_NAME_LEN - 1)     /* what vqt_name() gives of a name */

/* whether the name asked for (intin[1..n]) is that font's, blanks aside */
static BOOL name_matches(const char *name, const WORD *asked, WORD n)
{
    WORD i = 0, j = 0;

    for (;;)
    {
        while (i < NAME_SHOWN && name[i] == ' ')
            i++;
        while (j < n && asked[j] == ' ')
            j++;
        if (i == NAME_SHOWN || !name[i])
            break;                      /* the font's name, as shown, is done */
        if (j == n || !asked[j] || (UBYTE)name[i] != (UWORD)asked[j])
            return FALSE;
        i++;
        j++;
    }

    /*
     * all of it matched: the name asked for must end too, unless the font's
     * goes on past what vqt_name() shows, and it was asked for whole
     */
    return j == n || !asked[j] || i == NAME_SHOWN;
}

void vdi_vst_name(Vwk *vwk)
{
    WORD formats = INTIN[0], element, sfont, i, n;
    const Fonthead *font;
    const char *found_name;
    BOOL select = CONTRL->subcode != 100;

    n = CONTRL->nintin - 1;
    for (element = 1; vdi_font_at(vwk, element, &font, &sfont); element++)
    {
        if (formats && !(formats & font_format(sfont)))
            continue;
        if (name_matches(font_name(font, sfont), INTIN + 1, n))
            break;
    }

    if (sfont < 0 && !font && !select)
    {
        INTOUT[0] = 0;                  /* not there */
        CONTRL->nintout = 1;
        return;
    }
    if (select)
    {
        /* that font, or the system font; the answer is what vst_font() selected */
        WORD *old_intin = INTIN, id = (sfont < 0 && !font) ? 1 : font_id(font, sfont);

        INTIN = &id;
        vdi_vst_font(vwk);
        INTIN = old_intin;
        font = vwk->cur_font;
        sfont = -1;
    }

    INTOUT[0] = font_id(font, sfont);
    found_name = font_name(font, sfont);
    for (i = 0; i < NAME_SHOWN && found_name[i]; i++)
        INTOUT[i + 1] = (UBYTE)found_name[i];
    INTOUT[i + 1] = 0;
    CONTRL->nintout = i + 1;
}


/*
 * vqt_fontheader() - the current font's header, as a Speedo font file
 * has it (a virtual one, for what is not a Speedo font: as NVDI does)
 *
 * intin[0..1] point to the buffer, intout gets the path of the font's
 * file (none for a bitmap font).  The fields past fh_orupm are left
 * alone.
 */

typedef struct
{
    char fmver[8];                  /* the format: "D1.0\r\n" */
    ULONG fntsz;                    /* size of the font file */
    ULONG fbfsz;                    /* the buffer the font needs */
    UWORD cbfsz;                    /* the buffer a character needs */
    UWORD hedsz;                    /* size of this header */
    UWORD fntid;                    /* Bitstream's font id */
    UWORD sfvnr;                    /* the font's version */
    char fntnm[70];                 /* its name */
    char mdate[10];                 /* when it was made */
    char laynm[70];                 /* its layout's name */
    char cpyrt[78];                 /* its copyright */
    UWORD nchrl;                    /* characters in the layout */
    UWORD nchrf;                    /* characters in the font */
    UWORD fchrf;                    /* the first one */
    UWORD nktks;                    /* kerning tracks */
    UWORD nkprs;                    /* kerning pairs */
    UBYTE flags;                    /* bit 0: extended mode */
    UBYTE cflgs;                    /* bit 0: italic, bit 1: monospaced */
    UBYTE famcl;                    /* the family's class */
    UBYTE frmcl;                    /* weight, high nibble, and width, low */
    char sfntn[32];                 /* its PostScript name */
    char sfacn[16];                 /* its family, short */
    char fntfm[14];                 /* its style */
    UWORD itang;                    /* its slant, in 1/256 degree */
    UWORD orupm;                    /* design units per em */
} SPEEDO_HEADER;

void vdi_vqt_fontheader(Vwk *vwk)
{
    SPEEDO_HEADER *h = intin_ptr(0);
    const Fonthead *font = vwk->cur_font;
    const VDI_SFONT *f = NULL;
    const char *path = "";
    char *p;
    WORD i, index;

    CONTRL->nintout = 0;
    if (font->flags & F_SFONT)
    {
        index = vdi_sfont_find(font->font_id);
        if (index >= 0)
        {
            f = &vdi_sfonts[index];
            path = vdi_sfont_path(index);
        }
    }
    if (!h)
        return;

    bzero(h, sizeof(*h));
    memcpy(h->fmver, "D1.0\r\n", 6);
    h->hedsz = sizeof(*h);
    put_str(h->fntnm, f ? f->name : font->name, sizeof(h->fntnm));
    h->nchrf = f ? f->glyphs : font->last_ade - font->first_ade + 1;
    h->fchrf = f ? 0 : font->first_ade;
    h->nchrl = h->nchrf;
    h->nkprs = f ? f->kern_pairs : 0;
    if (f && (f->flags & SFONT_ITALIC))
        h->cflgs |= 0x01;
    if (f ? (f->flags & SFONT_MONOSPACE) : (font->flags & F_MONOSPACE))
        h->cflgs |= 0x02;
    h->frmcl = (f && (f->flags & SFONT_BOLD)) ? 0xa8 : 0x68;

    /* the family and the style, apart, and the two without blanks */
    if (f && f->style)
    {
        for (i = 0; i < f->style - 1 && i < (WORD)sizeof(h->sfacn); i++)
            h->sfacn[i] = f->name[i];
        put_str(h->fntfm, f->name + f->style, sizeof(h->fntfm));
    }
    else
    {
        put_str(h->sfacn, f ? f->name : font->name, sizeof(h->sfacn));
        put_str(h->fntfm, "Regular", sizeof(h->fntfm));
    }
    for (i = 0, p = h->sfntn; f && f->name[i] && p < h->sfntn + sizeof(h->sfntn); i++)
    {
        if (f->style && i == f->style - 1)
            *p++ = '-';
        else if (f->name[i] != ' ')
            *p++ = f->name[i];
    }
    h->orupm = f ? f->upem : font->top + font->bottom + 1;

    for (i = 0; path[i]; i++)
        INTOUT[i] = (UBYTE)path[i];
    CONTRL->nintout = i;
}


/*
 * v_getoutline() - a character's outline: none, as yet (intout[0] is the
 * number of points)
 */
void vdi_v_getoutline(Vwk *vwk)
{
    INTOUT[0] = 0;
}


/*
 * vst_error() - where to report errors: intin[0] is the mode (the alerts
 * of mode 1 are not shown: both modes report to the variable), intin[1..2]
 * the variable's address
 */
void vdi_vst_error(Vwk *vwk)
{
    vwk->error_var = intin_ptr(1);
    if (vwk->error_var)
        *vwk->error_var = 0;
}


/*
 * The cache: v_savecache() and v_loadcache() fail, there being no cache
 * file; v_flushcache() drops the sizes no workstation has selected;
 * vqt_cachesize() says how much of the cache is free, for both caches
 */
void vdi_v_savecache(Vwk *vwk)
{
    INTOUT[0] = -1;
}

void vdi_v_loadcache(Vwk *vwk)
{
    INTOUT[0] = -1;
}

void vdi_v_flushcache(Vwk *vwk)
{
    vdi_sfont_flush();
    INTOUT[0] = 0;
}

void vdi_vqt_cachesize(Vwk *vwk)
{
    intout_long(0, vdi_sfont_cache_free());
}


/*
 * vqt_get_table() - the table that maps the Atari character set, from
 * 32 on, to the Bitstream International Character Set: its ASCII part,
 * where the two agree, -1 for the others
 */
static const WORD bics_table[224] =
{
#define B8(n) n, n + 1, n + 2, n + 3, n + 4, n + 5, n + 6, n + 7
#define N8 -1, -1, -1, -1, -1, -1, -1, -1
    B8(0), B8(8), B8(16), B8(24), B8(32), B8(40), B8(48), B8(56),
    B8(64), B8(72), B8(80),
    88, 89, 90, 91, 92, 93, 94, -1,
    N8, N8, N8, N8, N8, N8, N8, N8, N8, N8, N8, N8, N8, N8, N8, N8
#undef B8
#undef N8
};

void vdi_vqt_get_table(Vwk *vwk)
{
    intout_long(0, (ULONG)(uintptr_t)bics_table);
}


/*
 * Sizes: vst_arbpt() and vst_setsize(), the 32-bit forms (two intin, the
 * size in points as 16.16 fixed point) as well, vst_width() and vst_skew()
 */

#define SKEW_MAX            800     /* the slant vst_skew() takes, in 1/10 degree */

/* report an error to the variable vst_error() named */
void vdi_sfont_error(Vwk *vwk, WORD error)
{
    if (vwk->error_var)
        *vwk->error_var = error;
}

/* the character and cell sizes of the current font, as vst_point() answers */
static void size_answer(Vwk *vwk)
{
    const Fonthead *font = vwk->cur_font;

    PTSOUT[0] = font->max_char_width;
    PTSOUT[1] = font->top;
    PTSOUT[2] = font->max_cell_width;
    PTSOUT[3] = font->top + font->bottom + 1;
    CONTRL->nptsout = 2;
    flip_y = 1;
}

/* the screen's pixel height in microns, as the sizes are computed for it */
static LONG micron(void)
{
    LONG m = linea_vars.DEV_TAB[4];

    if (m <= 0)
        return 353;                     /* 72 dpi */
    return m > 2000 ? 2000 : m;
}

/* points in 16.16 fixed point as an em size in 1/64 pixel, and back */
#define POINTS_MAX  (3000L << 16)    /* far past the biggest size, and no overflow */

static LONG fix_to_em(LONG points)
{
    LONG div = micron() * 72, q, r;

    if (points < 0x10000L)
        points = 0x10000L;
    if (points > POINTS_MAX)
        points = POINTS_MAX;
    q = points / div;
    r = points % div;

    /* points * 25400 / div in 16.16, then in 1/64 */
    return (q * 25400 + r * 25400 / div + 512) >> 10;
}

/*
 * a size asked for in 16.16 points, within POINTS_MAX either way: before
 * its sign is taken off, which -0x80000000 would not survive
 */
static LONG clamp_points(LONG points)
{
    if (points > POINTS_MAX)
        return POINTS_MAX;
    if (points < -POINTS_MAX)
        return -POINTS_MAX;
    return points;
}

static LONG em_to_fix(LONG em)
{
    return (LONG)(((ULONG)em * 72 * (ULONG)micron() + 12700) / 25400) << 10;
}

/*
 * a size of a scalable font for a workstation, with its width, slant and
 * mirroring, NULL if it cannot be had; what it was asked for is kept with
 * the workstation, for when one of those changes (the size cache's record
 * of what a size was asked for is shared by the workstations)
 */
const Fonthead *vdi_sfont_select(Vwk *vwk, WORD index, LONG size, WORD want)
{
    const Fonthead *f;

    f = vdi_sfont_size(index, size, want, vwk->sf_width, vwk->sf_skew, vwk->sf_mirror);
    if (f)
    {
        vwk->sf_size = size;
        vwk->sf_want = want;
    }

    return f;
}

/* the current scalable font again, at the size it was asked for, as now set */
static void sfont_resize(Vwk *vwk, LONG size, WORD want)
{
    WORD index = vdi_sfont_find(vwk->cur_font->font_id);
    const Fonthead *f = NULL;

    if (index >= 0)
        f = vdi_sfont_select(vwk, index, size, want);
    if (f)
        vwk->cur_font = f;
    else
        vdi_sfont_error(vwk, SPEEDO_CACHE_FULL);
    vdi_sfont_trim();                   /* the size it had may go now */
    vwk->scaled = FALSE;
}

/* the size the current scalable font was asked for */
static void sfont_request(Vwk *vwk, LONG *size, WORD *want)
{
    *size = vwk->sf_size;
    *want = vwk->sf_want;
}

/* a bitmap font's size by vst_point(), from a size in points */
static void bitmap_point(Vwk *vwk, WORD points)
{
    WORD *old_intin = INTIN;

    INTIN = &points;
    vdi_vst_point(vwk);
    INTIN = old_intin;
}

void vdi_vst_arbpt(Vwk *vwk)
{
    BOOL fix = CONTRL->nintin >= 2;
    LONG points = 0;

    if (fix)
        memcpy(&points, &INTIN[0], sizeof(points));
    else
        points = (LONG)INTIN[0] << 16;
    points = clamp_points(points);

    if (vwk->cur_font->flags & F_SFONT)
    {
        /* a negative height mirrors the characters along the x axis; the width follows */
        BOOL mirrored = points < 0;

        vwk->pts_mode = TRUE;
        vwk->sf_width = 0;
        vwk->sf_mirror = mirrored ? SF_MIRROR_X : 0;
        sfont_resize(vwk, fix_to_em(mirrored ? -points : points), 0);
        points = em_to_fix(vdi_sfont_em(vwk->cur_font));
        if (mirrored)
            points = -points;
    }
    else
    {
        if (points < 0)
            points = -points;           /* a bitmap font is not mirrored */
        bitmap_point(vwk, (WORD)((points + 0x8000L) >> 16));
        points = (LONG)vwk->cur_font->point << 16;
    }

    size_answer(vwk);
    if (fix)
    {
        intout_long(0, points);
        CONTRL->nintout = 2;
    }
    else
    {
        INTOUT[0] = (WORD)((points + 0x8000L) >> 16);
        CONTRL->nintout = 1;
    }
}

/* vst_setsize(): the width apart from the height; a bitmap font has none */
void vdi_vst_setsize(Vwk *vwk)
{
    BOOL fix = CONTRL->nintin >= 2;
    LONG points = 0, size;
    WORD want;

    if (fix)
        memcpy(&points, &INTIN[0], sizeof(points));
    else
        points = (LONG)INTIN[0] << 16;
    points = clamp_points(points);

    if (vwk->cur_font->flags & F_SFONT)
    {
        /* a negative width mirrors the characters along the y axis */
        BOOL mirrored = points < 0;

        sfont_request(vwk, &size, &want);
        vwk->sf_width = fix_to_em(mirrored ? -points : points);
        vwk->sf_mirror = (vwk->sf_mirror & ~SF_MIRROR_Y) | (mirrored ? SF_MIRROR_Y : 0);
        sfont_resize(vwk, size, want);
        points = em_to_fix(vdi_sfont_em_width(vwk->cur_font));  /* what it came to */
        if (mirrored)
            points = -points;
    }
    else
        points = (LONG)vwk->cur_font->point << 16;

    size_answer(vwk);
    if (fix)
    {
        intout_long(0, points);
        CONTRL->nintout = 2;
    }
    else
    {
        INTOUT[0] = (WORD)((points + 0x8000L) >> 16);
        CONTRL->nintout = 1;
    }
}

/* vst_width(): the width in pixels (ptsin[0]) */
void vdi_vst_width(Vwk *vwk)
{
    LONG size;
    WORD want, width = PTSIN[0];

    if ((vwk->cur_font->flags & F_SFONT) && width > 0)
    {
        sfont_request(vwk, &size, &want);
        vwk->sf_width = (LONG)width * 64;
        sfont_resize(vwk, size, want);
    }
    size_answer(vwk);
}

/* vst_skew(): the slant, in 1/10 degree, to the left when positive (as NVDI) */
void vdi_vst_skew(Vwk *vwk)
{
    WORD skew = INTIN[0];
    LONG size;
    WORD want;

    if (skew > SKEW_MAX)
        skew = SKEW_MAX;
    if (skew < -SKEW_MAX)
        skew = -SKEW_MAX;
    vwk->sf_skew = skew;
    if (vwk->cur_font->flags & F_SFONT)
    {
        sfont_request(vwk, &size, &want);
        sfont_resize(vwk, size, want);
    }
    INTOUT[0] = skew;
}


/*
 * Metrics: vqt_advance() (with the 16.16 fixed point advance after the
 * whole one, for vqt_advance32()) and v_getbitmap_info()
 */

/* the advance as a move, for the rotation */
static void advance_move(Vwk *vwk, WORD adv, WORD *x, WORD *y)
{
    *x = *y = 0;
    switch(vwk->chup) {
    default:
        *x = adv;
        break;
    case 900:
        *y = -adv;
        break;
    case 1800:
        *x = -adv;
        break;
    case 2700:
        *y = adv;
        break;
    }
}

void vdi_vqt_advance(Vwk *vwk)
{
    WORD x, y;
    LONG fix;

    advance_move(vwk, vdi_text_advance(vwk, INTIN[0]), &x, &y);
    PTSOUT[0] = x;
    PTSOUT[1] = y;
    PTSOUT[2] = PTSOUT[3] = 0;          /* the remainders: whole pixels */
    fix = (LONG)x << 16;
    memcpy(&PTSOUT[4], &fix, sizeof(fix));
    fix = (LONG)y << 16;
    memcpy(&PTSOUT[6], &fix, sizeof(fix));
    CONTRL->nptsout = 4;
}

/*
 * v_getbitmap_info(): the advance, but no bitmap: the glyphs are only
 * kept in the forms of the sizes, not one by one
 */
void vdi_v_getbitmap_info(Vwk *vwk)
{
    WORD x, y;

    advance_move(vwk, vdi_text_advance(vwk, INTIN[0]), &x, &y);
    INTOUT[0] = INTOUT[1] = 0;          /* width and height */
    intout_long(2, (LONG)x << 16);
    intout_long(4, (LONG)y << 16);
    intout_long(6, 0);                  /* the offsets */
    intout_long(8, 0);
    intout_long(10, 0);                 /* the bitmap */
    CONTRL->nintout = 12;
}


/*
 * Character maps: vst_charmap() and vst_map_mode() (which answers), and
 * vqt_char_index()
 */
void vdi_vst_charmap(Vwk *vwk)
{
    WORD mode = INTIN[0];

    if (mode >= MAP_BICS && mode <= MAP_UTF16)
        vwk->char_map = mode;

    /* vst_map_mode() has a second intin of 1, and an answer */
    if (CONTRL->nintin >= 2 && INTIN[1] == 1)
    {
        INTOUT[0] = vwk->char_map;
        CONTRL->nintout = 1;
    }
}

/*
 * modes of vqt_char_index(): the "direct" index is the character of
 * vst_map_mode(0), BICS, which the text path draws in that mode, so that
 * what the call answers draws as the character it was asked about
 */
#define CHARIDX_DIRECT      0       /* BICS */
#define CHARIDX_ASCII       1       /* the Atari character set */
#define CHARIDX_UNICODE     2

void vdi_vqt_char_index(Vwk *vwk)
{
    UWORD c = INTIN[0];
    WORD src = INTIN[1], dst = INTIN[2];
    const Fonthead *font = vwk->cur_font;
    WORD index = (font->flags & F_SFONT) ? vdi_sfont_find(font->font_id) : -1;
    LONG cp = -1;
    UWORD result = 0xffff;

    /* the character, in Unicode */
    switch (src) {
    case CHARIDX_DIRECT:
        cp = vdi_bics_to_unicode(c);
        break;
    case CHARIDX_ASCII:
        if (c >= 32 && c < 256)
            cp = vdi_atari_to_unicode((UBYTE)c);
        break;
    case CHARIDX_UNICODE:
        if (c < 0xd800 || c >= 0xe000)  /* a surrogate is half a character */
            cp = c;
        break;
    }

    if (cp > 0)
    {
        switch (dst) {
        case CHARIDX_DIRECT:
            {
                WORD bics = vdi_unicode_to_bics(cp);

                /* only if the font has it, as SpeedoGDOS's direct index */
                if (bics >= 0 && (index >= 0 ? vdi_sfont_char_glyph(index, cp) != 0
                                             : cp >= font->first_ade && cp <= font->last_ade))
                    result = bics;
            }
            break;
        case CHARIDX_ASCII:
            {
                WORD atari = vdi_unicode_to_atari(cp);

                if (atari >= 32)
                    result = atari;
            }
            break;
        case CHARIDX_UNICODE:
            if (cp <= 0xffffL)
                result = (UWORD)cp;
            break;
        }
    }

    INTOUT[0] = result;
}


/*
 * Kerning: vst_kern() (track mode, pair mode; track mode 255 with an offset
 * in 16.16 pixels after them is vst_track_offset()), vqt_pairkern() and
 * vqt_trackkern(), whose answers are 16.16 pixels
 */

/* the kerning pairs of the current font */
static WORD kern_pairs(Vwk *vwk)
{
    WORD index;

    if (!(vwk->cur_font->flags & F_SFONT))
        return 0;
    index = vdi_sfont_find(vwk->cur_font->font_id);

    return index >= 0 ? (WORD)(vdi_sfonts[index].kern_pairs & 0x7fff) : 0;
}

void vdi_vst_kern(Vwk *vwk)
{
    LONG offset = 0;

    if (INTIN[0] == 255 && CONTRL->nintin >= 4)
        memcpy(&offset, &INTIN[2], sizeof(offset));
    vwk->track_offset = offset;     /* there are no tracks: only an offset */
    vwk->pair_kern = INTIN[1] != 0;

    INTOUT[0] = 0;
    INTOUT[1] = kern_pairs(vwk);
}

/* a move along the baseline, in 16.16 pixels, for the rotation, into intout */
static void kern_answer(Vwk *vwk, LONG d)
{
    LONG x = 0, y = 0;

    switch(vwk->chup) {
    default:
        x = d;
        break;
    case 900:
        y = -d;
        break;
    case 1800:
        x = -d;
        break;
    case 2700:
        y = d;
        break;
    }
    intout_long(0, x);
    intout_long(2, y);
}

void vdi_vqt_pairkern(Vwk *vwk)
{
    LONG cps[2], kern[2];
    WORD i, c;

    /* the two characters in Unicode (from the first plane, in UTF-16) */
    for (i = 0; i < 2; i++)
    {
        c = INTIN[i];
        if (vwk->char_map == MAP_ATARI)
            cps[i] = (c >= 0 && c < 256) ? vdi_atari_to_unicode((UBYTE)c) : 0;
        else if (vwk->char_map == MAP_BICS)
            cps[i] = vdi_bics_to_unicode(c);
        else
            cps[i] = (UWORD)c;
    }
    if (!(vwk->cur_font->flags & F_SFONT)
        || !vdi_sfont_kerning(vwk->cur_font, -1, cps, 2, kern))
        kern[1] = 0;
    kern_answer(vwk, kern[1] << 10);    /* 26.6 to 16.16 */
}

void vdi_vqt_trackkern(Vwk *vwk)
{
    kern_answer(vwk, vwk->track_offset);
}
