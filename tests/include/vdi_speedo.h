/*
 * vdi_speedo.h - bindings for the font calls of SpeedoGDOS and NVDI
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * libcmini has no VDI bindings, so the tests and test programs that use
 * the built-in GDOS's calls (#577) take them from here: the calls with
 * gemlib's names and arguments, written from their documented parameter
 * layouts.  Only the calls of opcodes 190 and 229 to 255 and
 * vqt_ext_name() are here; the classic ones a test makes itself.  The
 * header has its own parameter arrays, and only static inline functions,
 * so that a program pays for what it uses.
 *
 * Pointers go in two WORDs of intin or intout, as on the 68000 (memcpy(),
 * as a WORD array need not be aligned for them).  Text is passed as WORDs,
 * one per character (in the character map vst_map_mode() set), with a
 * count, not as a C string.
 */

#ifndef VDI_SPEEDO_H
#define VDI_SPEEDO_H

#include <string.h>

#define VS_MAX  256                     /* the largest array a call takes */

static short vs_contrl[16], vs_intin[VS_MAX], vs_ptsin[VS_MAX];
static short vs_intout[VS_MAX], vs_ptsout[VS_MAX];
static void *vs_pb[5] = { vs_contrl, vs_intin, vs_ptsin, vs_intout, vs_ptsout };

/* NVDI's XFNT_INFO (vqt_xfntinfo()) */
typedef struct
{
    long size;                          /* set to sizeof(XFNT_INFO) */
    short format, id, index;
    char font_name[50], family_name[50], style_name[50];
    char file_name1[200], file_name2[200], file_name3[200];
    short pt_cnt;
    short pt_sizes[64];
} XFNT_INFO;

/* vqt_char_index()'s modes, and vst_map_mode()'s */
#define CHARIDX_DIRECT  0
#define CHARIDX_ASCII   1
#define CHARIDX_UNICODE 2
#define MAP_BITSTREAM   0
#define MAP_ATARI       1
#define MAP_UNICODE     2
#define MAP_UTF16       3               /* pTOS's */

/* a VDI call: opcode, subcode, counts in; returns intout[0] */
static inline short vs_call(short handle, short op, short sub, short nptsin, short nintin)
{
    vs_contrl[0] = op;
    vs_contrl[1] = nptsin;
    vs_contrl[2] = vs_contrl[4] = 0;
    vs_contrl[3] = nintin;
    vs_contrl[5] = sub;
    vs_contrl[6] = handle;
#ifdef __mc68000__
    {
        register long d0 __asm__("d0") = 0x73;
        register void *d1 __asm__("d1") = vs_pb;

        __asm__ volatile (
            "trap #2"
            : "+d" (d0), "+d" (d1)
            :
            : "d2", "a0", "a1", "memory", "cc");
    }
#elif defined(__arm__)
    {
        register long r0 __asm__("r0") = 0x73;
        register void *r1 __asm__("r1") = vs_pb;

        __asm__ volatile (
            "svc 2"
            : "+r" (r0), "+r" (r1)
            :
            : "r2", "r3", "r12", "lr", "memory", "cc");
    }
#endif
    return vs_intout[0];
}

static inline void vs_put_ptr(short *at, const void *p)
{
    memcpy(at, &p, sizeof(p));
}

static inline long vs_long(const short *at)
{
    long l;

    memcpy(&l, at, sizeof(l));
    return l;
}

static inline void vs_put_long(short *at, long l)
{
    memcpy(at, &l, sizeof(l));
}

/* the size answer of vst_arbpt() and the like */
static inline void vs_sizes(short *char_width, short *char_height,
                            short *cell_width, short *cell_height)
{
    *char_width = vs_ptsout[0];
    *char_height = vs_ptsout[1];
    *cell_width = vs_ptsout[2];
    *cell_height = vs_ptsout[3];
}

