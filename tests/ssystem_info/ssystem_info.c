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

    ptest_begin("ssystem_info");

    if (Ssystem(SS_INQUIRE, SS_OSNAME, 0L) == EINVFN)
    {
        /* no Ssystem() on this machine */
        ptest_pass();
        return;
    }

    /* S_INQUIRE: every mode the call implements, and one that does not */
    {
        static const long modes[] = {
            SS_OSNAME, SS_OSXNAME, SS_OSVERSION, SS_OSBUILDDATE, SS_OSFEATURES,
            8, 9, 10, 11, 12, 13, 14, 15,   /* cookies, system variables */
            26,                             /* S_DELCOOKIE */
            SS_KNAME,
            -2                              /* S_CONSOLE_DIM, 0xfffe */
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

    /* where the pointers of a process are checked (private address spaces,
     * S_OSFEATURES bit 0) a buffer that is not the caller's is refused */
    if (Ssystem(SS_OSFEATURES, 0L, 0L) & 1)
    {
        rc = Ssystem(SS_KNAME, 0x100L, 16L);
        ptest_assert_msg(rc == EIMBA, "S_KNAME into an unmapped address is not refused");
    }

    ptest_pass();
}
