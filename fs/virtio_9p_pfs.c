/*
 * virtio_9p_pfs.c - wraps bios/virtio_9p.c's fid-level API as a pfs_ops
 * instance
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * A thin adapter, like fs/fatfs_pfs.c: every entry point below maps
 * straight onto bios/virtio_9p.c's v9p_walk()/v9p_clunk()/... - no 9P
 * wire-format or virtio-mmio knowledge lives here.
 *
 * Stage 2 of #157: only root() and lookup() (empty-path "dup" only, per
 * fs/pfs.h's contract) are implemented; open/create/read/write/readdir/
 * mkdir/rmdir/remove/rename/chattr/dfree/mediach and multi-component
 * lookup() all land in later stages.
 */

#include "config.h"

#if CONF_WITH_VIRTIO_9P

#include "portab.h"
#include "pfs.h"
#include "gemerror.h"
#include "kprint.h"
#include "virtio_9p.h"

static LONG v9p_pfs_root(struct pfs_ops *fs, WORD drive, PFSCOOKIE *out)
{
    ULONG fid;
    QID9P qid;
    LONG rc;

    (void)drive;    /* this driver only ever serves CONF_VIRTIO_9P_DRIVE */

    rc = v9p_walk(v9p_root_fid(), 0, NULL, &fid, &qid);
    if (rc < 0)
        return rc;

    out->fs = fs;
    out->index = (LONG)fid;
    out->aux = 0;
    out->pos = 0;

    return E_OK;
}

static LONG v9p_pfs_lookup(PFSCOOKIE *dir, const char *path, PFSCOOKIE *out)
{
    ULONG fid;
    QID9P qid;
    LONG rc;

    if (path[0])
    {
        /* Multi-component path resolution (splitting on '\', handling
         * "."/"..", chaining Twalk calls) is a later stage - see #157's
         * plan. For now only the "give me a fresh, independently
         * releasable dup of 'dir' itself" contract (empty path) is
         * supported. */
        return EPTHNF;
    }

    rc = v9p_walk((ULONG)dir->index, 0, NULL, &fid, &qid);
    if (rc < 0)
        return rc;

    out->fs = dir->fs;
    out->index = (LONG)fid;
    out->aux = 0;
    out->pos = 0;

    return E_OK;
}

static void v9p_pfs_release(PFSCOOKIE *fc)
{
    v9p_clunk((ULONG)fc->index);
}

static struct pfs_ops v9p_pfs_ops = {
    v9p_pfs_root,
    v9p_pfs_lookup,
    NULL,               /* open: a later stage */
    NULL,               /* create */
    NULL,               /* close */
    NULL,               /* read */
    NULL,               /* write */
    NULL,               /* readdir */
    NULL,               /* mkdir */
    NULL,               /* rmdir */
    NULL,               /* remove */
    NULL,               /* rename */
    NULL,               /* chattr */
    NULL,               /* dfree */
    NULL,               /* mediach */
    v9p_pfs_release,
    FALSE               /* native_handles: this driver has no handle
                         * management of its own - see fs/pfs.h. */
};

void v9p_pfs_init(void);   /* called from bios/bios.c after virtio_9p_init() */

void v9p_pfs_init(void)
{
    LONG rc;

    if (!virtio_9p_present())
        return;

    rc = pfs_register_drive(CONF_VIRTIO_9P_DRIVE, &v9p_pfs_ops);
    if (rc < 0)
        KDEBUG(("v9p_pfs_init: pfs_register_drive failed, rc=%ld\n", rc));
}

#endif /* CONF_WITH_VIRTIO_9P */