static inline short vs_text(const short *str, short n, short at)
{
    if (n > VS_MAX - at)
        n = VS_MAX - at;
    memcpy(&vs_intin[at], str, n * sizeof(short));
    return n;
}


/* Names */

/* name: 33 chars, the last being 0 for a bitmap font, 1 for a vector font */
static inline short vqt_ext_name(short handle, short index, char *name,
                                 short *font_format, short *flags)
{
    short i;

    vs_intin[0] = index;
    vs_intin[1] = 0;
    vs_call(handle, 130, 1, 0, 2);
    for (i = 0; i < 32; i++)
        name[i] = (char)vs_intout[i + 1];
    name[32] = (char)vs_intout[33];     /* 0: a bitmap font, 1: a vector font */
    *font_format = vs_contrl[4] > 34 ? vs_intout[34] & 0xff : 0;
    *flags = vs_contrl[4] > 34 ? (vs_intout[34] >> 8) & 0xff : 0;
    return vs_intout[0];
}

static inline short vqt_xfntinfo(short handle, short flags, short id, short index, XFNT_INFO *info)
{
    info->size = sizeof(XFNT_INFO);
    vs_intin[0] = flags;
    vs_intin[1] = id;
    vs_intin[2] = index;
    vs_put_ptr(&vs_intin[3], info);
    vs_intout[1] = 0;
    vs_call(handle, 229, 0, 0, 3 + sizeof(void *) / 2);
    info->format = vs_intout[0];
    info->id = vs_intout[1];
    info->index = vs_intout[2];
    return vs_intout[1];
}

/* vst_name() (sub 0) and vqt_name_and_id() (sub 100) */
static inline short vs_name(short handle, short sub, short font_format,
                            const char *font_name, char *ret_name)
{
    short n, i;

    vs_intin[0] = font_format;
    for (n = 0; font_name[n] && n < VS_MAX - 2; n++)
        vs_intin[n + 1] = (unsigned char)font_name[n];
    vs_intin[n + 1] = 0;
    vs_call(handle, 230, sub, 0, n + 2);
    for (i = 0; i < vs_contrl[4] - 1; i++)
        ret_name[i] = (char)vs_intout[i + 1];
    ret_name[i < 0 ? 0 : i] = '\0';
    return vs_intout[0];
}

static inline short vst_name(short handle, short font_format, const char *font_name, char *ret_name)
{
    return vs_name(handle, 0, font_format, font_name, ret_name);
}

static inline short vqt_name_and_id(short handle, short font_format, const char *font_name,
                                    char *ret_name)
{
    return vs_name(handle, 100, font_format, font_name, ret_name);
}

static inline void vqt_fontheader(short handle, char *buffer, char *pathname)
{
    short i;

    vs_put_ptr(&vs_intin[0], buffer);
    vs_call(handle, 232, 0, 0, sizeof(void *) / 2);
    for (i = 0; i < vs_contrl[4]; i++)
        pathname[i] = (char)vs_intout[i];
    pathname[i] = '\0';
}


/* Sizes */

static inline short vst_arbpt(short handle, short point, short *char_width, short *char_height,
                              short *cell_width, short *cell_height)
{
    vs_intin[0] = point;
    vs_call(handle, 246, 0, 0, 1);
    vs_sizes(char_width, char_height, cell_width, cell_height);
    return vs_intout[0];
}

static inline long vst_arbpt32(short handle, long point, short *char_width, short *char_height,
                               short *cell_width, short *cell_height)
{
    vs_put_long(&vs_intin[0], point);
    vs_call(handle, 246, 0, 0, 2);
    vs_sizes(char_width, char_height, cell_width, cell_height);
    return vs_long(&vs_intout[0]);
}

static inline short vst_setsize(short handle, short point, short *char_width, short *char_height,
                                short *cell_width, short *cell_height)
{
    vs_intin[0] = point;
    vs_call(handle, 252, 0, 0, 1);
    vs_sizes(char_width, char_height, cell_width, cell_height);
    return vs_intout[0];
}

