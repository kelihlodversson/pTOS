/*
 * rsconf_abi.c - regression test for the Rsconf() vector's classic ABI
 *
 * The Rsconf field of the Bconmap() tables (and the rsconfptr vector it is
 * loaded into) is a frozen TOS vector: a program may call it directly with
 * TOS's classic ABI, six WORDs packed one after the other on the stack.  The
 * kernel's own driver functions are ordinary C functions in the kernel's ABI,
 * so the table has to point at a classic-ABI entry point; if it points at the
 * driver itself the six arguments are read from the wrong places.
 *
 * The test fetches Rsconf from the Bconmap() table of the mapped device and
 * calls it by hand with the classic frame, asking for the current baud rate
 * (baud = -2), and compares the answer with the Rsconf() XBIOS call.  Only
 * machines with Bconmap() (those with an SCC or TT MFP) have such a table;
 * elsewhere there is nothing to test and it passes trivially.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>

void test_rsconf_abi(void);

#ifdef __m68k__
/*
 * call fn(a, b, c, d, e, f) with six packed WORDs, as TOS does.  Like any
 * classic caller it expects d2/a2 back unchanged from the callee, but does
 * not rely on that: it saves and restores them itself.
 */
static unsigned long call_classic(unsigned long fn, short a, short b, short c,
                                  short d, short e, short f)
{
    short frame[6];
    register unsigned long target __asm__("a0") = fn;
    register unsigned long result __asm__("d0");

    frame[0] = a; frame[1] = b; frame[2] = c;
    frame[3] = d; frame[4] = e; frame[5] = f;

    __asm__ volatile (
        "movem.l %%d2/%%a2,-(%%sp)\n\t"
        "move.w 10(%2),-(%%sp)\n\t"
        "move.w 8(%2),-(%%sp)\n\t"
        "move.w 6(%2),-(%%sp)\n\t"
        "move.w 4(%2),-(%%sp)\n\t"
        "move.w 2(%2),-(%%sp)\n\t"
        "move.w 0(%2),-(%%sp)\n\t"
        "jsr (%%a0)\n\t"
        "lea 12(%%sp),%%sp\n\t"
        "movem.l (%%sp)+,%%d2/%%a2"
        : "=d" (result), "+a" (target)
        : "a" (frame)
        : "d1", "a1", "cc", "memory");
    return result;
}
#endif

void test_rsconf_abi(void)
{
    ptest_begin("rsconf_abi");

#ifdef __m68k__
    {
        long root = Bconmap(-2);

        if (root > 0x100)               /* a pointer: the machine has Bconmap() */
        {
            unsigned long *bconmap = (unsigned long *)root;
            unsigned long *maptab = (unsigned long *)bconmap[0];
            short mapped;
            long expected, got;

            mapped = *(short *)((char *)bconmap + 6);   /* WORD mapped_device */
            /* each MAPTAB is six longs: Bconstat, Bconin, Bcostat, Bconout, Rsconf, Iorec */
            maptab += (mapped - 6) * 6;
            expected = Rsconf(-2, -1, -1, -1, -1, -1);
            got = (long)call_classic(maptab[4], -2, -1, -1, -1, -1, -1);
            ptest_assert_msg(got == expected,
                             "Rsconf() called with the classic ABI through "
                             "the Bconmap table returned the wrong baud rate");
        }
    }
#endif

    ptest_pass();
}
