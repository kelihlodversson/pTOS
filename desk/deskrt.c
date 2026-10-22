/*
 * deskrt.c - the runtime of EmuDesk as a program (CONF_EXTERNAL_DESK)
 *
 * Linked into the kernel, EmuDesk uses the variables and the internal
 * helpers of the AES and the VDI (gl_handle, gl_wchar, ..., gsx_attr(),
 * bb_fill(), ...) and the variables of the BIOS.  As a program it has none
 * of them, so this file provides them, with the same names, from what a
 * program can ask: the AES (graf_handle(), wind_get()), the VDI on a
 * virtual workstation of its own, and Ssystem().  See desk/deskrt.h.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"

#if CONF_EXTERNAL_DESK

#include "obdefs.h"
#include "aesdefs.h"
#include "gsxdefs.h"
#include "aesbind.h"
#include "biosext.h"
#include "xbiosbind.h"
#include "bdosbind.h"
#include "string.h"
#include "nls.h"
#include "bdosdefs.h"
#include "../bdos/ssystem.h"
#include "deskrt.h"

#define VDI_OPCODE      115
#define VDI_INTIN_SIZE  300     /* a string of the desktop, as words */

/* the AES state the desktop reads */
WORD gl_handle;                 /* the AES' physical workstation */
WORD gl_wchar, gl_hchar;        /* the character cell of the normal font */
WORD gl_wschar, gl_hschar;      /* ... and of the small font */
WORD gl_wbox, gl_hbox;          /* the box of the AES objects */
WORD gl_width, gl_height;       /* the screen */
WORD gl_nplanes;
GRECT gl_rscreen;               /* the screen */
GRECT gl_rfull;                 /* the screen but the menu bar */

/* the basepage of this program, from its startup code (deskstart.S) */
void *desk_basepage;

/* what Ssystem() told, once */
static struct desk_info info;

/*
 * the VDI: a virtual workstation of the desktop's own, so that the clip
 * rectangle and the attributes it sets are not the AES'
 */
VDICONTROL contrl;
WORD intin[128];
WORD ptsin[20];
static WORD intout[64];
static WORD ptsout[16];
static VDIPB vdipb = { &contrl, intin, ptsin, intout, ptsout };

static WORD vdi_handle;         /* the desktop's workstation */
static WORD vdi_planes;

/* the attributes last set, to save calls (aes/gemgraf.c) */
static WORD gl_mode, gl_tcolor, gl_lcolor, gl_fis, gl_patt, gl_font;
static WORD hptschar_ibm, hptschar_small;       /* character heights for vst_height() */
static WORD hpts_ibm, hpts_small;               /* ... and what it made of them */
GRECT gl_clip;


