/*
 * sysconf.c - regression test for Sysconf() (GEMDOS 0x122)
 *
 * The numbers are the kernel's own, so what is checked is that they are
 * consistent and plausible, plus the ones that are fixed: the highest number,
 * the page size, the tick, the command tail.  Where Sysconf() does not exist
 * (the Atari ROMs) the suite passes without testing anything.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>

#define EINVFN  (-32L)

void test_sysconf(void);

void test_sysconf(void)
{
    long last, phys, avail, n;

    ptest_begin("sysconf");

    last = Sysconf(-1);
    if (last == EINVFN)
    {
#if defined(__arm__) || defined(__x86_64__)
        /* these have it (CONF_WITH_SSYSTEM): a dispatch that is gone is a failure */
        ptest_fail("Sysconf() is missing");
#else
        ptest_pass();               /* no Sysconf() on the Atari ROMs */
#endif
        return;
    }

    ptest_assert_msg(last == 10, "Sysconf(-1) is not 10");

    /* every number up to the highest answers, none beyond it does (the bound is
     * the known 10, not what Sysconf(-1) said: a wrong answer must not make the
     * test run for as long as that is) */
    for (n = 0; n <= 10; n++)
        ptest_assert_msg(Sysconf((int)n) != EINVFN || n == 7,
                         "a number up to Sysconf(-1) is refused");
    ptest_assert_msg(Sysconf(11) == EINVFN, "Sysconf(11) was not refused");
    ptest_assert_msg(Sysconf(-2) == EINVFN, "Sysconf(-2) was not refused");
    ptest_assert_msg(Sysconf(1000) == EINVFN, "Sysconf(1000) was not refused");

    ptest_assert_msg(Sysconf(0) == 0x7fffffffL, "Sysconf(_SC_MEMR_MAX) is not unlimited");
    ptest_assert_msg(Sysconf(1) == 127, "Sysconf(_SC_ARG_MAX) is not 127");
    n = Sysconf(2);
    ptest_assert_msg(n >= 20 && n < 0x10000L, "Sysconf(_SC_OPEN_MAX) is not plausible");
    ptest_assert_msg(Sysconf(3) == 0, "Sysconf(_SC_NGROUPS_MAX) is not 0");
    ptest_assert_msg(Sysconf(4) == 0x7fffffffL, "Sysconf(_SC_CHILD_MAX) is not unlimited");
    ptest_assert_msg(Sysconf(5) == 200, "Sysconf(_SC_CLK_TCK) is not 200");
    ptest_assert_msg(Sysconf(6) == 4096, "Sysconf(_SC_PAGE_SIZE) is not 4096");
    ptest_assert_msg(Sysconf(8) == 1024 && Sysconf(9) == 1024, "the passwd and group buffer sizes");

    /* memory: at least the 4 MB of the smallest machine; what is free is no more
     * than there is, where the machine can say */
    phys = Sysconf(10);
    ptest_assert_msg(phys >= 1024, "Sysconf(_SC_PHYS_PAGES) is below 4 MB");
    avail = Sysconf(7);
    ptest_assert_msg(avail == EINVFN || (avail > 0 && avail <= phys),
                     "Sysconf(_SC_AVPHYS_PAGES) is more than there is");
#ifdef __x86_64__
    ptest_assert_msg(avail > 0, "the x86-64 port can count its free pages");
#endif

    ptest_pass();
}
