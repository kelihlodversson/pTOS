/*
 * ssystem_info.c - regression test for the Ssystem() information modes
 *
 * SS_INQUIRE says exactly which modes exist, and the kernel identification
 * modes (S_OSNAME, S_OSXNAME, S_OSVERSION, S_OSBUILDDATE, S_OSFEATURES,
 * S_KNAME) answer sensibly.  Where Ssystem() does not exist (the Atari ROMs:
 * GEMDOS stops at 0x57 there) the suite passes without testing anything.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <string.h>

#define SS_INQUIRE       (-1)
#define SS_OSNAME        0
#define SS_OSXNAME       1
#define SS_OSVERSION     2
#define SS_OSBUILDDATE   4
#define SS_OSFEATURES    7
#define SS_KNAME         900

#define EINVFN          (-32L)
#define ERANGE          (-64L)
#define EIMBA           (-40L)

void test_ssystem_info(void);

void test_ssystem_info(void)
{
    long rc;
    char name[8];
    char big[128];
    struct { unsigned long page_size, phys_pages, free_pages, processes; } mi;

    ptest_begin("ssystem_info");

    if (Ssystem(SS_INQUIRE, SS_OSNAME, 0L) == EINVFN)
    {
#if defined(__arm__) || defined(__x86_64__)
        /* these have it (CONF_WITH_SSYSTEM): a dispatch that is gone is a failure */
        ptest_fail("Ssystem() is missing");
#else
        ptest_pass();               /* no Ssystem() on the Atari ROMs */
