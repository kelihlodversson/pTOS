/*
 * EmuTOS AES
 *
 * Copyright (C) 2002-2017 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef GEMEVLIB_H
#define GEMEVLIB_H

extern WORD     gl_dclick;
extern WORD     gl_ticktime;


WORD ev_block(WORD code, LONG lvalue);
UWORD ev_button(WORD bflgclks, UWORD bmask, UWORD bstate, WORD rets[]);
UWORD ev_mouse(MOBLK *pmo, WORD rets[]);
void ev_mesag(WORD *mebuff);
void ev_timer(LONG count);
WORD ev_multi(WORD flags, MOBLK *pmo1, MOBLK *pmo2, LONG tmcount,
              LONG buparm, WORD *mebuff, WORD prets[]);
WORD ev_dclick(WORD rate, WORD setit);

/*
 * combine clicks/mask/state into LONG
 *
 * downorup() (geminput.c) decodes this value with explicit byte
 * shifts, so it must be built the same way here: a union of a
 * {WORD,BYTE,BYTE} struct and a LONG is endian-dependent and gives
 * the wrong layout on little-endian targets (same bug class as
 * kb_last in bios/ikbd.c, see issue #185).
 */
static __inline__ LONG combine_cms(WORD clicks,WORD mask,WORD state)
{
    return (((LONG)clicks & 0xffffL) << 16) | (((LONG)mask & 0xff) << 8) | ((LONG)state & 0xff);
}
#endif
