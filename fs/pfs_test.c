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
 * One fixed drive letter and a few read-only fixtures in its root directory.
 */

#include "config.h"

#if CONF_WITH_PLUGGABLE_FS_TEST

#include "portab.h"
#include "pfs.h"
#include "fs.h"
#include "gemerror.h"
#include "string.h"
#if CONF_WITH_FPOLL_TIME64
#include "bdosstub.h"
#include "console.h"
#include "fpoll_kernel.h"
#include "process_files.h"
#endif

#ifndef CONF_PFS_TEST_DRIVE
#define CONF_PFS_TEST_DRIVE 15     /* P: */
#endif

static const char pfs_test_contents[] = "Hello from the pluggable filesystem layer!\r\n";

enum {
    PFS_TEST_ROOT, PFS_TEST_FILE,
#if CONF_WITH_FPOLL_TIME64
    PFS_TEST_POLL_CHECK, PFS_TEST_POLL_WAIT, PFS_TEST_POLL_CHURN,
#endif
    PFS_TEST_COUNT
};
static const char *const pfs_test_names[] = {
    "PFSTEST.TXT",
#if CONF_WITH_FPOLL_TIME64
    "POLLCHK.TXT", "POLLWAIT.TXT", "POLLCHRN.TXT",
#endif
};

#if CONF_WITH_FPOLL_TIME64
/* The producers and observers share mock device state across open files. */
static BOOL poll_check_ready, poll_wait_sampled, poll_wait_ready;
static UBYTE poll_churn_samples;

static LONG pfstest_ready(const PFSCOOKIE *fc, UWORD events)
{
    if (fc->index == PFS_TEST_POLL_CHECK && !poll_check_ready) {
        /* Publish after observing the old level but before wait admission. */
        poll_check_ready = TRUE;
        fpoll_notify();
        return 0;
    }
    if (fc->index == PFS_TEST_POLL_WAIT) {
        poll_wait_sampled = TRUE;
        if (!poll_wait_ready)
            return 0;
        return (events & FS_READY_READ) | FS_READY_ERROR | FS_READY_HANGUP;
    }
    if (fc->index == PFS_TEST_POLL_CHURN) {
        /* Inject the binding changes a reentrant BIOS status vector can
         * cause. Bound the injection so a broken retry loop fails the test
         * rather than hanging the suite. */
        if (++poll_churn_samples <= 8)
            return process_files_bind_bios(run, 4, H_Console);
        return 0;
    }
    return events & FS_READY_READ;
}
#endif

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
    LONG index;

    if (dir->index != PFS_TEST_ROOT)
        return EPTHNF;
    for (index = PFS_TEST_FILE; index < PFS_TEST_COUNT; index++)
        if (strcmp(name, pfs_test_names[index - PFS_TEST_FILE]) == 0)
            break;
    if (index == PFS_TEST_COUNT)
        return EFILNF;
    if (mode != 0)      /* read-only drive: only RO_MODE opens */
        return EACCDN;

    out->fs = dir->fs;
    out->index = index;
    out->aux = 0;
    out->pos = 0;
#if CONF_WITH_FPOLL_TIME64
    if (index == PFS_TEST_POLL_CHECK)
        poll_check_ready = FALSE;
    if (index == PFS_TEST_POLL_CHURN)
        poll_churn_samples = 0;
#endif

    return E_OK;
}

static LONG pfstest_read(PFSCOOKIE *fc, LONG pos, LONG len, UBYTE *buf)
{
    LONG total = (LONG)sizeof(pfs_test_contents) - 1;

    if (fc->index < PFS_TEST_FILE || fc->index >= PFS_TEST_COUNT)
        return EIHNDL;

#if CONF_WITH_FPOLL_TIME64
    if (fc->index == PFS_TEST_POLL_CHURN && len == 1) {
        *buf = poll_churn_samples;
        poll_churn_samples = 0;
        return 1;
    }
    if (fc->index == PFS_TEST_POLL_WAIT) {
        /* A two-byte read resets the test device. A one-byte read in the
         * producer acknowledges a sampled wait and publishes readiness. */
        if (len == 2) {
            poll_wait_sampled = poll_wait_ready = FALSE;
        } else if (len == 1) {
            if (!poll_wait_sampled)
                return 0;
            poll_wait_ready = TRUE;
            fpoll_notify();
        }
    }
#endif

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
    if (*cursor >= PFS_TEST_COUNT - PFS_TEST_FILE)
        return ENMFIL;

    strlcpy(name, pfs_test_names[(*cursor)++], namelen);
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
    NULL,               /* release: nothing to release */
#if CONF_WITH_FPOLL_TIME64
    .ready = pfstest_ready,
#endif
    /* native_handles omitted: defaults to FALSE, since this driver has
     * no handle management of its own - see fs/pfs.h. */
};

void pfs_test_init(void)
{
    pfs_register_drive(CONF_PFS_TEST_DRIVE, &pfs_test_ops);
}

#endif /* CONF_WITH_PLUGGABLE_FS_TEST */