static inline long vst_setsize32(short handle, long point, short *char_width, short *char_height,
                                 short *cell_width, short *cell_height)
{
    vs_put_long(&vs_intin[0], point);
    vs_call(handle, 252, 0, 0, 2);
    vs_sizes(char_width, char_height, cell_width, cell_height);
    return vs_long(&vs_intout[0]);
}

static inline void vst_width(short handle, short width, short *char_width, short *char_height,
                             short *cell_width, short *cell_height)
{
    vs_ptsin[0] = width;
    vs_ptsin[1] = 0;
    vs_call(handle, 231, 0, 1, 0);
    vs_sizes(char_width, char_height, cell_width, cell_height);
}

static inline short vst_skew(short handle, short skew)
{
    vs_intin[0] = skew;
    return vs_call(handle, 253, 0, 0, 1);
}


/* Metrics and text */

static inline void vqt_advance(short handle, short ch, short *advx, short *advy,
                               short *remx, short *remy)
{
    vs_intin[0] = ch;
    vs_call(handle, 247, 0, 0, 1);
    *advx = vs_ptsout[0];
    *advy = vs_ptsout[1];
    *remx = vs_ptsout[2];
    *remy = vs_ptsout[3];
}

static inline void vqt_advance32(short handle, short ch, long *advx, long *advy)
{
    vs_intin[0] = ch;
    vs_call(handle, 247, 0, 0, 1);
    *advx = vs_long(&vs_ptsout[4]);
    *advy = vs_long(&vs_ptsout[6]);
}

static inline void vqt_f_extent(short handle, const short *str, short n, short *extent)
{
    n = vs_text(str, n, 0);
    vs_call(handle, 240, 0, 0, n);
    memcpy(extent, vs_ptsout, 8 * sizeof(short));
}

static inline void vqt_real_extent(short handle, short x, short y, const short *str, short n,
                                   short *extent)
{
    n = vs_text(str, n, 0);
    vs_ptsin[0] = x;
    vs_ptsin[1] = y;
    vs_call(handle, 240, 4200, 1, n);
    memcpy(extent, vs_ptsout, 8 * sizeof(short));
}

static inline void v_ftext(short handle, short x, short y, const short *str, short n)
{
    n = vs_text(str, n, 0);
    vs_ptsin[0] = x;
    vs_ptsin[1] = y;
    vs_call(handle, 241, 0, 1, n);
}

/* offset: a pair (x, y) per WORD of str */
static inline void v_ftext_offset(short handle, short x, short y, const short *str, short n,
                                  const short *offset)
{
    n = vs_text(str, n, 0);
    if (n > VS_MAX / 2 - 1)
        n = VS_MAX / 2 - 1;
    vs_ptsin[0] = x;
    vs_ptsin[1] = y;
    memcpy(&vs_ptsin[2], offset, 2 * n * sizeof(short));
    vs_call(handle, 241, 0, n + 1, n);
}

static inline void v_getbitmap_info(short handle, short ch, long *advancex, long *advancey,
                                    long *xoffset, long *yoffset, short *width, short *height,
                                    short **bitmap)
{
    vs_intin[0] = ch;
    vs_call(handle, 239, 0, 0, 1);
    *width = vs_intout[0];
    *height = vs_intout[1];
    *advancex = vs_long(&vs_intout[2]);
    *advancey = vs_long(&vs_intout[4]);
    *xoffset = vs_long(&vs_intout[6]);
    *yoffset = vs_long(&vs_intout[8]);
    memcpy(bitmap, &vs_intout[10], sizeof(*bitmap));
}

static inline short v_getoutline(short handle, short ch, short *xyarray, char *bezarray,
                                 short maxverts)
{
    vs_intin[0] = ch;
    vs_intin[1] = maxverts;
    vs_put_ptr(&vs_intin[2], xyarray);
    vs_put_ptr(&vs_intin[2 + sizeof(void *) / 2], bezarray);
    return vs_call(handle, 243, 0, 0, 2 + sizeof(void *));
}

