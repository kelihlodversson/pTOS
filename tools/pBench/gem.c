/*
 * gem.c - minimal AES/VDI bindings for pBench
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include <string.h>
#include "gem.h"

/* the AES/VDI parameter blocks */
typedef struct {
    WORD *control;
    WORD *global;
    WORD *intin;
    WORD *intout;
    LONG *addrin;
    LONG *addrout;
} AESPB;

typedef struct {
    WORD *contrl;
    WORD *intin;
    WORD *ptsin;
    WORD *intout;
    WORD *ptsout;
} VDIPB;

#define VDI_OPCODE  115
#define AES_OPCODE  200

#define MAX_STR     128

static WORD contrl[12];
static WORD intin[MAX_STR + 16];
static WORD ptsin[16];
static WORD intout[64];
static WORD ptsout[16];
static VDIPB vdipb = { contrl, intin, ptsin, intout, ptsout };

static WORD control[5];
static WORD global[15];
static WORD aintin[16];
static WORD aintout[8];
static LONG addrin[4];
static LONG addrout[2];
static AESPB aespb = { control, global, aintin, aintout, addrin, addrout };

#ifdef __arm__

static void gem_call(WORD opcode, void *pb)
{
    register unsigned long r0 __asm__("r0") = opcode;
    register void *r1 __asm__("r1") = pb;

    __asm__ volatile (
        "svc 2"
        : "+r" (r0), "+r" (r1)
        :
        : "r2", "r3", "r7", "r12", "lr", "memory", "cc");
}

#else

