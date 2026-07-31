/*
 * cmdgetwh.c - portable screen-dimension helpers for EmuCON
 *
 * Copyright (C) 2013-2017 The EmuTOS development team
 *
 * Authors:
 *  RFB    Roger Burrows
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * On m68k these functions are implemented in cmdasm.S using the lineA
 * trap.  On ARM (and any future non-m68k port) lineA does not exist,
 * so we read the same information directly from the linea_vars struct
 * that the VDI keeps up to date.
 */

#include "cmd.h"
#include "../bios/lineavars.h"

/*
 * getwh - return columns and rows packed into a ULONG
 *
 * high word: v_cel_mx (columns - 1)
 * low  word: v_cel_my (rows - 1)
 *
 * Callers add 1 to each half to obtain the actual dimensions.
 */
ULONG getwh(void)
{
    return ((ULONG)linea_vars.v_cel_mx << 16) | linea_vars.v_cel_my;
}

/*
 * getht - return the cell height in pixels
 */
WORD getht(void)
{
    return (WORD)linea_vars.v_cel_ht;
}
