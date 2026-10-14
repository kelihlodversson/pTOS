/*
 * vdi_defs.h - Definitions for virtual workstations
 *
 * Copyright 1999 by Caldera, Inc.
 * Copyright 2005-2025 The EmuTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VDIDEFS_H
#define VDIDEFS_H

#include "fonthdr.h"
#include "aesext.h"
#include "vdipb.h"
#include "screen_mode.h"
#include "vdiext.h"

struct vdi_backend_ops;   /* forward declaration -- full definition in vdi_backend.h */

#define HAVE_BEZIER 0           /* switch on bezier capability - entirely untested */

#define EXTENDED_PALETTE (CONF_WITH_VIDEL || CONF_WITH_TT_SHIFTER \
    || defined(MACHINE_RPI) || CONF_WITH_VDI_BACKEND_TRUECOLOR32)

#define TRUECOLOR_MODE  (linea_vars.v_planes > 8)


#if CONF_WITH_VIDEL
# define UDPAT_PLANES   32      /* actually 16, but each plane occupies 2 WORDs */
#elif CONF_WITH_TT_SHIFTER
# define UDPAT_PLANES   8
#else
# define UDPAT_PLANES   4
#endif

/*
 * some VDI opcodes
 */
#define V_OPNWK_OP      1
#define V_CLSWK_OP      2
#define V_OPNVWK_OP     100
#define V_CLSVWK_OP     101


/*
 * some minima and maxima
 */
#define MIN_LINE_STYLE  1       /* for vsl_type() */
#define MAX_LINE_STYLE  7
#define DEF_LINE_STYLE  1

#define MIN_END_STYLE   SQUARED /* for vsl_ends() */
#define MAX_END_STYLE   ROUND
#define DEF_END_STYLE   SQUARED

#define MAX_LINE_WIDTH  40

#define MIN_MARK_STYLE  1       /* for vsm_type() */
#define MAX_MARK_STYLE  6
#define DEF_MARK_STYLE  3

#define MIN_FILL_STYLE  0       /* for vsf_interior() */
#define FIS_HOLLOW      0
#define FIS_SOLID       1
#define FIS_PATTERN     2
#define FIS_HATCH       3
#define FIS_USER        4
#define MAX_FILL_STYLE  4
#define DEF_FILL_STYLE  FIS_HOLLOW

#define MIN_FILL_HATCH  1       /* for vsf_style() when fill style is hatch */
#define MAX_FILL_HATCH  12
#define DEF_FILL_HATCH  1

#define MIN_FILL_PATTERN 1      /* for vsf_style() when fill style is pattern */
#define MAX_FILL_PATTERN 24
#define DEF_FILL_PATTERN 1

#define MIN_WRT_MODE    1       /* for vswr_mode() */
#define MD_REPLACE      1
#define MD_TRANS        2
#define MD_XOR          3
#define MD_ERASE        4
#define MAX_WRT_MODE    4
#define DEF_WRT_MODE    MD_REPLACE

#define MIN_ARC_CT      32      /* min # of points to use when drawing circle/ellipse */
#define MAX_ARC_CT      128     /* max # of points ... (must not exceed MAX_VERTICES) */


/* line ending types */
#define SQUARED     0
#define ARROWED     1
#define ROUND       2

/* aliases for different table positions */
#define xres            linea_vars.DEV_TAB[0]
#define yres            linea_vars.DEV_TAB[1]
#define xsize           linea_vars.DEV_TAB[3]
#define ysize           linea_vars.DEV_TAB[4]
#define numcolors       linea_vars.DEV_TAB[13]

#define DEF_LWID        linea_vars.SIZ_TAB[4]
#define DEF_CHHT        linea_vars.SIZ_TAB[1]
#define DEF_CHWT        linea_vars.SIZ_TAB[0]
#define DEF_MKWD        linea_vars.SIZ_TAB[8]
#define DEF_MKHT        linea_vars.SIZ_TAB[9]
#define MAX_MKWD        linea_vars.SIZ_TAB[10]
#define MAX_MKHT        linea_vars.SIZ_TAB[11]

/* Defines for CONTRL[] */
#define ROUTINE     0
#define N_PTSIN     1
#define N_PTSOUT    2
#define N_INTIN     3
#define N_INTOUT    4
#define SUBROUTINE  5
#define VDI_HANDLE  6

