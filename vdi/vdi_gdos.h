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
    UBYTE face;                     /* which face of its file: 0, but in a .TTC */
    char name[FONT_NAME_LEN + 1];   /* "Family Style", as vqt_name() gives it */
    char file[14];                  /* its 8.3 file name in CONF_VDI_FONT_DIR */
} VDI_SFONT;

/* the scalable fonts, sorted by name; none unless the GDOS is active */
extern WORD vdi_sfont_count;
extern VDI_SFONT *vdi_sfonts;

/* the path of a scalable font's file (in a buffer of vdi_gdos.c's) */
const char *vdi_sfont_path(WORD index);

/* which face of that file it is: the index FT_New_Face() takes */
LONG vdi_sfont_face(WORD index);

/*
 * Look for the fonts again if the scan at boot found none (the drive was
 * not there, say): nobody can have a font of the empty list to lose
 */
void vdi_sfont_rescan(void);

/*
 * Sizes of the scalable fonts (vdi/vdi_sfont.c): each is a Fonthead with
 * its glyphs rendered into a form, as a bitmap font's, so that the text
 * path draws it as it draws any other font.  F_SFONT marks one.
 */
#define F_SFONT             0x8000
/*
 * and F_SFONT_FIXED one of a fixed-pitch face, whose bold characters stay
 * in their cells, as F_MONOSPACE has it for a bitmap font: in each page
 * of the face alike, which F_MONOSPACE (all characters as wide, for the
 * text path to measure them so) may not be, with zero-width marks in it
 */
#define F_SFONT_FIXED       0x4000

/*
 * The forms of a system font (a header of the font ring: 8x8, 8x16) made
 * from the scalable font with that index, in the cell it has; FALSE if that
 * fails, and then it is as it was
 */
BOOL vdi_sfont_make_system(WORD index, Fonthead *font);

/* the scalable font with that id, or -1 */
WORD vdi_sfont_find(WORD font_id);

/* the em size, in 1/64 pixel, for a size in points, or for a cell top height */
LONG vdi_sfont_size_for_points(WORD points);
LONG vdi_sfont_size_for_top(WORD index, WORD top);

/* a scalable font at that em size (1/64 pixel), or with that top when
 * want is not 0, at that em width (0: as the size), slant (1/10 degree)
 * and mirroring (SF_MIRROR_X, SF_MIRROR_Y), rendered or from the cache;
 * NULL if it cannot be had (no memory, a font file gone) */
const Fonthead *vdi_sfont_size(WORD index, LONG size, WORD want, LONG width, WORD skew,
                               WORD mirror);

/* the mirroring of a size: upside down (a negative height), left to right (width) */
#define SF_MIRROR_X         1
#define SF_MIRROR_Y         2

/* a Unicode page of a workstation's current size, or NULL */
const Fonthead *vdi_sfont_page(const Fonthead *font, UWORD page);

/* the em size a size has */
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

/* a BICS character in Unicode, or -1; a Unicode one in BICS or the Atari set, or -1 */
LONG vdi_bics_to_unicode(UWORD c);
WORD vdi_unicode_to_bics(LONG cp);
WORD vdi_unicode_to_atari(LONG cp);
UWORD vdi_atari_to_unicode(UBYTE c);

/* a code point's glyph in a scalable font, 0 if none */
UWORD vdi_sfont_char_glyph(WORD index, LONG cp);

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