static void vdi_call(void)
{
#ifdef __arm__
    register unsigned long r0 __asm__("r0") = VDI_OPCODE;
    register void *r1 __asm__("r1") = &vdipb;

    __asm__ volatile ("svc 2"
                      : "+r" (r0), "+r" (r1)
                      :
                      : "r2", "r3", "r7", "r12", "lr", "memory", "cc");
#else
    __asm__ volatile ("move.l  %0,%%d1\n\t"
                      "move.w  #115,%%d0\n\t"
                      "trap    #2"
                      :
                      : "g" (&vdipb)
                      : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc");
#endif
}

/* a call of the desktop's workstation, with the numbers of the parameters */
static void vdi(WORD opcode, WORD nptsin, WORD nintin, WORD sub)
{
    contrl.code = opcode;
    contrl.nptsin = nptsin;
    contrl.nptsout = 0;
    contrl.nintin = nintin;
    contrl.nintout = 0;
    contrl.subcode = sub;
    contrl.handle = vdi_handle;
    vdi_call();
}

/* ---- Ssystem() ---- */

#ifdef __m68k__
#define ssystem(mode, a1, a2)   trap1_wwll(0x154, (mode), (long)(a1), (long)(a2))
#else
#define ssystem(mode, a1, a2)   trap1(0x154, (mode), (long)(a1), (long)(a2))
#endif

#define ssys(mode, a1, a2)      ssystem(mode, a1, a2)


/* ---- the set-up ---- */

static void ibm_size(void)
{
    vdi(38, 0, 0, 0);                   /* vqt_attributes() */
    hptschar_ibm = ptsout[1];
}

/*
 * the language of the messages: LANG of the environment, which the kernel
 * puts there (bios/bios.c), as for EmuCON; the default is English
 */
static void set_language(void)
{
#if CONF_WITH_NLS
    const char *env = desk_basepage ? (const char *)((PD *)desk_basepage)->p_env : NULL;
    char name[3];

    nls_init();
    for ( ; env && *env; env += strlen(env) + 1)
    {
        if (!strncmp(env, "LANG=", 5) && env[5])
        {
            name[0] = env[5];
            name[1] = env[6];
            name[2] = '\0';
            if (!strcmp(name, "el"))    /* pTOS names Greek "gr", ISO 639 "el" */
                strcpy(name, "gr");
            nls_set_lang(name);
            break;
        }
    }
#endif
}

void desk_rt_init(void)
{
    WORD i, wc, hc;

    set_language();

    ssys(S_DESKINFO, &info, sizeof(info));

    gl_width = gl_height = 0;
    for (i = 0; i < 10; i++)
        intin[i] = 1;
    intin[10] = 2;                      /* raster coordinates */
    vdi_handle = gl_handle;
    vdi(100, 0, 11, 0);                 /* v_opnvwk() */
    vdi_handle = contrl.handle;
    gl_width = intout[0] + 1;
    gl_height = intout[1] + 1;
    wc = ptsout[0];                     /* the smallest character */
    hc = ptsout[1];
    (void)wc;

    intin[0] = 1;
    vdi(102, 0, 1, 1);                  /* vq_extnd(1): the planes */
    gl_nplanes = vdi_planes = intout[4];

    /* the normal font is the current one, the small font the smallest */
    ibm_size();
    hptschar_small = hc;
    ptsin[0] = 0;
    ptsin[1] = hptschar_small;
    vdi(12, 1, 0, 0);                   /* vst_height() */
    gl_wschar = ptsout[2];
    gl_hschar = ptsout[3];
    ptsin[0] = 0;
    ptsin[1] = hptschar_ibm;
    vdi(12, 1, 0, 0);                   /* back to the normal font */

    intin[0] = 7;
    vdi(15, 0, 1, 0);                   /* vsl_type() */
    intin[0] = 1;
    vdi(16, 1, 0, 0);                   /* vsl_width() */
    ptsin[0] = 1;
    ptsin[1] = 0;
    vdi(16, 1, 0, 0);
    intin[0] = (WORD)0xffff;
    vdi(113, 0, 1, 0);                  /* vsl_udsty() */

    gl_mode = gl_tcolor = gl_lcolor = -1;
    gl_fis = gl_patt = gl_font = -1;

    r_set(&gl_rscreen, 0, 0, gl_width, gl_height);
    r_set(&gl_rfull, 0, gl_hbox, gl_width, gl_height - gl_hbox);
    gl_clip = gl_rscreen;
}


/* ---- what the kernel told (Ssystem(S_DESKINFO)) and what it is told ---- */

UBYTE desk_bootflags(void)
{
    return (UBYTE)info.bootflags;
}

BOOL desk_rez_changeable(void)
{
    return info.rez_changeable != 0;
}

BOOL desk_has_alt_ram(void)
{
    return info.has_alt_ram != 0;
}

/* the _IDT cookie: the format of the date and time of the files */
LONG desk_idt(void)
{
    LONG value = 0;

    if (ssys(S_GETCOOKIE, 0x5f494454L, &value) < 0)     /* '_IDT' */
        return 0;
    return value;
}

void desk_set_ceh(BOOL enable)
{
    ssys(S_DESKSET, DESKSET_CEH, enable != 0);
}

void desk_set_background(UBYTE patcol)
{
    ssys(S_DESKSET, DESKSET_BACKGROUND, patcol);
}

BOOL desk_cache_exists(void)
{
    return info.cache_exists != 0;
}

void desk_set_cache(WORD enable)
{
    ssys(S_DESKSET, DESKSET_CACHE, enable != 0);
}

BOOL desk_can_shutdown(void)
{
    return info.can_shutdown != 0;
}

BOOL desk_has_videl(void)
{
    return info.has_videl != 0;
}

BOOL desk_has_tt_shifter(void)
{
    return info.has_tt_shifter != 0;
}

WORD desk_videl_mode(void)
{
#if CONF_WITH_VIDEL
    return info.has_videl ? VsetMode(-1) : 0;
#else
    return 0;
#endif
}

/* the type of the floppy drives, from the _FDC cookie: 0 if DD, 1 if HD */
WORD desk_floppy_type(void)
{
    LONG value;

    if (ssys(S_GETCOOKIE, 0x5f464443L, &value) < 0)     /* '_FDC' */
        return 0;
    return (WORD)((ULONG)value >> 24);
}

/* the mouse buttons, as Line-A has them (bit 0 left, bit 1 right) */
WORD desk_mouse_buttons(void)
{
    WORD mx, my, mstate, kstate;

    graf_mkstate(&mx, &my, &mstate, &kstate);
    return mstate;
}


/* ---- aes/gemgraf.c on the desktop's workstation ---- */

/* the clip rectangle; a width or height of 0 switches clipping off */
void gsx_sclip(const GRECT *pt)
{
    gl_clip = *pt;

    if (gl_clip.g_w && gl_clip.g_h)
    {
        ptsin[0] = gl_clip.g_x;
        ptsin[1] = gl_clip.g_y;
        ptsin[2] = gl_clip.g_x + gl_clip.g_w - 1;
        ptsin[3] = gl_clip.g_y + gl_clip.g_h - 1;
        intin[0] = TRUE;
    }
    else
        intin[0] = FALSE;
    vdi(129, 2, 1, 0);                  /* vs_clip() */
}

void gsx_gclip(GRECT *pt)
{
    *pt = gl_clip;
}

/* the text, writing mode and colour */
void gsx_attr(UWORD text, UWORD mode, UWORD color)
{
    WORD tmp = intin[0];

    if (mode != gl_mode)
    {
        intin[0] = gl_mode = mode;
        vdi(32, 0, 1, 0);               /* vswr_mode() */
    }

    if (text)
    {
        if (color != gl_tcolor)
        {
            intin[0] = gl_tcolor = color;
            vdi(22, 0, 1, 0);           /* vst_color() */
        }
    }
    else
    {
        if (color != gl_lcolor)
        {
            intin[0] = gl_lcolor = color;
            vdi(17, 0, 1, 0);           /* vsl_color() */
        }
    }
    intin[0] = tmp;
}

/* a polyline, relative to an offset */
void gsx_pline(WORD offx, WORD offy, WORD cnt, const Point *pts)
{
    WORD i;

    for (i = 0; i < cnt; i++)
    {
        ptsin[2 * i] = offx + pts[i].x;
        ptsin[2 * i + 1] = offy + pts[i].y;
    }
    vdi(6, cnt, 0, 0);                  /* v_pline() */
}

/* the text in intin[], tb_nc characters of it, in the IBM or the small font */
void gsx_tblt(WORD tb_f, WORD x, WORD y, WORD tb_nc)
{
    if (tb_f == IBM || tb_f == SMALL)
    {
        WORD *hin = (tb_f == IBM) ? &hptschar_ibm : &hptschar_small;
        WORD *hout = (tb_f == IBM) ? &hpts_ibm : &hpts_small;

        if (tb_f != gl_font)
        {
            ptsin[0] = 0;
            ptsin[1] = *hin;
            vdi(12, 1, 0, 0);           /* vst_height() */
            *hout = ptsout[1];
            gl_font = tb_f;
        }
        y += *hout;
    }

    ptsin[0] = x;
    ptsin[1] = y;
    vdi(8, 1, tb_nc, 0);                /* v_gtext() */
}

/* the MFDB of a raster form of wb bytes per row and h rows, or of the screen */
static void fix_mfdb(FDB *pfd, void *addr, WORD wb, WORD h)
{
    if (addr == NULL)
    {
        pfd->fd_addr = NULL;
        pfd->fd_w = gl_width;
        pfd->fd_h = gl_height;
        pfd->fd_wdwidth = pfd->fd_w / 16;
    }
    else
    {
        pfd->fd_addr = addr;
        pfd->fd_w = wb * 8;
        pfd->fd_h = h;
        pfd->fd_wdwidth = wb / 2;
    }
    pfd->fd_stand = FALSE;
    pfd->fd_nplanes = (addr == NULL) ? vdi_planes : 1;
    pfd->fd_r1 = pfd->fd_r2 = pfd->fd_r3 = 0;
}

/* a monochrome standard form to the device format, in place */
void gsx_trans(void *addr, UWORD wb, UWORD h)
{
    FDB src, dst;

    fix_mfdb(&src, addr, wb, h);
    src.fd_stand = TRUE;
    fix_mfdb(&dst, addr, wb, h);
    contrl.ptr1 = &src;
    contrl.ptr2 = &dst;
    vdi(110, 0, 0, 0);                  /* vrn_trnfm() */
}

/* a filled rectangle */
void bb_fill(WORD mode, WORD fis, WORD patt, WORD hx, WORD hy, WORD hw, WORD hh)
{
    gsx_attr(TRUE, mode, gl_tcolor);
    if (fis != gl_fis)
    {
        intin[0] = gl_fis = fis;
        vdi(23, 0, 1, 0);               /* vsf_interior() */
    }
    if (patt != gl_patt)
    {
        intin[0] = gl_patt = patt;
        vdi(24, 0, 1, 0);               /* vsf_style() */
    }

    ptsin[0] = hx;
    ptsin[1] = hy;
    ptsin[2] = hx + hw - 1;
    ptsin[3] = hy + hh - 1;
    vdi(11, 2, 0, 1);                   /* vr_recfl() */
}

/* a screen area copied to another */
void bb_screen(WORD scsx, WORD scsy, WORD scdx, WORD scdy, WORD scw, WORD sch)
{
    FDB src, dst;

    fix_mfdb(&src, NULL, 0, 0);
    fix_mfdb(&dst, NULL, 0, 0);
    ptsin[0] = scsx;
    ptsin[1] = scsy;
    ptsin[2] = scsx + scw - 1;
    ptsin[3] = scsy + sch - 1;
    ptsin[4] = scdx;
    ptsin[5] = scdy;
    ptsin[6] = scdx + scw - 1;
    ptsin[7] = scdy + sch - 1;
    intin[0] = S_ONLY;
    contrl.ptr1 = &src;
    contrl.ptr2 = &dst;
    graf_mouse(M_OFF, NULL);
    vdi(109, 4, 1, 0);                  /* vro_cpyfm() */
    graf_mouse(M_ON, NULL);
}

#endif /* CONF_EXTERNAL_DESK */