/* text style bits: for vwk->style (and also line-A variable STYLE) */
#define F_THICKEN   1
#define F_LIGHT     2
#define F_SKEW      4
#define F_UNDER     8
#define F_OUTLINE   16
#define F_SHADOW    32

/* Write modes used by the line-A text engine (MD_* minus one). */
#define WM_REPLACE      (MD_REPLACE-1)
#define WM_TRANS        (MD_TRANS-1)
#define WM_XOR          (MD_XOR-1)
#define WM_ERASE        (MD_ERASE-1)

#define OUTLINE_THICKNESS   1   /* outline thickness in pixels (vdi_text.c uses it too) */

/*
 * Text scratch buffer sizing, calculated from the built-in 8x16 font
 * metrics exactly as the original assembler code did (see EmuTOS commits
 * 6d833b0b/e7fa27c8/7d157b8a and the (now deleted) size-calc comments in
 * vdi/arch/m68k/vdi_tblit.S).  SCRATCHBUF_OFFSET is the size of each of the
 * two half-buffers used for rotation/outlining/effects; the buffer must
 * hold two of them (rotation can use either half, outline needs both).
 * Keep the #error guard so a future larger font cannot silently overflow.
 */
#define FORM_HT         16          /* form height of the 8x16 font */
#define MX_CEL_WD       8           /* maximum cell width */
#define SKEW_OFFS       (1+7)       /* left_offset + right_offset of the 8x16 font */

#define CEL2_WW     ((((2*(SKEW_OFFS+MX_CEL_WD))+3+15)/16)*2)
#define CEL2_WH     ((2*(SKEW_OFFS+MX_CEL_WD))+2)
#define CEL2_HH     ((2*FORM_HT)+2)
#define CEL2_HW     ((((2*FORM_HT)+3+15)/16)*2)
#define CEL2_SZ0    (CEL2_WW*CEL2_HH)
#define CEL2_SZ9    (CEL2_WH*CEL2_HW)
#if CEL2_SZ0 >= CEL2_SZ9
# define CEL2_SIZ   (CEL2_SZ0)
#else
# define CEL2_SIZ   (CEL2_SZ9)
#endif
#if CEL2_WW >= CEL2_HW
# define OUT_ADD    (CEL2_WW+2)
#else
# define OUT_ADD    (CEL2_HW+2)
#endif
#define SCRATCHBUF_OFFSET   (CEL2_SIZ+OUT_ADD)
#define SCRATCHBUF_SIZE     (2*212)         /* 424 bytes */
#if SCRATCHBUF_SIZE < (2*SCRATCHBUF_OFFSET)
# error SCRATCHBUF_SIZE is too small for the built-in 8x16 font
#endif

/*
 * Small subset of Vwk data, used by draw_rect_common to hide VDI/Line-A
 * specific details from rectangle & polygon drawing.
 */
typedef struct {
    WORD clip;                  /* polygon clipping on/off */
    WORD multifill;             /* Multi-plane fill flag   */
    UWORD patmsk;               /* Current pattern mask    */
    const UWORD *patptr;        /* Current pattern pointer */
    WORD wrt_mode;              /* Current writing mode    */
    UWORD color;                /* fill color */
} VwkAttrib;


/* type that can be cast from clipping part of Wvk */
typedef struct {
    WORD xmn_clip;              /* Low x point of clipping rectangle    */
    WORD xmx_clip;              /* High x point of clipping rectangle   */
    WORD ymn_clip;              /* Low y point of clipping rectangle    */
    WORD ymx_clip;              /* High y point of clipping rectangle   */
} VwkClip;

#define VDI_CLIP(wvk) ((VwkClip*)(&(wvk->xmn_clip)))


#if CONF_WITH_VDI_16BIT
/* virtual workstation extension, used for VDI Trucolor (16-bit) support */
typedef struct {
    UWORD palette[256];         /* pseudo-palette with pixel value RRRRRGGGGG0BBBBB */
    WORD req_col[256][3];       /* requested colour */
} VwkExt;
#endif

/* Structure to hold data for a virtual workstation */

