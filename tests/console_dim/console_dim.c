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

/* Mirrors bdos/ssystem.h's struct console_dim -- not shared via a header
 * since this is userland/libcmini code, not kernel code. */
struct console_dim {
    unsigned short width;        /* columns */
    unsigned short height;       /* rows */
    unsigned short cell_height;  /* font cell height, in pixels */
};

void test_console_dim(void)
{
    struct console_dim dim;
    struct {
        struct console_dim base;
        unsigned char future[4];       /* simulates a newer, larger caller struct */
    } oversized;
    long rc;

    ptest_begin("console_dim (#235)");

    /*
     * Ssystem() itself is currently ARM-only (bdos/bdosmain.c's osif(),
     * #ifdef __arm__) -- every mode, not just this one, returns EINVFN
     * unconditionally on m68k. Unlike stack_alignment (ARM-only only in
     * the sense that its bug never manifested elsewhere), this is an
     * explicit "not implemented on this architecture" the harness
     * should skip past rather than assert success against.
     */
    if (Ssystem(S_INQUIRE, 0, 0) != 0) {
        ptest_pass();
        return;
    }

    rc = Ssystem(S_CONSOLE_DIM, (long)&dim, (long)sizeof(dim));
    ptest_assert_msg(rc == (long)sizeof(dim),
                     "Ssystem(S_CONSOLE_DIM) did not report the full struct written");

    /* Generous bounds: real/emulated screens run from ST low-res (40x25)
     * up to TT high-res in a small font (213x160-ish); anything outside
     * this range means garbage, not a real screen. */
    ptest_assert_msg(dim.width > 0 && dim.width <= 1000,
                     "console width out of plausible range");
    ptest_assert_msg(dim.height > 0 && dim.height <= 500,
                     "console height out of plausible range");
    /* pTOS fonts are 6x6, 8x8 or 8x16 (bios/font.c) -- generous bounds
     * around that rather than an exact match, in case a future font is
     * added. */
    ptest_assert_msg(dim.cell_height > 0 && dim.cell_height <= 64,
                     "console cell height out of plausible range");

    /* A caller struct larger than the kernel's own must not overrun --
     * only sizeof(struct console_dim) bytes get written. Fill the
     * trailing bytes with a sentinel first: checking just the return
     * value wouldn't catch a kernel that overwrites them but still
     * reports the smaller count. */
    {
        unsigned i;
        for (i = 0; i < sizeof(oversized.future); i++)
            oversized.future[i] = 0xaa;
    }
    rc = Ssystem(S_CONSOLE_DIM, (long)&oversized, (long)sizeof(oversized));
    ptest_assert_msg(rc == (long)sizeof(struct console_dim),
                     "Ssystem(S_CONSOLE_DIM) wrote more than it knows about");
    {
        unsigned i;
        for (i = 0; i < sizeof(oversized.future); i++)
            ptest_assert_msg(oversized.future[i] == 0xaa,
                             "Ssystem(S_CONSOLE_DIM) overran into the "
                             "caller's trailing bytes");
    }

    /* size == -1 queries sizeof(struct console_dim) as the running
     * kernel implements it, without writing anything -- arg1 is
     * ignored, so NULL is fine here. This is how a caller discovers
     * which struct version it's talking to up front. */
    rc = Ssystem(S_CONSOLE_DIM, 0, -1L);
    ptest_assert_msg(rc == (long)sizeof(struct console_dim),
                     "Ssystem(S_CONSOLE_DIM) size query did not report "
                     "sizeof(struct console_dim)");

    /* A NULL pointer or non-positive size (other than the -1 query
     * above) must be rejected, not crash. */
    rc = Ssystem(S_CONSOLE_DIM, 0, (long)sizeof(dim));
    ptest_assert_msg(rc < 0,
                     "Ssystem(S_CONSOLE_DIM) with a NULL pointer should fail");

    rc = Ssystem(S_CONSOLE_DIM, (long)&dim, 0);
    ptest_assert_msg(rc < 0,
                     "Ssystem(S_CONSOLE_DIM) with size 0 should fail");

    ptest_pass();
}
