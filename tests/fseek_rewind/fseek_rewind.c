/*
 * fseek_rewind.c - regression test for issue #321
 *
 * On ARM, Fseek() was mis-dispatched by bdos/osif(): the function
 * table entry carries stdio_typ 0x81, which the redirection code
 * interprets as "the handle is pw[3]" -- correct for the m68k WORD
 * parameter layout, but on ARM's LONG-based pw[] the handle is
 * pw[2].  The lookup consequently read the seek mode (0) as the
 * handle, resolved it to the console, and swallowed the SEEK_SET
 * call with a 0 return, leaving the file position untouched.
 *
 * The failure surfaced when the GEM resource loader read a 36-byte
 * RSC header, then Fseek(0, fd, SEEK_SET) to rewind, then tried to
 * read the whole file: the payload landed 36 bytes short.
 *
 * This suite recreates that sequence and drives Fseek() through all
 * three seek modes, verifying the resulting reads return the exact
 * bytes for that position.  It must pass on both m68k and ARM.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <string.h>

#define FIXTURE     "C:\\fseek_probe.bin"
#define FIXTURE_N   512                 /* total fixture size in bytes */
#define HEADER_N    36                  /* header read before rewinding */

void test_fseek_rewind(void);

void test_fseek_rewind(void)
{
    char data[FIXTURE_N];
    char buf[FIXTURE_N];
    long rc;
    short fd;
    int i;

    ptest_begin("fseek_rewind (#321)");

    /* deterministic content so bytes can be checked at each position */
    for (i = 0; i < FIXTURE_N; i++)
        data[i] = (char)(i * 7 + 3);

    fd = (short)Fcreate(FIXTURE, 0);    /* mode 0: read/write */
    ptest_assert_msg(fd >= 0, "Fcreate failed (is C: writable?)");
    if (fd < 0)
    {
        ptest_fail("cannot create fixture file");
        return;
    }

    rc = Fwrite(fd, FIXTURE_N, data);
    ptest_assert_msg(rc == FIXTURE_N, "Fwrite short");
    Fclose(fd);

    fd = (short)Fopen(FIXTURE, 0);      /* mode 0: read only */
    ptest_assert_msg(fd >= 0, "Fopen failed");
    if (fd < 0)
    {
        Fdelete(FIXTURE);
        ptest_fail("cannot reopen fixture file");
        return;
    }

    /* 1. header read, then SEEK_SET rewind -- the RSC loading sequence */
    rc = Fread(fd, HEADER_N, buf);
    ptest_assert_msg(rc == HEADER_N, "header read short");

    rc = Fseek(0L, fd, 0);              /* SEEK_SET */
    ptest_assert_msg(rc == 0, "Fseek(0,SEEK_SET) failed");

    rc = Fread(fd, FIXTURE_N, buf);
    ptest_assert_msg(rc == FIXTURE_N, "rewound read short");
    ptest_assert_msg(memcmp(buf, data, FIXTURE_N) == 0,
                     "rewound read returned wrong bytes");

    /* 2. absolute seek into the middle */
    rc = Fseek(17L, fd, 0);             /* SEEK_SET */
    ptest_assert_msg(rc == 17, "Fseek(17,SEEK_SET) failed");
    rc = Fread(fd, 10, buf);
    ptest_assert_msg(rc == 10, "mid read short");
    ptest_assert_msg(memcmp(buf, data + 17, 10) == 0, "mid read mismatch");

    /* 3. relative seek from the current position (now 27) */
    rc = Fseek(5L, fd, 1);              /* SEEK_CUR */
    ptest_assert_msg(rc == 32, "Fseek(5,SEEK_CUR) failed");
    rc = Fread(fd, 8, buf);
    ptest_assert_msg(rc == 8, "relative read short");
    ptest_assert_msg(memcmp(buf, data + 32, 8) == 0,
                     "relative read mismatch");

    /* 4. seek relative to end of file */
    rc = Fseek(-9L, fd, 2);             /* SEEK_END */
    ptest_assert_msg(rc == FIXTURE_N - 9, "Fseek(-9,SEEK_END) failed");
    rc = Fread(fd, 9, buf);
    ptest_assert_msg(rc == 9, "end read short");
    ptest_assert_msg(memcmp(buf, data + FIXTURE_N - 9, 9) == 0,
                     "end read mismatch");

    Fclose(fd);
    Fdelete(FIXTURE);

    ptest_pass();
}