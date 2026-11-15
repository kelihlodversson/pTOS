/*
 * gemgrlib.h - header for EmuTOS AES Graphics Library functions
 *
 * Copyright (C) 2002-2021 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#include "aescontext.h"
#ifndef GEMGRLIB_H
#define GEMGRLIB_H
#include "gsxdefs.h"

void gr_stepcalc(AES_RENDER_PARAM WORD orgw, WORD orgh, GRECT *pt, WORD *pcx, WORD *pcy,
                 WORD *pcnt, WORD *pxstep, WORD *pystep);

void gr_rubwind(AES_RENDER_PARAM WORD xorigin, WORD yorigin, WORD wmin, WORD hmin,
                GRECT *poff, WORD *pwend, WORD *phend);
void gr_rubbox(AES_RENDER_PARAM WORD xorigin, WORD yorigin, WORD wmin, WORD hmin,
               WORD *pwend, WORD *phend);
void gr_dragbox(AES_RENDER_PARAM WORD w, WORD h, WORD sx, WORD sy, GRECT *pc, WORD *pdx, WORD *pdy);
void gr_2box(AES_RENDER_PARAM WORD flag1, WORD cnt, GRECT *pt, WORD xstep, WORD ystep, WORD flag2);
void gr_movebox(AES_RENDER_PARAM WORD w, WORD h, WORD srcx, WORD srcy, WORD dstx, WORD dsty);
void gr_growbox(AES_RENDER_PARAM GRECT *po, GRECT *pt);
void gr_shrinkbox(AES_RENDER_PARAM GRECT *po, GRECT *pt);
WORD gr_watchbox(AES_FORM_PARAM OBJECT *tree, WORD obj, WORD instate, WORD outstate);
WORD gr_slidebox(AES_RENDER_PARAM OBJECT *tree, WORD parent, WORD obj, WORD isvert);
void gr_mouse(WORD mode, MFORM *maddr);
void gr_mkstate(WORD *pmx, WORD *pmy, WORD *pmstat, WORD *pkstat);


#endif