/* NOTE 1: for backwards compatibility with all versions of TOS, the
 * field 'fill_color' must remain at offset 0x1e, because the line-A
 * flood fill function uses the fill colour from the currently-open
 * virtual workstation, and it is documented that users can provide
 * a fake virtual workstation by pointing CUR_WORK to a 16-element
 * WORD array whose last element contains the fill colour.
 */
typedef struct Vwk_ Vwk;
struct Vwk_ {
    WORD chup;                  /* Character Up vector */
    WORD clip;                  /* Clipping Flag */
    const Fonthead *cur_font;   /* Pointer to current font */
    UWORD dda_inc;              /* Fraction to be added to the DDA */
    WORD multifill;             /* Multi-plane fill flag */
    UWORD patmsk;               /* Current pattern mask */
    UWORD *patptr;              /* Current pattern pointer */
    WORD pts_mode;              /* TRUE if height set in points mode */
    WORD *scrtchp;              /* Pointer to text scratch buffer */
    WORD scrpt2;                /* Offset to large text buffer */
    WORD style;                 /* Current text style */
    WORD t_sclsts;              /* TRUE if scaling up */
    WORD fill_color;            /* Current fill color (PEL value): see NOTE 1 above */
    WORD fill_index;            /* Current fill index */
    WORD fill_per;              /* TRUE if fill area outlined */
    WORD fill_style;            /* Current fill style */
    WORD h_align;               /* Current text horizontal alignment */
    WORD handle;                /* The handle this attribute area is for */
    WORD line_beg;              /* Beginning line endstyle */
    WORD line_color;            /* Current line color (PEL value) */
    WORD line_end;              /* Ending line endstyle */
    WORD line_index;            /* Current line style */
    WORD line_width;            /* Current line width */
    const Fonthead *loaded_fonts; /* Pointer to first loaded font */
    WORD mark_color;            /* Current marker color (PEL value)     */
    WORD mark_height;           /* Current marker height        */
    WORD mark_index;            /* Current marker style         */
    WORD mark_scale;            /* Current scale factor for marker data */
    Vwk *next_work;             /* Pointer to next virtual workstation  */
    WORD num_fonts;             /* Total number of faces available  */
    WORD scaled;                /* TRUE if font scaled in any way   */
    Fonthead scratch_head;      /* Holder for the doubled font data */
    WORD text_color;            /* Current text color (PEL value)   */
    WORD ud_ls;                 /* User defined linestyle       */
    WORD ud_patrn[UDPAT_PLANES*16]; /* User defined pattern             */
    WORD v_align;               /* Current text vertical alignment  */
    WORD wrt_mode;              /* Current writing mode         */
    WORD xfm_mode;              /* Transformation mode requested (NDC) */
    WORD xmn_clip;              /* Low x point of clipping rectangle    */
    WORD xmx_clip;              /* High x point of clipping rectangle   */
    WORD ymn_clip;              /* Low y point of clipping rectangle    */
    WORD ymx_clip;              /* High y point of clipping rectangle   */
#if CONF_WITH_VDI_16BIT
    VwkExt *ext;                /* 16 bit colour management */
#endif
    /* newly added */
    WORD bez_qual;              /* actual quality for bezier curves */
    SCREEN_MODE_DESC mode;      /* backend mode descriptor for this workstation's screen */
    const struct vdi_backend_ops *backend; /* dispatch table selected for `mode`; NULL if none matched */
#if CONF_WITH_VDI_BACKEND_TRUECOLOR
    /*
     * vs_color()/vq_color() pseudo-palette for the truecolor backend
     * (issue #89): MAP_COL-mapped hardware palette index -> the active
     * format's packed pixel value (RGB565 in the low 16 bits, XRGB8888
     * over all 32).  Genuinely per-workstation, matching upstream's
     * VwkExt::palette -- a vs_color() on one workstation must not affect
     * another. Seeded to the backend's defaults by init_wk()
     * (vdi_control.c) whenever a workstation is opened; see
     * vdi_backend_active_vwk() in vdi_backend.h for how the drawing
     * primitives pick which workstation's copy to use.
     */
    ULONG tc_palette[256];
    /*
     * vs_color()'s raw "last requested" values (VDI 0-1000 scale) for the
     * truecolor backend, indexed like REQ_COL/req_col2 by VDI pen number
     * (not MAP_COL-mapped) -- mirrors upstream's VwkExt::req_col so that
     * vq_color(pen,0) ("last requested") and vq_color(pen,1) ("actual",
     * via tc_palette above) stay consistent per-workstation instead of
     * reading the global REQ_COL/req_col2, which stay unpopulated for
     * pens 16-255 on RPi (no VIDEL/TT hardware to seed them). Seeded
     * alongside tc_palette by vdi_truecolor_init_palette().
     */
    WORD tc_req_col[256][3];
#endif
};

