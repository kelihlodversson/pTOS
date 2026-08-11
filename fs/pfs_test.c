/*
 * pfs_test.c - throwaway pluggable filesystem self-test driver
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * Exists only to exercise fs/pfs.c end-to-end (path dispatch, handle
 * table tagging, the directory-search side table) without needing a
 * real transport - gated behind CONF_WITH_PLUGGABLE_FS_TEST, development/
 * test scaffolding only, not a starting point for a real driver.
 *
 * One fixed drive letter, one fixed read-only file "PFSTEST.TXT" in its
 * root directory; nothing else exists on this drive.
 */

#include "config.h"

#if CONF_WITH_PLUGGABLE_FS_TEST

#include "portab.h"
#include "pfs.h"
#include "fs.h"
#include "gemerror.h"
#include "string.h"

#ifndef CONF_PFS_TEST_DRIVE
#define CONF_PFS_TEST_DRIVE 15     /* P: */
#endif

static const char pfs_test_filename[] = "PFSTEST.TXT";
static const char pfs_test_contents[] = "Hello from the pluggable filesystem layer!\r\n";

/* The only two objects this driver ever hands out: the root directory,
 * and the one file.  'index' distinguishes them (0 = root, 1 = file);
 * nothing else is ever looked up.
 */
#define PFS_TEST_ROOT 0
#define PFS_TEST_FILE 1

static LONG pfstest_root(struct pfs_ops *fs, WORD drive, PFSCOOKIE *out)
{
    (void)drive;

    out->fs = fs;
    out->index = PFS_TEST_ROOT;
    out->aux = 0;
    out->pos = 0;

    return E_OK;
}

static LONG pfstest_lookup(PFSCOOKIE *dir, const char *path, PFSCOOKIE *out)
{
    if (dir->index != PFS_TEST_ROOT)
        return EPTHNF;

    /* only an empty (root-relative-to-itself) path is ever valid here -
     * this drive has no subdirectories. */
    if (path[0])
        return EPTHNF;

    *out = *dir;
    return E_OK;
}

static LONG pfstest_open(PFSCOOKIE *dir, const char *name, WORD mode, PFSCOOKIE *out)
{
    if (dir->index != PFS_TEST_ROOT)
        return EPTHNF;
    if (strcmp(name, pfs_test_filename) != 0)
        return EFILNF;
    if (mode != 0)      /* read-only drive: only RO_MODE opens */
        return EACCDN;

    out->fs = dir->fs;
    out->index = PFS_TEST_FILE;
    out->aux = 0;
    out->pos = 0;

    return E_OK;
}

static LONG pfstest_read(PFSCOOKIE *fc, LONG pos, LONG len, UBYTE *buf)
{
    LONG total = (LONG)sizeof(pfs_test_contents) - 1;

    if (fc->index != PFS_TEST_FILE)
        return EIHNDL;

    if (pos >= total)
        return 0;
    if (pos + len > total)
        len = total - pos;

    memcpy(buf, pfs_test_contents + pos, (size_t)len);

    return len;
}

static LONG pfstest_readdir(PFSCOOKIE *dir, LONG *cursor, char *name, int namelen, PFSATTR *outattr)
{
    if (dir->index != PFS_TEST_ROOT)
        return ENMFIL;
    if (*cursor != 0)
        return ENMFIL;      /* one entry, already returned */

    *cursor = 1;
    strlcpy(name, pfs_test_filename, namelen);
    outattr->size = sizeof(pfs_test_contents) - 1;
    outattr->dos_attr = FA_RO;
    outattr->date = 0;
    outattr->time = 0;

    return E_OK;
}

static LONG pfstest_dfree(struct pfs_ops *fs, WORD drive, ULONG out[4])
{
    (void)fs;
    (void)drive;

    out[0] = 0;         /* free clusters: read-only, nothing to allocate */
    out[1] = 1;         /* total clusters */
    out[2] = 512;       /* bytes/sector */
    out[3] = 1;         /* sectors/cluster */

    return E_OK;
}

static LONG pfstest_mediach(struct pfs_ops *fs, WORD drive)
{
    (void)fs;
    (void)drive;

    return 0;           /* never changes */
}

void pfs_test_init(void);      /* called from bios/bios.c after blkdev_init() */

static struct pfs_ops pfs_test_ops = {
    pfstest_root,
    pfstest_lookup,
    pfstest_open,
    NULL,               /* create: read-only */
    NULL,               /* close: nothing to release */
    pfstest_read,
    NULL,               /* write: read-only */
    pfstest_readdir,
    NULL,               /* mkdir */
    NULL,               /* rmdir */
    NULL,               /* remove */
    NULL,               /* rename */
    NULL,               /* chattr */
    pfstest_dfree,
    pfstest_mediach,
    NULL                /* release: nothing to release */
};

void pfs_test_init(void)
{
    pfs_register_drive(CONF_PFS_TEST_DRIVE, &pfs_test_ops);
}

#endif /* CONF_WITH_PLUGGABLE_FS_TEST */
