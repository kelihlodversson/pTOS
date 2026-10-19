/*
 * vdi_gdos.h - the built-in GDOS layer, for the rest of the VDI
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VDI_GDOS_H
#define VDI_GDOS_H

#include "fonthdr.h"

#if CONF_WITH_VDI_SCALABLE_FONTS

/*
 * What vq_gdos() answers when the built-in GDOS is active: '_FSM', as
 * SpeedoGDOS does.  0 while it is not (before the AUTO folder has run,
 * or when a GDOS was loaded from disk), and the trap #2 handlers then
 * leave d0 at -2.  Read by the trap #2 entry of each architecture.
 */
extern ULONG vdi_gdos_id;

#define GDOS_ID_FSM         0x5f46534dUL    /* '_FSM' */

/* font_format of a font, as NVDI's vqt_ext_name() reports it */
#define SFONT_BITMAP        0x01
#define SFONT_SPEEDO        0x02
#define SFONT_TRUETYPE      0x04
#define SFONT_TYPE1         0x08

/* its flags: SFONT_MONOSPACE as vqt_ext_name() reports it, the others ours */
#define SFONT_MONOSPACE     0x01
#define SFONT_ITALIC        0x40
#define SFONT_BOLD          0x80

/* a scalable font the built-in GDOS found */
typedef struct
{
    WORD id;                        /* its font id, stable from boot to boot */
    UBYTE format;                   /* SFONT_TRUETYPE, ... */
    UBYTE flags;                    /* SFONT_MONOSPACE, ... */
    ULONG key;                      /* hash of the full family and style */
    UWORD upem;                     /* its design units per em */
    WORD ascender;                  /* its ascender, in those units */
    UWORD glyphs;                   /* how many it has */
    UWORD kern_pairs;               /* pairs in its 'kern' table, for vst_kern() */
    UBYTE style;                    /* where the style starts in name, 0 if regular */
    char name[FONT_NAME_LEN + 1];   /* "Family Style", as vqt_name() gives it */
    char file[14];                  /* its 8.3 file name in CONF_VDI_FONT_DIR */
} VDI_SFONT;

/* the scalable fonts, sorted by name; none unless the GDOS is active */
extern WORD vdi_sfont_count;
extern VDI_SFONT *vdi_sfonts;

/* the path of a scalable font's file (in a buffer of vdi_gdos.c's) */
const char *vdi_sfont_path(WORD index);

/*
 * Sizes of the scalable fonts (vdi/vdi_sfont.c): each is a Fonthead with
 * its glyphs rendered into a form, as a bitmap font's, so that the text
 * path draws it as it draws any other font.  F_SFONT marks one.
 */
#define F_SFONT             0x8000

/* the scalable font with that id, or -1 */
WORD vdi_sfont_find(WORD font_id);

/* the em size, in 1/64 pixel, for a size in points, or for a cell top height */
LONG vdi_sfont_size_for_points(WORD points);
LONG vdi_sfont_size_for_top(WORD index, WORD top);

/* a scalable font at that em size (1/64 pixel), or with that top when
 * want is not 0, at that em width (0: as the size) and slant (1/10
 * degree), rendered or from the cache; NULL if it cannot be had (no
 * memory, a font file gone) */
const Fonthead *vdi_sfont_size(WORD index, LONG size, WORD want, LONG width, WORD skew);

/* a Unicode page of a workstation's current size, or NULL */
const Fonthead *vdi_sfont_page(const Fonthead *font, UWORD page);

/* what a size was asked for as, and the em size it has */
void vdi_sfont_request(const Fonthead *font, LONG *size, WORD *want);
LONG vdi_sfont_em(const Fonthead *font);
LONG vdi_sfont_em_width(const Fonthead *font);

/* drop unselected sizes until the cache is within its limit again */
void vdi_sfont_trim(void);

/* vst_error()'s code when a size cannot be had: no memory for it */
#define SPEEDO_CACHE_FULL   11

/* the character maps of vst_map_mode(): mode 3 is pTOS's */
#define MAP_BICS            0       /* Bitstream International Character Set */
#define MAP_ATARI           1       /* the Atari character set */
#define MAP_UCS2            2       /* Unicode, its first plane */
#define MAP_UTF16           3       /* Unicode, all of it, with surrogate pairs */

/* a BICS character in Unicode, or -1; a Unicode one in the Atari set, or -1 */
LONG vdi_bics_to_unicode(UWORD c);
WORD vdi_unicode_to_atari(LONG cp);
UWORD vdi_atari_to_unicode(UBYTE c);

/* a code point's glyph in a scalable font, 0 if none; a glyph's code point, -1 if none */
UWORD vdi_sfont_char_glyph(WORD index, LONG cp);
LONG vdi_sfont_glyph_char(WORD index, UWORD glyph);

/*
 * the kerning (vst_kern()) before each of n code points of a size, after
 * the one before it, in 1/64 pixel (kern[0] is before cps[0], after
 * prev_cp, or 0 when there is none): FALSE when it cannot be had
 */
BOOL vdi_sfont_kerning(const Fonthead *font, LONG prev_cp, const LONG *cps, WORD n, LONG *kern);

/* v_flushcache() and vqt_cachesize() */
void vdi_sfont_flush(void);
ULONG vdi_sfont_cache_free(void);

/* the text scratch buffer of a size (an F_SFONT font), and its halves' offset */
void vdi_sfont_scratch(const Fonthead *font, WORD **buffer, WORD *half);

#endif /* CONF_WITH_VDI_SCALABLE_FONTS */

#endif /* VDI_GDOS_H */
