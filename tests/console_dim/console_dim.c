/*
 * console_dim.c - regression test for issue #235
 *
 * Verifies Ssystem(S_CONSOLE_DIM, ...), a pTOS-only extension (see
 * bdos/ssystem.h) that reports the console's text-cell dimensions
 * without going through the m68k-only Line-A trap -- the portable
 * replacement getwh()/getht() (cli/cmdasm.S, cli/cmdgetwh.c) need on
 * architectures, like ARM, that have no Line-A at all.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>     /* Ssystem() */

void test_console_dim(void);

/*
 * Not in libcmini's <mint/mintbind.h>: it only lists the modes that are
 * part of MiNT's documented Ssystem() protocol, and this one deliberately
 * isn't (see bdos/ssystem.h for why -- a negative mode value, same idiom
 * as S_INQUIRE, so it can never collide with a real MiNT mode).
 */
#define S_CONSOLE_DIM ((short)0xfffe)

void test_console_dim(void)
{
    long dim = -1;
    long rc;
    unsigned short width, height;

    ptest_begin("console_dim (#235)");

    rc = Ssystem(S_CONSOLE_DIM, 0, (long)&dim);
    ptest_assert_msg(rc == 0, "Ssystem(S_CONSOLE_DIM) did not return E_OK");

    width = (unsigned short)(dim & 0xffff);
    height = (unsigned short)((dim >> 16) & 0xffff);

    /* Generous bounds: real/emulated screens run from ST low-res
     * (40x25) up to TT high-res in a small font (213x160-ish); anything
     * outside this range means the wrong bits ended up in the wrong
     * half of the LONG, not a real screen. */
    ptest_assert_msg(width > 0 && width <= 1000,
                     "console width out of plausible range");
    ptest_assert_msg(height > 0 && height <= 500,
                     "console height out of plausible range");

    /* A NULL destination pointer must be rejected, not crash. */
    rc = Ssystem(S_CONSOLE_DIM, 0, 0L);
    ptest_assert_msg(rc < 0, "Ssystem(S_CONSOLE_DIM) with a NULL pointer "
                              "should fail, not succeed");

    ptest_pass();
}
