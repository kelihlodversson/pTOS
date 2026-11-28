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

static void test_directory_cache(const char *directory)
{
    static const char absolute[] = "Q:\\XFS9P\\CACHE.TXT";
    static const char relative[] = "Q:CACHE.TXT";
    char saved[128] = "Q:\\";
    long fd, rc;

    if (Dgetpath(saved + 2, DRIVE_Q + 1) != 0 || Dsetpath(directory) != 0) {
        ptest_fail("set the host current directory for the cache test");
        return;
    }
    if (!saved[2])
        saved[2] = '\\';
    fd = Fcreate(absolute, 0);
    ptest_assert_msg(fd >= 0, "create the host cache fixture");
    if (fd >= 0) {
        ptest_assert_msg(Fclose((short)fd) == 0, "close the host cache fixture");
        fd = Fopen(relative, 0);
        ptest_assert_msg(fd >= 0, "populate the current-directory listing cache");
        if (fd >= 0)
            ptest_assert_msg(Fclose((short)fd) == 0, "close the relative cache fixture");
        ptest_assert_msg(Fdelete(absolute) == 0, "delete the cached file through an absolute path");
        rc = Dcreate(relative);
        ptest_assert_msg(rc == 0, "a relative mkdir sees the absolute deletion");
        if (rc == 0)
            ptest_assert_msg(Ddelete(absolute) == 0, "remove the replacement directory");
    }
    ptest_assert_msg(Dsetpath(saved) == 0, "restore the host current directory");
}

static void test_directory(void)
{
    static const char dir[] = "Q:\\XFS9P";
    static const char first[] = "Q:\\XFS9P\\ALPHA.TXT";
    static const char second[] = "Q:\\XFS9P\\BETA.TXT";
    static const char pattern[] = "Q:\\XFS9P\\*.TXT";
    _DTA a, b, *saved = (_DTA *)Fgetdta();
    long fd;

    if (Dcreate(dir) != 0) {
        ptest_fail("create a directory on the host drive");
        return;
    }
    fd = Fcreate(first, 0);
    if (fd >= 0) {
        ptest_assert_msg(Fwrite((short)fd, 1, "x") == 1, "write the first host file");
        ptest_assert_msg(Fclose((short)fd) == 0, "close the first host file");
    } else {
        ptest_fail("create the first host file");
    }

    /* Fcreate targets the existing host spelling and truncates that file. */
    fd = Fcreate("Q:\\xfs9p\\alpha.txt", 0);
    ptest_assert_msg(fd >= 0, "recreate through a case-insensitive spelling");
    if (fd >= 0)
        ptest_assert_msg(Fseek(0, (short)fd, 2) == 0 && Fclose((short)fd) == 0,
                         "recreate truncates the original file");
    ptest_assert_msg(Frename(0, first, second) == 0, "rename the existing host file");
    fd = Fcreate(first, 0);
    ptest_assert_msg(fd >= 0 && Fclose((short)fd) == 0, "create the second directory entry");

    (void)Fsetdta(&a);
    if (Fsfirst(pattern, 0) == 0) {
        char first_name[14];

        strcpy(first_name, a.dta_name);
        (void)Fsetdta(&b);
        ptest_assert_msg(Fsfirst(pattern, 0) == 0 && !strcmp(first_name, b.dta_name),
                         "a separate search starts at the same entry");
        (void)Fsetdta(&a);
        if (Fsnext() == 0) {
            ptest_assert_msg(strcmp(first_name, a.dta_name) && Fsnext() == -49L,
                             "the first search reaches EOF");
            (void)Fsetdta(&b);
            ptest_assert_msg(Fsnext() == 0 && !strcmp(a.dta_name, b.dta_name) && Fsnext() == -49L,
                             "the other search still owns its cursor");
        } else {
            ptest_fail("the first search continues independently");
        }
    } else {
        ptest_fail("start a host-directory search");
    }
    (void)Fsetdta(saved);
    test_directory_cache(dir);
    ptest_assert_msg(Fdelete(first) == 0 && Fdelete(second) == 0 && Ddelete(dir) == 0,
                     "release the host directory and its files");
}

void test_pfs_seek(void)
{
    char data[FIXTURE_N];
    char buf[FIXTURE_N];
    _DOSTIME dt;
    _DISKINFO disk;
    long rc;
    short fd, saved;
    int i, failures = ptest_failures();

    ptest_begin("pfs_seek (#589)");

    if (!(Drvmap() & (1L << DRIVE_Q)))
    {
        conws("SKIP (no Q: drive)\r\n");
        return;
    }

    ptest_assert_msg(Dfree(&disk, DRIVE_Q + 1) == 0 && disk.b_clsiz == 1 && disk.b_secsiz > 0,
                     "host Dfree reports its block geometry");

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
    for (i = 0; i < 128; i++) {
        /* The old provider ignored high Fopen bits. Every iteration must
         * return its fid before the next open can acquire the slot. */
        rc = Fopen(FIXTURE, 0x100);
        if (rc < 0 || Fclose((short)rc)) {
            ptest_fail("host opens retain legacy mode masking and release each fid");
            break;
        }
    }
    Fdelete(FIXTURE);
    test_directory();

    ptest_pass();
    if (ptest_failures() == failures)
        conws("native virtio-9p through XFS: PASS\r\n");
}
