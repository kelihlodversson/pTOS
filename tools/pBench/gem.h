/*
 * gem.h - minimal AES/VDI bindings for pBench
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef PBENCH_GEM_H
#define PBENCH_GEM_H

/*
 * Only what pBench needs.  The calls go through the documented entry points
 * of every pTOS target (trap #2 on m68k, svc 2 on ARM, with the opcode in
 * the first argument register and the parameter block in the second), and
 * nothing here touches hardware.
 */

typedef short WORD;
typedef unsigned short UWORD;
typedef long LONG;

typedef struct {
    WORD x, y, w, h;
} GRECT;

typedef struct {
    void *fd_addr;
    WORD fd_w, fd_h, fd_wdwidth, fd_stand, fd_nplanes, fd_r1, fd_r2, fd_r3;
} MFDB;

typedef struct {
    char *te_ptext;
    char *te_ptmplt;
    char *te_pvalid;
    WORD te_font, te_junk1, te_just, te_color, te_junk2, te_thickness;
    WORD te_txtlen, te_tmplen;
} TEDINFO;

typedef struct {
    WORD ob_next, ob_head, ob_tail;
    UWORD ob_type, ob_flags, ob_state;
    LONG ob_spec;
    WORD ob_x, ob_y, ob_w, ob_h;
} OBJECT;

/* object types, flags and states */
#define G_BOX       20
#define G_STRING    28
#define G_FTEXT     29
#define G_BUTTON    26
#define G_BOXCHAR   27

#define OF_SELECTABLE   0x0001
#define OF_DEFAULT      0x0002
#define OF_EXIT         0x0004
#define OF_LASTOB       0x0020
#define OF_RBUTTON      0x0010
#define OF_EDITABLE     0x0008
#define OS_SELECTED     0x0001
#define OS_OUTLINED     0x0010
#define OS_SHADOWED     0x0020

/* form_dial() flags */
#define FMD_START   0
#define FMD_GROW    1
#define FMD_SHRINK  2
#define FMD_FINISH  3

/* wind_get()/wind_set() fields and gadgets */
#define WF_WORKXYWH 4
#define WF_NAME     2
#define WF_INFO     3
#define WF_FULLXYWH 7
#define WF_UPDATE   11
#define BEG_UPDATE  1
#define END_UPDATE  0
#define NAME        0x0001
#define CLOSER      0x0002
#define FULLER      0x0004
#define MOVER       0x0008
#define INFO        0x0010
#define SIZER       0x0020
#define UPARROW     0x0040
#define DNARROW     0x0080
#define VSLIDE      0x0100
#define LFARROW     0x0200
#define RTARROW     0x0400
#define HSLIDE      0x0800
#define WC_BORDER   0
#define WC_WORK     1

/* graf_mouse() */
#define ARROW       0
#define BUSY_BEE    2
#define M_OFF       256
#define M_ON        257

/* vro_cpyfm() writing mode */
#define S_ONLY      3

/* vswr_mode() */
#define MD_REPLACE  1
#define MD_TRANS    2

#define MAX_DEPTH   8

/* AES */
WORD appl_init(void);
WORD appl_exit(void);
WORD graf_handle(WORD *wchar, WORD *hchar, WORD *wbox, WORD *hbox);
WORD graf_mouse(WORD form, void *fmdata);
WORD wind_create(WORD kind, WORD x, WORD y, WORD w, WORD h);
WORD wind_open(WORD handle, WORD x, WORD y, WORD w, WORD h);
WORD wind_close(WORD handle);
WORD wind_delete(WORD handle);
WORD wind_get(WORD handle, WORD field, WORD *w1, WORD *w2, WORD *w3, WORD *w4);
WORD wind_set_str(WORD handle, WORD field, const char *s);
WORD wind_calc(WORD type, WORD kind, WORD x, WORD y, WORD w, WORD h,
               WORD *ox, WORD *oy, WORD *ow, WORD *oh);
WORD wind_update(WORD what);
WORD form_dial(WORD flag, WORD lx, WORD ly, WORD lw, WORD lh,
               WORD x, WORD y, WORD w, WORD h);
WORD objc_draw(OBJECT *tree, WORD start, WORD depth,
               WORD x, WORD y, WORD w, WORD h);
WORD evnt_mesag_poll(WORD *msg);
WORD evnt_keybd(void);
WORD evnt_timer(WORD lo, WORD hi);

/* VDI */
WORD v_opnvwk(WORD aeshandle, WORD *out);      /* out: 45 words */
void v_clsvwk(WORD handle);
void v_clrwk(WORD handle);
void vs_clip(WORD handle, WORD flag, const GRECT *r);
WORD vswr_mode(WORD handle, WORD mode);
WORD vsf_color(WORD handle, WORD color);
WORD vsf_interior(WORD handle, WORD style);
WORD vsf_style(WORD handle, WORD style);
WORD vsl_color(WORD handle, WORD color);
WORD vst_color(WORD handle, WORD color);
WORD vst_point(WORD handle, WORD point);
WORD vst_effects(WORD handle, WORD effects);
WORD vst_rotation(WORD handle, WORD angle);
void vst_alignment(WORD handle, WORD h, WORD v);
void v_gtext(WORD handle, WORD x, WORD y, const char *s);
void v_justified(WORD handle, WORD x, WORD y, const char *s, WORD len,
                 WORD wordspace, WORD charspace);
void v_pline2(WORD handle, WORD x1, WORD y1, WORD x2, WORD y2);
void v_bar(WORD handle, WORD x1, WORD y1, WORD x2, WORD y2);
void v_ellipse(WORD handle, WORD x, WORD y, WORD xr, WORD yr);
void vr_recfl(WORD handle, WORD x1, WORD y1, WORD x2, WORD y2);
void vro_cpyfm(WORD handle, WORD mode, const GRECT *src, const GRECT *dst);
void v_hide_c(WORD handle);
void v_show_c(WORD handle, WORD reset);
void vq_extnd(WORD handle, WORD flag, WORD *out);
void vql_attributes(WORD handle, WORD *attr);
void vqm_attributes(WORD handle, WORD *attr);
void vqf_attributes(WORD handle, WORD *attr);
void vqt_attributes(WORD handle, WORD *attr);
void vqt_fontinfo(WORD handle, WORD *minade, WORD *maxade, WORD *distances,
                  WORD *maxwidth, WORD *effects);
void vqt_extent(WORD handle, const char *s, WORD *pts);
void vq_mouse(WORD handle, WORD *status, WORD *x, WORD *y);
void vq_key_s(WORD handle, WORD *state);

#endif /* PBENCH_GEM_H */
