/*
 * pfs_seek.c - regression test for issue #589
 *
 * Fseek() returned EIHNDL for every file on a pluggable file system
 * drive (the virtio-9p host directory, Q: on the virt boards):
 * xlseek() only looked for a FAT open file.  Fdatime() had the same
 * gap.
 *
 * This suite writes a file on Q:, then drives Fseek() through all
 * three seek modes and out of range, checking the bytes read at each
 * position, reads the file's date and time, and redirects standard
 * input to it with Fforce().  It is skipped where there is no Q: drive.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <string.h>

#define FIXTURE     "Q:\\PFSSEEK.BIN"
#define FIXTURE_N   512                 /* total fixture size in bytes */
#define DRIVE_Q     16

#define GEMDOS_ERANGE (-64L)      /* gemerror.h ERANGE */

void test_pfs_seek(void);

void test_pfs_seek(void)
{
    char data[FIXTURE_N];
    char buf[FIXTURE_N];
    _DOSTIME dt;
    long rc;
    short fd, saved;
    int i;

    ptest_begin("pfs_seek (#589)");

    if (!(Drvmap() & (1L << DRIVE_Q)))
    {
        conws("SKIP (no Q: drive)\r\n");
        return;
    }

    for (i = 0; i < FIXTURE_N; i++)
        data[i] = (char)(i * 13 + 5);

    fd = (short)Fcreate(FIXTURE, 0);
    if (fd < 0)
    {
        ptest_fail("cannot create the file on Q: (is it writable?)");
        return;
    }

    rc = Fwrite(fd, FIXTURE_N, data);
    ptest_assert_msg(rc == FIXTURE_N, "Fwrite short");

    /* the position after writing, and the size, on the same handle */
    rc = Fseek(0L, fd, 1);              /* SEEK_CUR */
    ptest_assert_msg(rc == FIXTURE_N, "Fseek(0,SEEK_CUR) after Fwrite");
    rc = Fseek(0L, fd, 2);              /* SEEK_END */
    ptest_assert_msg(rc == FIXTURE_N, "Fseek(0,SEEK_END) after Fwrite");
    Fclose(fd);

    fd = (short)Fopen(FIXTURE, 0);
    if (fd < 0)
    {
        Fdelete(FIXTURE);
        ptest_fail("cannot reopen the file on Q:");
        return;
    }

    /* the size, then back to the start */
    rc = Fseek(0L, fd, 2);
    ptest_assert_msg(rc == FIXTURE_N, "Fseek(0,SEEK_END) is not the size");
    rc = Fseek(0L, fd, 0);              /* SEEK_SET */
    ptest_assert_msg(rc == 0, "Fseek(0,SEEK_SET) failed");
    rc = Fread(fd, FIXTURE_N, buf);
    ptest_assert_msg(rc == FIXTURE_N, "rewound read short");
    ptest_assert_msg(memcmp(buf, data, FIXTURE_N) == 0,
                     "rewound read returned wrong bytes");

    /* absolute, relative and from the end */
    rc = Fseek(17L, fd, 0);
    ptest_assert_msg(rc == 17, "Fseek(17,SEEK_SET) failed");
    rc = Fread(fd, 10, buf);
    ptest_assert_msg(rc == 10 && memcmp(buf, data + 17, 10) == 0,
                     "read after Fseek(17,SEEK_SET)");

    rc = Fseek(5L, fd, 1);
    ptest_assert_msg(rc == 32, "Fseek(5,SEEK_CUR) failed");
    rc = Fread(fd, 8, buf);
    ptest_assert_msg(rc == 8 && memcmp(buf, data + 32, 8) == 0,
                     "read after Fseek(5,SEEK_CUR)");

    rc = Fseek(-9L, fd, 2);
    ptest_assert_msg(rc == FIXTURE_N - 9, "Fseek(-9,SEEK_END) failed");
    rc = Fread(fd, 9, buf);
    ptest_assert_msg(rc == 9 && memcmp(buf, data + FIXTURE_N - 9, 9) == 0,
                     "read after Fseek(-9,SEEK_END)");

    /* out of range, as on a FAT drive; the position stays */
    rc = Fseek(1L, fd, 2);
    ptest_assert_msg(rc == GEMDOS_ERANGE, "Fseek past the end is not ERANGE");
    rc = Fseek(-1L, fd, 0);
    ptest_assert_msg(rc == GEMDOS_ERANGE, "Fseek before the start is not ERANGE");
    rc = Fseek(0L, fd, 1);
    ptest_assert_msg(rc == FIXTURE_N, "position moved by a failed Fseek");

    /* the date and time of an open file */
    dt.date = 0;
    rc = Fdatime(&dt, fd, 0);
    ptest_assert_msg(rc == 0, "Fdatime(get) failed");
    ptest_assert_msg(dt.date != 0, "Fdatime(get) gave no date");

    /* standard input redirected to it, and back to the console */
    saved = (short)Fdup(0);
    ptest_assert_msg(saved >= 0, "Fdup(0) failed");
    if (saved >= 0)
    {
        rc = Fforce(0, fd);
        ptest_assert_msg(rc == 0, "Fforce(0) to the file failed");
        if (rc == 0)
        {
            rc = Fseek(100L, 0, 0);
            ptest_assert_msg(rc == 100, "Fseek on the redirected handle");
            rc = Fread(0, 4, buf);
            ptest_assert_msg(rc == 4 && memcmp(buf, data + 100, 4) == 0,
                             "read from the redirected handle");
            Fclose(0);  /* gives back the reference Fforce() took */
        }
        Fforce(0, saved);
        Fclose(saved);
    }

    Fclose(fd);
    Fdelete(FIXTURE);

    ptest_pass();
}
