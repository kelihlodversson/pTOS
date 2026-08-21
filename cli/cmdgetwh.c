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

#include <stddef.h>             /* offsetof() */
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
    UWORD width;        /* columns */
    UWORD height;       /* rows */
    UWORD cell_width;   /* font cell width, in pixels -- always 8 */
    UWORD cell_height;  /* font cell height, in pixels */
};

/*
 * Console dimensions can't change for the lifetime of this process (there
 * is no resize event on a TOS console), so getwh() and getht() share one
 * Ssystem(S_CONSOLE_DIM) round trip instead of each making their own.
 * dim_cached is filled in by the first call to either function;
 * rc_cached caches Ssystem()'s return value alongside it, since both
 * functions need to know how many bytes actually came back, not just
 * the struct contents -- see getwh()'s "needed" comment below. -1 marks
 * "not fetched yet": Ssystem() itself can never return a negative value
 * that low (GEMDOS error codes bottom out around -32, e.g. EINVFN), so
 * there's no ambiguity between "not fetched" and a real error.
 */
static struct console_dim dim_cached;
static LONG rc_cached = -1;

static LONG get_console_dim(void)
{
    if (rc_cached < 0)
        rc_cached = Ssystem(S_CONSOLE_DIM,(long)&dim_cached,(long)sizeof(dim_cached));

    return rc_cached;
}

/*
 * getwh - return columns and rows packed into a ULONG
 *
 * high word: columns - 1
 * low  word: rows - 1
 *
 * Callers add 1 to each half to obtain the actual dimensions -- matches
 * the m68k/ROM-ARM implementations' convention, both of which pack the
 * same "last valid index" values straight out of v_cel_mx/v_cel_my.
 *
 * Only requires enough bytes back to cover width/height, not the whole
 * (possibly larger) struct console_dim this file was built against: the
 * struct's size-versioned truncation contract (bdos/ssystem.h) means a
 * standalone binary can outlive the exact kernel it was built against,
 * and an older kernel that only ever implemented width/height (pre-
 * #238/#239) would otherwise get treated as a failure here even though
 * it filled in everything this function actually reads.
 *
 * Ssystem() is unconditionally available on ARM (bdos/bdosmain.c's
 * osif(), #ifdef __arm__), so returning too few bytes even for that
 * means something is genuinely wrong; fall back to 0 (screen_cols =
 * screen_rows = 1 in cmdmain.c) rather than returning garbage. Also
 * guard against a width/height of 0 specifically: subtracting 1 from
 * either would underflow to 0xffff, and cmdmain.c's screen_cols/
 * linesize computation (which adds 1 back) would then wrap to a huge
 * UWORD instead of failing safely.
 */
ULONG getwh(void)
{
    LONG needed = offsetof(struct console_dim,height) + sizeof(dim_cached.height);

    if (get_console_dim() < needed)
        return 0;
    if (!dim_cached.width || !dim_cached.height)
        return 0;

    return ((ULONG)(UWORD)(dim_cached.width-1) << 16) | (UWORD)(dim_cached.height-1);
}

/*
 * getht - return the cell height in pixels
 *
 * Same cached Ssystem() result as getwh(), just reading a different
 * field -- see bdos/ssystem.h's struct console_dim. Unlike getwh(),
 * needs the full current struct (cell_height is its last field), so
 * this still checks for an exact sizeof() match rather than getwh()'s
 * looser threshold. Falls back to 8 (pTOS's default font height, see
 * bios/font.c's 8x8/8x16 choices) on the same "genuinely wrong" failure
 * case getwh() guards against, since the only caller (cmdint.c's "MODE
 * CON" status display) just reports this value rather than computing
 * with it -- a plausible fallback is fine where getwh()'s 0 fallback
 * wouldn't be.
 */
WORD getht(void)
{
    if (get_console_dim() != (long)sizeof(dim_cached))
        return 8;

    return (WORD)dim_cached.cell_height;
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