typedef struct Rect_ Rect;
struct Rect_
{
    WORD x1,y1;
    WORD x2,y2;
};

typedef struct {
    WORD x1,y1;
    WORD x2,y2;
} Line;

/*
 * the following values are used for 'wrt_mode' in the Vwk structure above
 */
#define WM_REPLACE      (MD_REPLACE-1)
#define WM_TRANS        (MD_TRANS-1)
#define WM_XOR          (MD_XOR-1)
#define WM_ERASE        (MD_ERASE-1)


/*
 * the following line-A variables contain the VDI color palette entries.
 * REQ_COL (linea_vars.REQ_COL) contains the first 16 entries; req_col2
 * contains entries 16-255 (only applicable for 8-plane resolutions).
 * Note that the location of req_col2 is not documented by Atari, but is
 * derived from disassembly of TOS ROMs, and source code for MagiC's VDI.
 */
extern WORD req_col2[240][3];  /* defined in vdi_col.c */

/* External definitions for internal use */
extern WORD flip_y;             /* True if magnitudes being returned */

/* These are still needed for text blitting */
extern const UWORD LINE_STYLE[];
extern const UWORD ROM_UD_PATRN[];
extern const UWORD SOLID;
extern const UWORD HOLLOW;

extern WORD MAP_COL[], REV_MAP_COL[];


BOOL clip_line(Vwk *vwk, Line *line);
void arb_corner(Rect *rect);
void arb_line(Line *line);

int GSX_ENTRY(int op, VDIPB* paramblock);
/* C Support routines */
Vwk * get_vwk_by_handle(WORD);
Vwk * vdi_physical_vwk(void);
UWORD * get_start_addr(const WORD x, const WORD y);
void set_LN_MASK(Vwk *vwk);
void st_fl_ptr(Vwk *);
void gdp_justified(Vwk *);
WORD validate_color_index(WORD colnum);
void set_color16(Vwk *vwk, WORD colnum, WORD *rgb);

/* drawing primitives */
void draw_pline(Vwk *vwk);
void arrow(Vwk *vwk, Point *point, int count);
void draw_rect(const Vwk *vwk, Rect *rect, const UWORD fillcolor);
void polygon(Vwk *vwk, Point *point, int count);
void polyline(Vwk *vwk, Point *point, int count, WORD color);
void wideline(Vwk *vwk, Point *point, int count);

/* common drawing function */
void Vwk2Attrib(const Vwk *vwk, VwkAttrib *attr, const UWORD color);
void draw_rect_common(const VwkAttrib *attr, const Rect *rect);
UWORD *planar_get_start_addr(WORD x, WORD y);
UWORD planar_get_pixel(WORD x, WORD y);
void planar_put_pixel(WORD x, WORD y, UWORD color);
void planar_fill_rect(const VwkAttrib *attr, const Rect *rect);
/* planar_set_color/planar_get_color (vdi_col.c) -- hardware colour-register
 * read/write called directly in non-dispatch, planar-only builds (issue
 * #171), see the vdi_backend_ops comment on set_color/get_color in
 * vdi_backend.h. Only defined in that one build configuration (issue #173)
 * -- a dispatch build reaches palette I/O through planar_backend_ops's
 * patched set_color/get_color slots instead (see vdi_backend_planar.c/
 * vdi_backend.c), and a truecolor-only build never touches planar code at
 * all. */
