/*
 * legacy_tpa.c - regression test for the TOS contract of a classic PRG
 *
 * On the 68k, with every process in an address space of its own, a program in
 * the PRG format is started the way TOS starts it: its TPA is the free memory,
 * its stack pointer is somewhere in the TPA (the startup code chose it, as it
 * always did), and Mshrink() takes back what the program does not need.  (A
 * program in ELF has a private stack and no such TPA, and an ARM or x86-64
 * program is always one.)  This tests a harness that is a PRG (TEST_IS_PRG:
 * the m68k toolchains that make one; the bare m68k-elf makes an ELF); elsewhere
 * it passes without testing anything.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <mint/basepage.h>

#define EGSBF   (-67L)

void test_legacy_tpa(void);

void test_legacy_tpa(void)
{
    ptest_begin("legacy_tpa");

#if defined(__m68k__) && defined(TEST_WITH_USER_ASPACE) && defined(TEST_IS_PRG)
    {
        BASEPAGE *bp = _base;
        long sp_now, in_use;

        __asm__ __volatile__("move.l %%sp,%0" : "=r" (sp_now));
        ptest_assert_msg(sp_now > (long)bp && sp_now <= (long)bp->p_hitpa,
                         "the stack is not in the TPA");

        /* the program keeps what it uses: the stack, and the frames of its callers
         * above it, which end at the old top of the TPA (a margin of two pages; if
         * that is no smaller than the TPA, there is nothing to take back) */
        in_use = (sp_now - (long)bp + 8192L) & ~4095L;
        if (in_use < (long)bp->p_hitpa - (long)bp)
        {
            long rc = Mshrink(bp, in_use);

            /* (EGSBF: the TPA is smaller than p_hitpa says already, the startup code
             * having taken back what it did not need) */
            ptest_assert_msg(rc == 0 || rc == EGSBF, "Mshrink() to the size in use failed");
            ptest_assert_msg(Mshrink(bp, in_use) == rc, "Mshrink() to the same size again failed");
        }

        /* what is not the program's own TPA, or cannot grow, is refused */
        ptest_assert_msg(Mshrink(bp, 0x10000000L) == EGSBF, "Mshrink() growing the TPA was not refused");
        ptest_assert_msg(Mshrink((void *)bp->p_env, 16L) < 0, "Mshrink() of the environment was not refused");

        /* and the stack still works after the shrink */
        {
            volatile char buf[256];
            int i;

            for (i = 0; i < 256; i++)
                buf[i] = (char)i;
            ptest_assert_msg(buf[255] == (char)255, "the stack does not work");
        }
    }
#endif
    ptest_pass();
}
