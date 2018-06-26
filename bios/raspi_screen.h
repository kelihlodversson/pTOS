/*
 * raspi_screen.h Raspberry PI framebuffer support
 *
 * Copyright (C) 2013-2018 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef RASPI_SCREEN_H
#   define RASPI_SCREEN_H
#   ifdef MACHINE_RPI
void raspi_screen_init(void);
WORD raspi_check_moderez(WORD moderez);
void raspi_get_current_mode_info(UWORD *planes, UWORD *hz_rez, UWORD *vt_rez);
void raspi_setphys(const UBYTE *addr);
const UBYTE *raspi_physbase(void);
WORD raspi_setcolor(WORD colorNum, WORD color);
void raspi_setrez(WORD rez, WORD videlmode);
WORD raspi_vgetmode(void);

UBYTE * raspi_cell_addr(int x, int y);
void raspi_blank_out (int topx, int topy, int botx, int boty);
void raspi_cell_xfer(UBYTE * src, UBYTE * dst);
void raspi_neg_cell(UBYTE * cell);

void initialise_raspi_palette(WORD mode);

#   endif // MACHINE_RPI
#endif // RASPI_SCREEN_H