static void gem_call(WORD opcode, void *pb)
{
    __asm__ volatile (
        "move.l  %1,%%d1\n\t"
        "move.w  %0,%%d0\n\t"
        "trap    #2"
        :
        : "g" ((WORD)opcode), "g" (pb)
        : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc");
}

#endif

/* issue a VDI call */
static void vdi(WORD opcode, WORD nptsin, WORD nintin, WORD sub, WORD handle)
{
    contrl[0] = opcode;
    contrl[1] = nptsin;
    contrl[2] = 0;
    contrl[3] = nintin;
    contrl[4] = 0;
    contrl[5] = sub;
    contrl[6] = handle;
    gem_call(VDI_OPCODE, &vdipb);
}

/* issue an AES call, returning intout[0] */
static WORD aes(WORD opcode, WORD nin, WORD nout, WORD naddrin)
{
    control[0] = opcode;
    control[1] = nin;
    control[2] = nout;
    control[3] = naddrin;
    control[4] = 0;
    gem_call(AES_OPCODE, &aespb);
    return aintout[0];
}

/* copy a string into intin[], from index 'first'; returns its length */
static WORD put_string(WORD first, const char *s)
{
    WORD n = 0;

    while (s[n] && n < MAX_STR)
    {
        intin[first + n] = (unsigned char)s[n];
        n++;
    }
    return n;
}

/*
 * a LONG in a pair of WORDs of a parameter array: stored as the machine
 * stores it, which is high word first on the m68k and low word first on
 * the ARM
 */
static void put_long(WORD *dst, LONG value)
{
    memcpy(dst, &value, sizeof(value));
}

/* ---- AES ---- */

WORD appl_init(void)
{
    return aes(10, 0, 1, 0);
}

WORD appl_exit(void)
{
    return aes(19, 0, 1, 0);
}

WORD graf_handle(WORD *wchar, WORD *hchar, WORD *wbox, WORD *hbox)
{
    WORD h = aes(77, 0, 5, 0);

    *wchar = aintout[1];
    *hchar = aintout[2];
    *wbox = aintout[3];
    *hbox = aintout[4];
    return h;
}

WORD graf_mouse(WORD form, void *fmdata)
{
    aintin[0] = form;
    addrin[0] = (LONG)fmdata;
    return aes(78, 1, 1, 1);
}

WORD wind_create(WORD kind, WORD x, WORD y, WORD w, WORD h)
{
    aintin[0] = kind;
    aintin[1] = x;
    aintin[2] = y;
    aintin[3] = w;
    aintin[4] = h;
    return aes(100, 5, 1, 0);
}

WORD wind_open(WORD handle, WORD x, WORD y, WORD w, WORD h)
{
    aintin[0] = handle;
    aintin[1] = x;
    aintin[2] = y;
    aintin[3] = w;
    aintin[4] = h;
    return aes(101, 5, 1, 0);
}

WORD wind_close(WORD handle)
{
    aintin[0] = handle;
    return aes(102, 1, 1, 0);
}

WORD wind_delete(WORD handle)
{
    aintin[0] = handle;
    return aes(103, 1, 1, 0);
}

WORD wind_get(WORD handle, WORD field, WORD *w1, WORD *w2, WORD *w3, WORD *w4)
{
    WORD ret;

    aintin[0] = handle;
    aintin[1] = field;
    ret = aes(104, 2, 5, 0);
    *w1 = aintout[1];
    *w2 = aintout[2];
    *w3 = aintout[3];
    *w4 = aintout[4];
    return ret;
}

WORD wind_set_str(WORD handle, WORD field, const char *s)
{
    aintin[0] = handle;
    aintin[1] = field;
    put_long(&aintin[2], (LONG)s);
    aintin[4] = 0;
    aintin[5] = 0;
    return aes(105, 6, 1, 0);
}

WORD wind_calc(WORD type, WORD kind, WORD x, WORD y, WORD w, WORD h,
               WORD *ox, WORD *oy, WORD *ow, WORD *oh)
{
    WORD ret;

    aintin[0] = type;
    aintin[1] = kind;
    aintin[2] = x;
    aintin[3] = y;
    aintin[4] = w;
    aintin[5] = h;
    ret = aes(108, 6, 5, 0);
    *ox = aintout[1];
    *oy = aintout[2];
    *ow = aintout[3];
    *oh = aintout[4];
    return ret;
}

WORD wind_update(WORD what)
{
    aintin[0] = what;
    return aes(107, 1, 1, 0);
}

WORD form_dial(WORD flag, WORD lx, WORD ly, WORD lw, WORD lh,
               WORD x, WORD y, WORD w, WORD h)
{
    aintin[0] = flag;
    aintin[1] = lx;
    aintin[2] = ly;
    aintin[3] = lw;
    aintin[4] = lh;
    aintin[5] = x;
    aintin[6] = y;
    aintin[7] = w;
    aintin[8] = h;
    return aes(51, 9, 1, 0);
}

WORD objc_draw(OBJECT *tree, WORD start, WORD depth,
               WORD x, WORD y, WORD w, WORD h)
{
    aintin[0] = start;
    aintin[1] = depth;
    aintin[2] = x;
    aintin[3] = y;
    aintin[4] = w;
    aintin[5] = h;
    addrin[0] = (LONG)tree;
    return aes(42, 6, 1, 1);
}

/* fetch one pending AES message, if there is one, without waiting */
WORD evnt_mesag_poll(WORD *msg)
{
    WORD i;

    aintin[0] = 0x0030;         /* MU_MESAG | MU_TIMER */
    for (i = 1; i < 14; i++)
        aintin[i] = 0;
    aintin[14] = 0;             /* timer: no wait */
    aintin[15] = 0;
    addrin[0] = (LONG)msg;
    aes(25, 16, 7, 1);
    return aintout[0] & 0x0010;
}

WORD evnt_keybd(void)
{
    return aes(20, 0, 1, 0);
}

WORD evnt_timer(WORD lo, WORD hi)
{
    aintin[0] = lo;
    aintin[1] = hi;
    return aes(24, 2, 1, 0);
}

/* ---- VDI ---- */

WORD v_opnvwk(WORD aeshandle, WORD *out)
{
    WORD i;

    intin[0] = 1;
    for (i = 1; i < 10; i++)
        intin[i] = 1;
    intin[10] = 2;
    vdi(100, 0, 11, 0, aeshandle);
    for (i = 0; i < 45; i++)
        out[i] = intout[i];
    return contrl[6];
}

void v_clsvwk(WORD handle)
{
    vdi(101, 0, 0, 0, handle);
}

void v_clrwk(WORD handle)
{
    vdi(3, 0, 0, 0, handle);
}

void vs_clip(WORD handle, WORD flag, const GRECT *r)
{
    intin[0] = flag;
    ptsin[0] = r->x;
    ptsin[1] = r->y;
    ptsin[2] = r->x + r->w - 1;
    ptsin[3] = r->y + r->h - 1;
    vdi(129, 2, 1, 0, handle);
}

WORD vswr_mode(WORD handle, WORD mode)
{
    intin[0] = mode;
    vdi(32, 0, 1, 0, handle);
    return intout[0];
}

WORD vsf_color(WORD handle, WORD color)
{
    intin[0] = color;
    vdi(25, 0, 1, 0, handle);
    return intout[0];
}

WORD vsf_interior(WORD handle, WORD style)
{
    intin[0] = style;
    vdi(23, 0, 1, 0, handle);
    return intout[0];
}

WORD vsf_style(WORD handle, WORD style)
{
    intin[0] = style;
    vdi(24, 0, 1, 0, handle);
    return intout[0];
}

WORD vsl_color(WORD handle, WORD color)
{
    intin[0] = color;
    vdi(17, 0, 1, 0, handle);
    return intout[0];
}

WORD vst_color(WORD handle, WORD color)
{
    intin[0] = color;
    vdi(22, 0, 1, 0, handle);
    return intout[0];
}

WORD vst_point(WORD handle, WORD point)
{
    intin[0] = point;
    vdi(107, 0, 1, 0, handle);
    return intout[0];
}

WORD vst_effects(WORD handle, WORD effects)
{
    intin[0] = effects;
    vdi(106, 0, 1, 0, handle);
    return intout[0];
}

void vst_alignment(WORD handle, WORD h, WORD v)
{
    intin[0] = h;
    intin[1] = v;
    vdi(39, 0, 2, 0, handle);
}

void v_gtext(WORD handle, WORD x, WORD y, const char *s)
{
    WORD n = put_string(0, s);

    ptsin[0] = x;
    ptsin[1] = y;
    vdi(8, 1, n, 0, handle);
}

void v_justified(WORD handle, WORD x, WORD y, const char *s, WORD len,
                 WORD wordspace, WORD charspace)
{
    WORD n;

    intin[0] = wordspace;
    intin[1] = charspace;
    n = put_string(2, s);
    ptsin[0] = x;
    ptsin[1] = y;
    ptsin[2] = len;
    ptsin[3] = 0;
    vdi(11, 2, n + 2, 10, handle);
}

void v_pline2(WORD handle, WORD x1, WORD y1, WORD x2, WORD y2)
{
    ptsin[0] = x1;
    ptsin[1] = y1;
    ptsin[2] = x2;
    ptsin[3] = y2;
    vdi(6, 2, 0, 0, handle);
}

void v_bar(WORD handle, WORD x1, WORD y1, WORD x2, WORD y2)
{
    ptsin[0] = x1;
    ptsin[1] = y1;
    ptsin[2] = x2;
    ptsin[3] = y2;
    vdi(11, 2, 0, 1, handle);
}

void v_ellipse(WORD handle, WORD x, WORD y, WORD xr, WORD yr)
{
    ptsin[0] = x;
    ptsin[1] = y;
    ptsin[2] = xr;
    ptsin[3] = yr;
    vdi(11, 2, 0, 5, handle);
}

void vr_recfl(WORD handle, WORD x1, WORD y1, WORD x2, WORD y2)
{
    ptsin[0] = x1;
    ptsin[1] = y1;
    ptsin[2] = x2;
    ptsin[3] = y2;
    vdi(114, 2, 0, 0, handle);
}

void vro_cpyfm(WORD handle, WORD mode, const GRECT *src, const GRECT *dst)
{
    static MFDB screen;     /* all zero: the screen */
    LONG p = (LONG)&screen;

    intin[0] = mode;
    ptsin[0] = src->x;
    ptsin[1] = src->y;
    ptsin[2] = src->x + src->w - 1;
    ptsin[3] = src->y + src->h - 1;
    ptsin[4] = dst->x;
    ptsin[5] = dst->y;
    ptsin[6] = dst->x + dst->w - 1;
    ptsin[7] = dst->y + dst->h - 1;
#ifdef __arm__
    /* pTOS's ARM control array has a pad word before the pointers, so that
       they are aligned (include/vdipb.h) */
    put_long(&contrl[8], p);
    put_long(&contrl[10], p);
#else
    put_long(&contrl[7], p);
    put_long(&contrl[9], p);
#endif
    vdi(109, 4, 1, 0, handle);
}

void v_hide_c(WORD handle)
{
    vdi(123, 0, 0, 0, handle);
}

void v_show_c(WORD handle, WORD reset)
{
    intin[0] = reset;
    vdi(122, 0, 1, 0, handle);
}

void vq_extnd(WORD handle, WORD flag, WORD *out)
{
    WORD i;

    intin[0] = flag;
    vdi(102, 0, 1, 0, handle);
    for (i = 0; i < 45; i++)
        out[i] = intout[i];
    for (i = 0; i < 12; i++)
        out[45 + i] = ptsout[i];
}

void vql_attributes(WORD handle, WORD *attr)
{
    vdi(35, 0, 0, 0, handle);
    attr[0] = intout[0];
    attr[1] = intout[1];
    attr[2] = intout[2];
    attr[3] = intout[3];
}

void vqm_attributes(WORD handle, WORD *attr)
{
    vdi(36, 0, 0, 0, handle);
    attr[0] = intout[0];
    attr[1] = intout[1];
    attr[2] = intout[2];
    attr[3] = ptsout[1];
}

void vqf_attributes(WORD handle, WORD *attr)
{
    vdi(37, 0, 0, 0, handle);
    attr[0] = intout[0];
    attr[1] = intout[1];
    attr[2] = intout[2];
    attr[3] = intout[3];
    attr[4] = intout[4];
}

void vqt_attributes(WORD handle, WORD *attr)
{
    WORD i;

    vdi(38, 0, 0, 0, handle);
    for (i = 0; i < 6; i++)
        attr[i] = intout[i];
    for (i = 0; i < 4; i++)
        attr[6 + i] = ptsout[i];
}

void vqt_fontinfo(WORD handle, WORD *minade, WORD *maxade, WORD *distances,
                  WORD *maxwidth, WORD *effects)
{
    WORD i;

    vdi(131, 0, 0, 0, handle);
    *minade = intout[0];
    *maxade = intout[1];
    *maxwidth = ptsout[0];
    for (i = 0; i < 5; i++)
        distances[i] = ptsout[1 + 2 * i];
    for (i = 0; i < 3; i++)
        effects[i] = ptsout[2 * i + 2];
}

void vqt_extent(WORD handle, const char *s, WORD *pts)
{
    WORD n = put_string(0, s);
    WORD i;

    vdi(116, 0, n, 0, handle);
    for (i = 0; i < 8; i++)
        pts[i] = ptsout[i];
}

void vq_mouse(WORD handle, WORD *status, WORD *x, WORD *y)
{
    vdi(124, 0, 0, 0, handle);
    *status = intout[0];
    *x = ptsout[0];
    *y = ptsout[1];
}

void vq_key_s(WORD handle, WORD *state)
{
    vdi(128, 0, 0, 0, handle);
    *state = intout[0];
}
