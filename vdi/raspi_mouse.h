/*
 * raspi_mouse.h Raspberry PI hw mouse sprite support
 *
 * Copyright (C) 2013-2018 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef RASPI_MOUSE_H
#   define RASPI_MOUSE_H
#   ifdef MACHINE_RPI

/* Mouse / sprite structure */
typedef struct Mcdb_ Mcdb;
struct Mcdb_ {
        WORD    xhot;
        WORD    yhot;
        WORD    planes;
        WORD    bg_col;
        WORD    fg_col;
        UWORD   maskdata[32];   /* mask & data are interleaved */
};
void raspi_hw_cur_display(Mcdb *sprite, WORD x, WORD y);

#   endif // MACHINE_RPI
#endif // RASPI_MOUSE_H
