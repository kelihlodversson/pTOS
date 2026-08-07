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

/*
 * Draws the hardware cursor via the VideoCore mailbox. Returns FALSE if the
 * firmware doesn't implement the cursor property tags (e.g. under QEMU),
 * so callers can fall back to a software-drawn cursor.
 */
BOOL raspi_hw_cur_display(Mcdb *sprite, WORD x, WORD y);

#   endif /* MACHINE_RPI */
#endif /* RASPI_MOUSE_H */