#endif
        return;
    }

    /* S_INQUIRE: every mode the call implements, and one that does not */
    {
        static const long modes[] = {
            SS_OSNAME, SS_OSXNAME, SS_OSVERSION, SS_OSBUILDDATE, SS_OSFEATURES,
            8, 9, 10, 11, 12, 13, 14, 15,   /* cookies, system variables */
            26,                             /* S_DELCOOKIE */
            SS_KNAME, 910, 911,             /* S_KNAME, S_CNAME, S_CVERSION */
            -2, -3                          /* S_CONSOLE_DIM 0xfffe, S_MEMINFO 0xfffd */
        };
        unsigned int i;

        for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++)
            ptest_assert_msg(Ssystem(SS_INQUIRE, modes[i], 0L) == 0,
                             "S_INQUIRE does not know a mode that is implemented");
    }
    ptest_assert_msg(Ssystem(SS_INQUIRE, SS_INQUIRE, 0L) == 0, "S_INQUIRE does not know itself");
    ptest_assert_msg(Ssystem(SS_INQUIRE, 0x7001, 0L) == EINVFN, "S_INQUIRE claims a mode that does not exist");
    ptest_assert_msg(Ssystem(0x7001, 0L, 0L) == EINVFN, "a mode that does not exist is not refused");

    /* identification */
    ptest_assert_msg(Ssystem(SS_OSNAME, 0L, 0L) == 0x70544f53L, "S_OSNAME is not 'pTOS'");
    ptest_assert_msg(Ssystem(SS_OSXNAME, 0L, 0L) == 0x456d7554L, "S_OSXNAME is not 'EmuT'");
    /* major.minor.patch and a release letter, MSB first: the letter at least */
    rc = Ssystem(SS_OSVERSION, 0L, 0L);
    ptest_assert_msg(rc >= 0 && (rc & 0xff) >= 'a' && (rc & 0xff) <= 'z',
                     "S_OSVERSION has no release letter");

    /* the build date as FreeMiNT packs it, all binary: day in bits 31-24,
     * month in bits 23-16, year in bits 15-0 */
    rc = Ssystem(SS_OSBUILDDATE, 0L, 0L);
    ptest_assert_msg((rc & 0xffff) >= 2018 && (rc & 0xffff) <= 2099,
                     "S_OSBUILDDATE: the year is not plausible");
    ptest_assert_msg(((rc >> 16) & 0xff) >= 1 && ((rc >> 16) & 0xff) <= 12,
                     "S_OSBUILDDATE: the month is not plausible");
    ptest_assert_msg(((rc >> 24) & 0xff) >= 1 && ((rc >> 24) & 0xff) <= 31,
                     "S_OSBUILDDATE: the day is not plausible");

    /* features: only the three bits FreeMiNT defines */
    rc = Ssystem(SS_OSFEATURES, 0L, 0L);
    ptest_assert_msg(rc >= 0 && (rc & ~7L) == 0, "S_OSFEATURES has undefined bits");

    /* the kernel name; too small a buffer is refused and left alone */
    memset(name, 'x', sizeof(name));
    rc = Ssystem(SS_KNAME, (long)name, (long)sizeof(name));
    ptest_assert_msg(rc == 0, "S_KNAME failed");
    ptest_assert_msg(strcmp(name, "pTOS") == 0, "S_KNAME is not 'pTOS'");
    memset(name, 'x', sizeof(name));
    rc = Ssystem(SS_KNAME, (long)name, 4L);
    ptest_assert_msg(rc == ERANGE, "S_KNAME with a buffer too small is not refused");
    ptest_assert_msg(name[0] == 'x', "S_KNAME wrote into a buffer too small");
    /* a buffer of exactly the five bytes is enough */
    rc = Ssystem(SS_KNAME, (long)name, 5L);
    ptest_assert_msg(rc == 0 && strcmp(name, "pTOS") == 0, "S_KNAME with a buffer of five bytes failed");

    /* the compiler's name and version: a string, refused when the buffer is
     * too small for it */
    memset(big, 'x', sizeof(big));
    rc = Ssystem(910, (long)big, (long)sizeof(big));
    ptest_assert_msg(rc == 0 && strlen(big) >= 3 && strlen(big) < 16, "S_CNAME failed");
    memset(big, 'x', sizeof(big));
    rc = Ssystem(911, (long)big, (long)sizeof(big));
    /* (gcc's starts with a digit, clang's with its name: only a string is asked for) */
    ptest_assert_msg(rc == 0 && strlen(big) >= 1 && strlen(big) < sizeof(big) - 1,
                     "S_CVERSION is not a string");
    memset(name, 'x', sizeof(name));
    ptest_assert_msg(Ssystem(911, (long)name, 2L) == ERANGE && name[0] == 'x',
                     "S_CVERSION into a buffer too small was not refused");

    /* the memory in one call: the struct as far as it was asked for, its size
     * for -1, the fields that agree with Sysconf() */
    ptest_assert_msg(Ssystem(-3, 0L, -1L) == (long)sizeof(mi), "S_MEMINFO size query");
    memset(&mi, 0, sizeof(mi));
    rc = Ssystem(-3, (long)&mi, (long)sizeof(mi));
    ptest_assert_msg(rc == (long)sizeof(mi), "S_MEMINFO failed");
    ptest_assert_msg(mi.page_size == 4096UL, "S_MEMINFO: the page size is not 4096");
    ptest_assert_msg(mi.phys_pages == (unsigned long)Sysconf(10), "S_MEMINFO: the pages differ from Sysconf()");
    /* the free pages are what Sysconf(7) says (all ones where it cannot say), give
     * or take the few pages this process touches between the two calls */
    {
        long avail = Sysconf(7);

        if (avail == EINVFN)
            ptest_assert_msg(mi.free_pages == 0xffffffffUL, "S_MEMINFO: free pages unknown, not all ones");
        else
            ptest_assert_msg(mi.free_pages <= mi.phys_pages
                             && (long)mi.free_pages - avail <= 64 && avail - (long)mi.free_pages <= 64,
                             "S_MEMINFO: the free pages differ from Sysconf()");
    }
    ptest_assert_msg(mi.processes >= 1, "S_MEMINFO: no process (all ones: not counted)");
    memset(&mi, 0xff, sizeof(mi));
    ptest_assert_msg(Ssystem(-3, (long)&mi, 4L) == 4 && mi.phys_pages == 0xffffffffUL,
                     "S_MEMINFO copied more than it was asked for");
    ptest_assert_msg(Ssystem(-3, 0L, 16L) < 0, "S_MEMINFO into NULL was not refused");

    /* where the pointers of a process are checked (private address spaces,
     * S_OSFEATURES bit 0) a buffer that is not the caller's is refused.  The
     * x86-64 port has them, so the bit has to be there (and the test below
     * must not be skipped); ARM and m68k do not */
#ifdef __x86_64__
    ptest_assert_msg(Ssystem(SS_OSFEATURES, 0L, 0L) & 1,
                     "S_OSFEATURES: memory protection is not reported on x86-64");
#else
    ptest_assert_msg(!(Ssystem(SS_OSFEATURES, 0L, 0L) & 1),
                     "S_OSFEATURES: memory protection is reported without address spaces");
#endif
    if (Ssystem(SS_OSFEATURES, 0L, 0L) & 1)
    {
        rc = Ssystem(SS_KNAME, 0x100L, 16L);
        ptest_assert_msg(rc == EIMBA, "S_KNAME into an unmapped address is not refused");
    }

    ptest_pass();
}