#if !CONF_WITH_VDI_BACKEND_DISPATCH && !CONF_WITH_VDI_BACKEND_TRUECOLOR
void planar_set_color(Vwk *vwk, WORD pen, WORD *rgb);
void planar_get_color(const Vwk *vwk, WORD pen, WORD *rgb);
#endif
/* planar_set_*_color/planar_get_*_color (vdi_col.c) -- per-shifter-family
 * vdi_backend_ops.set_color/get_color entries (issue #173), one pair
 * patched into planar_backend_ops's slots for the selected shifter family
 * (vdi_backend_planar.c/vdi_backend.c); see the comment on
 * SCREEN_MODE_DESC.shifter (screen_mode.h) */
void planar_set_st_color(Vwk *vwk, WORD pen, WORD *rgb);
void planar_get_st_color(const Vwk *vwk, WORD pen, WORD *rgb);
#if CONF_WITH_STE_SHIFTER
void planar_set_ste_color(Vwk *vwk, WORD pen, WORD *rgb);
void planar_get_ste_color(const Vwk *vwk, WORD pen, WORD *rgb);
#endif
#if CONF_WITH_TT_SHIFTER
void planar_set_tt_color(Vwk *vwk, WORD pen, WORD *rgb);
void planar_get_tt_color(const Vwk *vwk, WORD pen, WORD *rgb);
#endif
#if CONF_WITH_VIDEL
void planar_set_videl_color(Vwk *vwk, WORD pen, WORD *rgb);
void planar_get_videl_color(const Vwk *vwk, WORD pen, WORD *rgb);
#endif
/* truecolor backend primitives (vdi_backend_truecolor.c) -- callable
 * directly in truecolor-only builds, where the dispatcher is compiled out */
UWORD *truecolor_get_start_addr(WORD x, WORD y);
UWORD truecolor_get_pixel(WORD x, WORD y);
void truecolor_put_pixel(WORD x, WORD y, UWORD color);
void truecolor_fill_rect(const VwkAttrib *attr, const Rect *rect);
UWORD truecolor_draw_line(const Line *line, WORD wrt_mode, UWORD color, UWORD linemask);
WORD truecolor_search_right(const VwkClip *clip, WORD x, WORD y, UWORD search_col);
WORD truecolor_search_left(const VwkClip *clip, WORD x, WORD y, UWORD search_col);
void clc_flit(const VwkAttrib *attr, const VwkClip *clipper, const Point *point, WORD vectors, WORD start, WORD end);
void abline (const Line * line, const WORD wrt_mode, UWORD color);
UWORD planar_draw_line(const Line * line, WORD wrt_mode, UWORD color, UWORD linemask);
WORD planar_search_right(const VwkClip * clip, WORD x, WORD y, UWORD search_col);
WORD planar_search_left(const VwkClip * clip, WORD x, WORD y, UWORD search_col);
void contourfill(const VwkAttrib * attr, const VwkClip *clip);

/* initialization of subsystems */
void init_colors(void);
void text_init(void);
void text_init2(Vwk *);
void timer_init(void);
void vdimouse_init(void);
void esc_init(Vwk *);

extern void (*user_wheel)(WORD wheel_number, WORD wheel_amount);   /* user provided mouse wheel vector */

void vdimouse_exit(void);
void timer_exit(void);
void esc_exit(Vwk *);
void mouse_int(UBYTE *buf);   /* mouse interrupt vector */
void wheel_int(UBYTE *buf);   /* wheel interrupt routine */
void mov_cur(WORD new_x, WORD new_y);      /* user button vector */
WORD gchr_key(void);

/* all VDI functions */

void vdi_v_opnwk(Vwk *);            /* 1 */
void vdi_v_clswk(Vwk *);            /* 2 */
void vdi_v_clrwk(Vwk *);            /* 3 */
/* void v_updwk(Vwk *); */          /* 4 - not implemented */
void vdi_v_escape(Vwk *);           /* 5 */

void vdi_v_pline(Vwk *);            /* 6 */
void vdi_v_pmarker(Vwk *);          /* 7 */
void vdi_v_gtext(Vwk *);            /* 8 */
void vdi_v_fillarea(Vwk *);         /* 9 */
/* void vdi_v_cellarray(Vwk *); */  /* 10 - not implemented */

void vdi_v_gdp(Vwk *);              /* 11 */
void vdi_vst_height(Vwk *);         /* 12 */
void vdi_vst_rotation(Vwk *);       /* 13 */
void vdi_vs_color(Vwk *);           /* 14 */
void vdi_vsl_type(Vwk *);           /* 15 */

