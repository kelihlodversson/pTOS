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
 * trap.  On ARM (and any future non-m68k port) lineA does not exist at
 * all, so this file provides two different ARM implementations
 * depending on which build this is:
 *
 * - The ROM build (compiled straight into the kernel's own address
 *   space, see cli/build.mk) reads the same information the VDI keeps
 *   up to date directly out of linea_vars.
 * - The standalone build (a separate userland program, see
 *   ../cli/Makefile) has no access to the kernel's own memory, so it
 *   goes through Ssystem(S_CONSOLE_DIM, ...) (bdos/ssystem.h) instead --
 *   a real syscall added (kelihlodversson/pTOS#235) specifically to
 *   give a standalone ARM program a portable way to learn this.
 */

#include "cmd.h"

#ifdef STANDALONE_CONSOLE

#include <mint/mintbind.h>     /* Ssystem() */

/*
 * Not in libcmini's <mint/mintbind.h>: it only lists the modes that are
 * part of MiNT's documented Ssystem() protocol, and this one
 * deliberately isn't (see bdos/ssystem.h). Mirrors bdos/ssystem.h's
 * struct console_dim -- not shared via a header since this is
 * userland/libcmini code, not kernel code (same as
 * tests/console_dim/console_dim.c).
 */
#define S_CONSOLE_DIM ((short)0xfffe)
struct console_dim {
    UWORD width;    /* columns */
    UWORD height;   /* rows */
};

/*
 * getwh - return columns and rows packed into a ULONG
 *
 * high word: columns - 1
 * low  word: rows - 1
 *
 * Callers add 1 to each half to obtain the actual dimensions -- matches
 * the m68k/ROM-ARM implementations' convention, both of which pack the
 * same "last valid index" values straight out of v_cel_mx/v_cel_my.
 * Ssystem() is unconditionally available on ARM (bdos/bdosmain.c's
 * osif(), #ifdef __arm__), so a failed call here means something is
 * genuinely wrong; fall back to 0 (screen_cols = screen_rows = 1 in
 * cmdmain.c) rather than returning garbage.
 */
ULONG getwh(void)
{
    struct console_dim dim;

    if (Ssystem(S_CONSOLE_DIM,(long)&dim,(long)sizeof(dim)) != (long)sizeof(dim))
        return 0;

    return ((ULONG)(UWORD)(dim.width-1) << 16) | (UWORD)(dim.height-1);
}

/*
 * getht - return the cell height in pixels
 *
 * No portable accessor for this exists yet -- S_CONSOLE_DIM only
 * covers the text-cell width/height getwh() needs (screen_cols/
 * screen_rows in cmdmain.c), not the font's pixel height (v_cel_ht).
 * The only caller (cmdint.c's "MODE CON" status display) just reports
 * this value, never computes with it, so a fixed fallback is a
 * reasonable stopgap: 8 matches pTOS's default font height (see
 * bios/font.c's 8x8/8x16 choices) rather than an arbitrary number.
 */
WORD getht(void)
{
    return 8;
}

#else /* ROM build: read the kernel's own linea_vars directly */

#include "../bios/lineavars.h"

ULONG getwh(void)
{
    return ((ULONG)linea_vars.v_cel_mx << 16) | linea_vars.v_cel_my;
}

WORD getht(void)
{
    return (WORD)linea_vars.v_cel_ht;
}

#endif /* STANDALONE_CONSOLE */