static inline void v_killoutline(short handle, void *component)
{
    vs_put_ptr(&vs_intin[0], component);
    vs_call(handle, 242, 0, 0, sizeof(void *) / 2);
}


/* Character maps */

static inline void vst_charmap(short handle, short mode)
{
    vs_intin[0] = mode;
    vs_call(handle, 236, 0, 0, 1);
}

static inline short vst_map_mode(short handle, short mode)
{
    vs_intin[0] = mode;
    vs_intin[1] = 1;
    return vs_call(handle, 236, 0, 0, 2);
}

static inline unsigned short vqt_char_index(short handle, short index, short src_mode,
                                            short dst_mode)
{
    vs_intin[0] = index;
    vs_intin[1] = src_mode;
    vs_intin[2] = dst_mode;
    return (unsigned short)vs_call(handle, 190, 0, 0, 3);
}


/* Kerning */

static inline void vst_kern(short handle, short tmode, short pmode, short *tracks, short *pairs)
{
    vs_intin[0] = tmode;
    vs_intin[1] = pmode;
    vs_call(handle, 237, 0, 0, 2);
    *tracks = vs_intout[0];
    *pairs = vs_intout[1];
}

static inline void vst_track_offset(short handle, long offset, short pairmode,
                                    short *tracks, short *pairs)
{
    vs_intin[0] = 255;
    vs_intin[1] = pairmode;
    vs_put_long(&vs_intin[2], offset);
    vs_call(handle, 237, 0, 0, 4);
    *tracks = vs_intout[0];
    *pairs = vs_intout[1];
}

static inline void vqt_pairkern(short handle, short ch1, short ch2, long *x, long *y)
{
    vs_intin[0] = ch1;
    vs_intin[1] = ch2;
    vs_call(handle, 235, 0, 0, 2);
    *x = vs_long(&vs_intout[0]);
    *y = vs_long(&vs_intout[2]);
}

static inline void vqt_trackkern(short handle, long *x, long *y)
{
    vs_call(handle, 234, 0, 0, 0);
    *x = vs_long(&vs_intout[0]);
    *y = vs_long(&vs_intout[2]);
}


/* Errors and the cache */

static inline void vst_error(short handle, short mode, short *errorvar)
{
    vs_intin[0] = mode;
    vs_put_ptr(&vs_intin[1], errorvar);
    vs_call(handle, 245, 0, 0, 1 + sizeof(void *) / 2);
}

static inline void vst_scratch(short handle, short mode)
{
    vs_intin[0] = mode;
    vs_call(handle, 244, 0, 0, 1);
}

static inline void vqt_cachesize(short handle, short which_cache, long *size)
{
    vs_intin[0] = which_cache;
    vs_call(handle, 255, 0, 0, 1);
    *size = vs_long(&vs_intout[0]);
}

static inline void vqt_get_table(short handle, short **map)
{
    vs_call(handle, 254, 0, 0, 0);
    memcpy(map, &vs_intout[0], sizeof(*map));
}

static inline short v_flushcache(short handle)
{
    return vs_call(handle, 251, 0, 0, 0);
}

static inline short v_savecache(short handle, const char *filename)
{
    short n;

    for (n = 0; filename[n] && n < VS_MAX - 1; n++)
        vs_intin[n] = (unsigned char)filename[n];
    vs_intin[n] = 0;
    return vs_call(handle, 249, 0, 0, n + 1);
}

static inline short v_loadcache(short handle, const char *filename, short mode)
{
    short n;

    vs_intin[0] = mode;
    for (n = 0; filename[n] && n < VS_MAX - 2; n++)
        vs_intin[n + 1] = (unsigned char)filename[n];
    vs_intin[n + 1] = 0;
    return vs_call(handle, 250, 0, 0, n + 2);
}

#endif /* VDI_SPEEDO_H */
