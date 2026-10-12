/*
 * cookie_values.c - regression test for the cookies a program reads with
 * Ssystem(S_GETCOOKIE)
 *
 * _CPU and _FPU of a CPU that is no 68k carry bit 31 (include/cookie.h) so that
 * a program does not take them for the 68k numbers they are not; _MCH and _VDO
 * say there is no Atari hardware; the "MiNT" cookie is not there, because it
 * would promise Pfork, signals and the rest.  Where Ssystem() does not exist
 * (the Atari ROMs) the suite passes without testing anything.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>

#define EINVFN          (-32L)
#define SS_GETCOOKIE    8
#define TAG(a, b, c, d) (((long)(a) << 24) | ((long)(b) << 16) | ((long)(c) << 8) | (d))

void test_cookie_values(void);

void test_cookie_values(void)
{
    unsigned long value;

    ptest_begin("cookie_values");

    if (Ssystem(-1, 0, 0L) == EINVFN)
    {
#if defined(__arm__) || defined(__x86_64__)
        ptest_fail("Ssystem() is missing");
#else
        ptest_pass();               /* no Ssystem() on the Atari ROMs */
#endif
        return;
    }

#if defined(__arm__) || defined(__x86_64__)
    /* a foreign CPU: bit 31, and the ELF machine number of the CPU below */
    value = 0;
    ptest_assert_msg(Ssystem(SS_GETCOOKIE, TAG('_', 'C', 'P', 'U'), (long)&value) == 0,
                     "no _CPU cookie");
    ptest_assert_msg(value & 0x80000000UL, "_CPU of a foreign CPU has bit 31 clear");
#ifdef __x86_64__
    ptest_assert_msg(value == (0x80000000UL | 62), "_CPU is not the x86-64 value");
#else
    ptest_assert_msg(value == (0x80000000UL | 40), "_CPU is not the ARM value");
#endif
    value = 0;
    ptest_assert_msg(Ssystem(SS_GETCOOKIE, TAG('_', 'F', 'P', 'U'), (long)&value) == 0,
                     "no _FPU cookie");
    ptest_assert_msg(value == 0x80000001UL, "_FPU is not 'foreign, hardware floating point'");
#endif

    /* there is no Atari hardware here, and no MiNT */
#if defined(__arm__) || defined(__x86_64__)
    ptest_assert_msg(Ssystem(SS_GETCOOKIE, TAG('_', 'M', 'C', 'H'), (long)&value) == 0,
                     "no _MCH cookie");
    ptest_assert_msg(Ssystem(SS_GETCOOKIE, TAG('_', 'V', 'D', 'O'), (long)&value) == 0,
                     "no _VDO cookie");
#endif
    ptest_assert_msg(Ssystem(SS_GETCOOKIE, TAG('M', 'i', 'N', 'T'), (long)&value) != 0,
                     "a MiNT cookie promises what pTOS does not have");

    ptest_pass();
}
