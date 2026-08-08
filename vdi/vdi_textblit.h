/*
 * vdi_textblit.h - Internal data for text blit implementation
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */



#ifndef VDI_TEXTBLIT_H
#define VDI_TEXTBLIT_H

#include "portab.h"

/*
 * the following structure mimics the format of the stack frame
 * containing the local variables used by the lower-level assembler
 * routines.  comments are taken from the assembler source.
 *
 * this could (and should) be cleaned up at some time, but any changes
 * MUST be synchronised with corresponding changes to the assembler code.
 */
typedef struct {
                        /* temporary working variables */
    WORD unused5;           /* was chup_flag (=CHUP-1800) */
    WORD blt_flag;
    WORD unused1;           /* was tmp_style */
                        /* working copies of the clipping variables */
    WORD unused6;           /* was YMX_CLIP */
    WORD unused7;           /* was XMX_CLIP */
    WORD unused8;           /* was YMN_CLIP */
    WORD unused9;           /* was XMN_CLIP */
    WORD unused10;          /* was CLIP */
                        /* working copies of often-used globals */
    WORD unused11;          /* was CHUP */
    WORD DESTY;
    WORD DELY;
    WORD DESTX;
    WORD DELX;
    WORD unused3;           /* was SKEWMASK */
    WORD WRT_MODE;
    WORD STYLE;
                        /* temps for arbitrary text scaling */
    WORD swap_tmps;         /* nonzero if temps are swapped */
    WORD tmp_dely;          /* temp DELY,DELX used by scaling */
    WORD tmp_delx;
                        /* colour, planes, etc */
    WORD nextwrd;           /* offset to next word in same plane */
    WORD nbrplane;          /* # planes */
    WORD forecol;           /* foreground colour */
                        /* masks for special effects */
    WORD thknover;          /* overflow for word thicken */
    WORD skew_msk;          /* rotate this to check shift */
    WORD lite_msk;          /* AND with this to get light effect */
    WORD ambient;           /* background colour */
    WORD smear;             /* amount to increase width */
                        /* vectors that may contain twoptable entries */
    void *litejpw;          /* vector for word function after lighten */
    void *thknjpw;          /* vector for word function after thicken */
                        /* vectors that may contain a toptable entry */
    void *litejpwf;         /* vector for word fringe function after lighten */
    void *thknjpwf;         /* vector for word fringe function after thicken */
    void *skewjmp;          /* vector for function after skew */
    void *litejmp;          /* vector for function after lighten */
    void *thknjmp;          /* vector for function after thicken */
                        /* other general-usage stuff */
    WORD wrd_cnt;           /* number inner loop words for left/right */
    WORD shif_cnt;          /* shift count for use by left/right shift routines */
    WORD rota_msk;          /* overlap between words in inner loop */
    WORD left_msk;          /* fringes of destination to be affected */
    WORD rite_msk;
    WORD thk_msk;           /* right fringe mask, before thicken */
    WORD src_wthk;
    WORD src_wrd;           /* # full words between fringes (source) (before thicken) */
    WORD dest_wrd;          /* # full words between fringes (destination) */
    WORD tddad;             /* destination dot address */
    WORD tsdad;             /* source dot address (pixel address, 0-15 word offset) */
    WORD height;            /* height of area in pixels */
    WORD width;             /* width of area in pixels */
    WORD d_next;            /* width of dest form (_v_lin_wr formerly used) */
    WORD s_next;            /* width of source form (formerly s_width) */
    UBYTE *dform;           /* start of destination form */
    UBYTE *sform;           /* start of source form */
    WORD unused2;           /* was buffc */
    WORD unused4;           /* was buffb */
    WORD buffa;             /* for clip & prerotate blt */
} LOCALVARS;

/* here we should have the preprocessor verify the length of LOCALVARS */
/*
 * assembler functions in vdi_tblit.S (m68k/coldfire)
 */
void normal_blit(LOCALVARS *vars, UBYTE *src, UBYTE *dst);

/*
 * planar text blit: output the current glyph to a bitplane screen.
 * shared implementation, called directly when truecolor is off and via
 * the planar backend's ops table when it is on (see vdi_backend.h)
 */
void planar_text_blit(LOCALVARS *vars);
void truecolor_text_blit(LOCALVARS *vars);

/*
 * portable C implementations, provided on every target
 */
void outline(LOCALVARS *vars);
void rotate(LOCALVARS *vars);   /* actually local, but non-static improves performance */
void scale(LOCALVARS *vars);

#endif /* VDI_TEXTBLIT_H */
