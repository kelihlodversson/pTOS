/*
 * vdi_raster.h - shared types for raster-copy backend dispatch
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef VDI_RASTER_H
#define VDI_RASTER_H

#include "portab.h"
#include "vdi_defs.h"

/*
 * Boolean raster-op codes (the 16 possible functions of source and
 * destination bits), shared between the planar blitter emulator
 * (do_blit() in vdi_raster.c) and the packed-truecolor backend's
 * word-at-a-time copy (truecolor_raster_copy() in
 * vdi_backend_truecolor.c).
 */
#define BM_ALL_WHITE   0
#define BM_S_AND_D     1
#define BM_S_AND_NOTD  2
#define BM_S_ONLY      3
#define BM_NOTS_AND_D  4
#define BM_D_ONLY      5
#define BM_S_XOR_D     6
#define BM_S_OR_D      7
#define BM_NOT_SORD    8
#define BM_NOT_SXORD   9
#define BM_NOT_D      10
#define BM_S_OR_NOTD  11
#define BM_NOT_S      12
#define BM_NOTS_OR_D  13
#define BM_NOT_SANDD  14
#define BM_ALL_BLACK  15

/* 76-byte line-A BITBLT struct passing parameters to bitblt */
struct blit_frame {
    WORD b_wd;          /* +00 width of block in pixels */
    WORD b_ht;          /* +02 height of block in pixels */
    WORD plane_ct;      /* +04 number of consecutive planes to blt */
    UWORD fg_col;       /* +06 foreground color (logic op table index:hi bit) */
    UWORD bg_col;       /* +08 background color (logic op table index:lo bit) */
    UBYTE op_tab[4];    /* +10 logic ops for all fore and background combos */
    WORD s_xmin;        /* +14 minimum X: source */
    WORD s_ymin;        /* +16 minimum Y: source */
    UWORD * s_form;     /* +18 source form base address */
    WORD s_nxwd;        /* +22 offset to next word in line  (in bytes) */
    WORD s_nxln;        /* +24 offset to next line in plane (in bytes) */
    WORD s_nxpl;        /* +26 offset to next plane from start of current plane */
    WORD d_xmin;        /* +28 minimum X: destination */
    WORD d_ymin;        /* +30 minimum Y: destination */
    UWORD * d_form;     /* +32 destination form base address */
    WORD d_nxwd;        /* +36 offset to next word in line  (in bytes) */
    WORD d_nxln;        /* +38 offset to next line in plane (in bytes) */
    WORD d_nxpl;        /* +40 offset to next plane from start of current plane */
    UWORD * p_addr;     /* +42 address of pattern buffer   (0:no pattern) */
    WORD p_nxln;        /* +46 offset to next line in pattern  (in bytes) */
    WORD p_nxpl;        /* +48 offset to next plane in pattern (in bytes) */
    WORD p_mask;        /* +50 pattern index mask */

    /* these frame parameters are internally set */
    WORD p_indx;        /* +52 initial pattern index */
    UWORD * s_addr;     /* +54 initial source address */
    WORD s_xmax;        /* +58 maximum X: source */
    WORD s_ymax;        /* +60 maximum Y: source */
    UWORD * d_addr;     /* +62 initial destination address */
    WORD d_xmax;        /* +66 maximum X: destination */
    WORD d_ymax;        /* +68 maximum Y: destination */
    WORD inner_ct;      /* +70 blt inner loop initial count */
    WORD dst_wr;        /* +72 destination form wrap (in bytes) */
    WORD src_wr;        /* +74 source form wrap (in bytes) */
};

/*
 * common settings needed both by VDI and line-A raster operations, but
 * being given through different means.
 */
struct raster_t {
    VwkClip *clipper;
    int clip;
    int multifill;
    int transparent;

    /*
     * Raw request parameters, alongside the op_tab/fg_col/bg_col encoding
     * cpy_raster() derives for the planar per-plane blitter emulator --
     * the packed-truecolor backend works a whole pixel at a time and
     * needs the original values instead of that per-plane encoding (see
     * truecolor_raster_copy() in vdi_backend_truecolor.c). mode is the
     * raw VDI raster-op index for an opaque copy, or one of
     * MD_TRANS/MD_REPLACE/MD_XOR/MD_ERASE for a transparent copy;
     * fg_col/bg_col are MAP_COL-mapped hardware palette indices,
     * meaningful only for a transparent copy.
     */
    WORD mode;
    UWORD fg_col;
    UWORD bg_col;
};

/*
 * Planar raster-copy backend: dispatches info (already fully set up by
 * cpy_raster()) to the hardware blitter or its C/assembler emulation.
 * Exposed here so vdi_backend_planar.c's ops table can reference it, and
 * so cpy_raster() can call it directly when CONF_WITH_VDI_TRUECOLOR is
 * off (see the comment on vdi_backend_ops in vdi_backend.h).
 */
void planar_raster_copy(struct raster_t *raster, struct blit_frame *info);

#endif /* VDI_RASTER_H */
