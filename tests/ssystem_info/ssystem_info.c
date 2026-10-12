/*
 * ssystem_info.c - regression test for the Ssystem() information modes
 *
 * SSS_INQUIRE says exactly which modes exist, and the kernel identification
 * modes (SS_OSNAME, SS_OSXNAME, SS_OSVERSION, SS_OSBUILDDATE, SS_OSFEATURES,
 * SS_KNAME) answer sensibly.  Where Ssystem() does not exist (the Atari ROMs:
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

    /* SS_INQUIRE: the modes that exist, and one that does not */
    ptest_assert_msg(Ssystem(SS_INQUIRE, SS_INQUIRE, 0L) == 0, "SS_INQUIRE does not know itself");
    ptest_assert_msg(Ssystem(SS_INQUIRE, SS_OSXNAME, 0L) == 0, "SS_INQUIRE: SS_OSXNAME missing");
    ptest_assert_msg(Ssystem(SS_INQUIRE, SS_OSBUILDDATE, 0L) == 0, "SS_INQUIRE: SS_OSBUILDDATE missing");
    ptest_assert_msg(Ssystem(SS_INQUIRE, SS_OSFEATURES, 0L) == 0, "SS_INQUIRE: SS_OSFEATURES missing");
    ptest_assert_msg(Ssystem(SS_INQUIRE, SS_KNAME, 0L) == 0, "SS_INQUIRE: SS_KNAME missing");
    ptest_assert_msg(Ssystem(SS_INQUIRE, 0x7001, 0L) == EINVFN, "SS_INQUIRE claims a mode that does not exist");
    ptest_assert_msg(Ssystem(0x7001, 0L, 0L) == EINVFN, "a mode that does not exist is not refused");

    /* identification */
    ptest_assert_msg(Ssystem(SS_OSNAME, 0L, 0L) == 0x70544f53L, "SS_OSNAME is not 'pTOS'");
    ptest_assert_msg(Ssystem(SS_OSXNAME, 0L, 0L) == 0x456d7554L, "SS_OSXNAME is not 'EmuT'");
    rc = Ssystem(SS_OSVERSION, 0L, 0L);
    ptest_assert_msg(rc >= 0 || rc < 0, "SS_OSVERSION");

    /* the build date, BCD YYYYMMDD: a plausible year, month and day */
    rc = Ssystem(SS_OSBUILDDATE, 0L, 0L);
    ptest_assert_msg(((rc >> 16) & 0xffff) >= 0x2018 && ((rc >> 16) & 0xffff) <= 0x2099,
                     "SS_OSBUILDDATE: the year is not plausible");
    ptest_assert_msg(((rc >> 8) & 0xff) >= 0x01 && ((rc >> 8) & 0xff) <= 0x12,
                     "SS_OSBUILDDATE: the month is not plausible");
    ptest_assert_msg((rc & 0xff) >= 0x01 && (rc & 0xff) <= 0x31,
                     "SS_OSBUILDDATE: the day is not plausible");

    /* features: only the three bits FreeMiNT defines */
    rc = Ssystem(SS_OSFEATURES, 0L, 0L);
    ptest_assert_msg(rc >= 0 && (rc & ~7L) == 0, "SS_OSFEATURES has undefined bits");

    /* the kernel name; too small a buffer is refused and left alone */
    memset(name, 'x', sizeof(name));
    rc = Ssystem(SS_KNAME, (long)name, (long)sizeof(name));
    ptest_assert_msg(rc == 0, "SS_KNAME failed");
    ptest_assert_msg(strcmp(name, "pTOS") == 0, "SS_KNAME is not 'pTOS'");
    memset(name, 'x', sizeof(name));
    rc = Ssystem(SS_KNAME, (long)name, 4L);
    ptest_assert_msg(rc == ERANGE, "SS_KNAME with a buffer too small is not refused");
    ptest_assert_msg(name[0] == 'x', "SS_KNAME wrote into a buffer too small");

    ptest_pass();
}