void vdi_vsl_width(Vwk *);          /* 16 */
void vdi_vsl_color(Vwk *);          /* 17 */
void vdi_vsm_type(Vwk *);           /* 18 */
void vdi_vsm_height(Vwk *);         /* 19 */
void vdi_vsm_color(Vwk *);          /* 20 */

void vdi_vst_font(Vwk *);           /* 21 */
void vdi_vst_color(Vwk *);          /* 22 */
void vdi_vsf_interior(Vwk *);       /* 23 */
void vdi_vsf_style(Vwk *);          /* 24 */
void vdi_vsf_color(Vwk *);          /* 25 */

void vdi_vq_color(Vwk *vwk);        /* 26 */
/* void vdi_vq_cellarray(Vwk *); */ /* 27 - not implemented */
void vdi_v_locator(Vwk *);          /* 28 */
void vdi_v_choice(Vwk *);           /* 30 */

void vdi_v_string(Vwk *);           /* 31 */
void vdi_vswr_mode(Vwk *);          /* 32 */
void vdi_vsin_mode(Vwk *);          /* 33 */
void vdi_vql_attributes(Vwk *);     /* 35 */

void vdi_vqm_attributes(Vwk *);     /* 36 */
void vdi_vqf_attributes(Vwk *);     /* 37 */
void vdi_vqt_attributes(Vwk *);     /* 38 */
void vdi_vst_alignment(Vwk *);      /* 39 */


void vdi_v_opnvwk(Vwk *);           /* 100 */

void vdi_v_clsvwk(Vwk *);           /* 101 */
void vdi_vq_extnd(Vwk *);           /* 102 */
void vdi_v_contourfill(Vwk *);      /* 103 */
void vdi_vsf_perimeter(Vwk *);      /* 104 */
void vdi_v_get_pixel(Vwk *);        /* 105 */

void vdi_vst_effects(Vwk *);        /* 106 */
void vdi_vst_point(Vwk *);          /* 107 */
void vdi_vsl_ends(Vwk *);           /* 108 */
void vdi_vro_cpyfm(Vwk *);          /* 109 */
void vdi_vr_trnfm(Vwk *);           /* 110 */

void vdi_vsc_form(Vwk *);           /* 111 */
void vdi_vsf_udpat(Vwk *);          /* 112 */
void vdi_vsl_udsty(Vwk *);          /* 113 */
void vdi_vr_recfl(Vwk *);           /* 114 */
void vdi_vqin_mode(Vwk *);          /* 115 */

void vdi_vqt_extent(Vwk *);         /* 116 */
void vdi_vqt_width(Vwk *);          /* 117 */
void vdi_vex_timv(Vwk *);           /* 118 */
void vdi_vst_load_fonts(Vwk *);     /* 119 */
void vdi_vst_unload_fonts(Vwk *);   /* 120 */

void vdi_vrt_cpyfm(Vwk *);          /* 121 */
void vdi_v_show_c(Vwk *);           /* 122 */
void vdi_v_hide_c(Vwk *);           /* 123 */
void vdi_vq_mouse(Vwk *);           /* 124 */
void vdi_vex_butv(Vwk *);           /* 125 */

void vdi_vex_motv(Vwk *);           /* 126 */
void vdi_vex_curv(Vwk *);           /* 127 */
void vdi_vq_key_s(Vwk *);           /* 128 */
void vdi_vs_clip(Vwk *);            /* 129 */
void vdi_vqt_name(Vwk *);           /* 130 */

void vdi_vqt_fontinfo(Vwk *);       /* 131 */

#if CONF_WITH_EXTENDED_MOUSE
void vdi_vex_wheelv(Vwk *);         /* 134 */
#endif

#if CONF_WITH_VDI_TEXT_SPEEDUP
void direct_screen_blit(WORD count, WORD *str);
#endif

#if HAVE_BEZIER
/* not in original TOS */
void v_bez_qual(Vwk *);
void v_bez_control(Vwk *);
void v_bez(Vwk *vwk, Point *points, int count);
void v_bez_fill(Vwk *vwk, Point *points, int count);
#endif

#endif                          /* VDIDEF_H */
