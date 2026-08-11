/*
 * fatfs_pfs.c - wraps the built-in FAT filesystem as a pfs_ops instance
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * A thin adapter, not a reimplementation: every entry point here
 * reconstructs an absolute path string (drive letter + dopath()'s walk
 * up from a directory cookie's DND) and calls the existing, unmodified
 * GEMDOS-level FAT functions (findit(), xopen(), ixcreat(), xmkdir(),
 * ...) - never relying on a process's ambient current-directory state,
 * since fs/pfs.c tracks that itself (see pfs.c's "current-directory
 * tracking" section).  Only dopath(), ixsnext() and makbuf() needed
 * exposing beyond bdos/fs.h's already-public FAT API; see
 * bdos/fs_internal.h.
 */

#include "config.h"
#include "portab.h"
#include "pfs.h"
#include "fs.h"
#include "fs_internal.h"
#include "gemerror.h"
#include "biosbind.h"
#include "string.h"
#include "kprint.h"

#define FAT_ALL_ATTR (FA_RO | FA_HIDDEN | FA_SYSTEM | FA_VOL | FA_SUBDIR | FA_ARCHIVE)

/* Build an absolute path "X:\...\name" into 'buf' (size 'buflen'), where
 * the "\...\" portion comes from walking 'dir's DND up to the root via
 * dopath(), and the drive letter from that DND's own DMD - never from
 * ambient process state.
 */
static LONG fat_abspath(PFSCOOKIE *dir, const char *tail, char *buf, int buflen)
{
    DND *dn = (DND *)dir->index;
    char *p = buf;
    int len;

    if (buflen < 4)
        return ERANGE;

    *p++ = (char)('A' + dn->d_drv->m_drvnum);
    *p++ = ':';

    len = buflen - 3;      /* -2 for "X:" already written, -1 for the null strlcpy always leaves room for */
    p = dopath(dn, p, &len);
    if (len < 0)
        len = 0;

    strlcpy(p, tail, (size_t)len + 1);

    return E_OK;
}

static LONG fat_root(struct pfs_ops *fs, WORD drive, PFSCOOKIE *out)
{
    char path[4];
    const char *sp;
    DND *dn;

    (void)fs;

    path[0] = (char)('A' + drive);
    path[1] = ':';
    path[2] = SLASH;
    path[3] = 0;

    dn = findit(path, &sp, 1);
    if ((LONG)dn < 0)
        return (LONG)dn;
    if (!dn)
        return EPTHNF;

    out->fs = &fat_pfs_ops;
    out->index = (LONG)dn;
    out->aux = 0;
    out->pos = 0;

    return E_OK;
}

static LONG fat_lookup(PFSCOOKIE *dir, const char *path, PFSCOOKIE *out)
{
    char abspath[LEN_ZPATH];
    const char *sp;
    DND *dn;
    LONG rc;

    rc = fat_abspath(dir, path, abspath, sizeof(abspath));
    if (rc < 0)
        return rc;

    dn = findit(abspath, &sp, 1);
    if ((LONG)dn < 0)
        return (LONG)dn;
    if (!dn)
        return EPTHNF;

    out->fs = &fat_pfs_ops;
    out->index = (LONG)dn;
    out->aux = 0;
    out->pos = 0;

    return E_OK;
}

static LONG fat_open(PFSCOOKIE *dir, const char *name, WORD mode, PFSCOOKIE *out)
{
    char path[LEN_ZPATH];
    LONG rc, h;

    rc = fat_abspath(dir, name, path, sizeof(path));
    if (rc < 0)
        return rc;

    h = xopen(path, mode);
    if (h < 0)
        return h;

    out->fs = &fat_pfs_ops;
    out->index = h;
    out->aux = 0;
    out->pos = 0;

    return E_OK;
}

static LONG fat_create(PFSCOOKIE *dir, const char *name, UWORD attr, PFSCOOKIE *out)
{
    char path[LEN_ZPATH];
    LONG rc, h;

    rc = fat_abspath(dir, name, path, sizeof(path));
    if (rc < 0)
        return rc;

    h = xcreat(path, (char)attr);
    if (h < 0)
        return h;

    out->fs = &fat_pfs_ops;
    out->index = h;
    out->aux = 0;
    out->pos = 0;

    return E_OK;
}

static LONG fat_close(PFSCOOKIE *fc)
{
    return xclose((int)fc->index);
}

static LONG fat_read(PFSCOOKIE *fc, LONG pos, LONG len, UBYTE *buf)
{
    OFD *ofd = getofd((int)fc->index);

    if (!ofd)
        return EIHNDL;
    if (ixlseek(ofd, pos) != pos)
        return EREADF;

    return ixread(ofd, len, buf);
}

static LONG fat_write(PFSCOOKIE *fc, LONG pos, LONG len, const UBYTE *buf)
{
    OFD *ofd = getofd((int)fc->index);

    if (!ofd)
        return EIHNDL;
    if (ixlseek(ofd, pos) != pos)
        return EWRITF;

    return ixwrite(ofd, len, (void *)buf);
}

/* Private pool of DTAINFO search cursors, one per concurrent
 * fs/pfs.c-driven directory listing on a FAT drive - independent of the
 * caller's own DTA, since readdir()'s cursor contract is a single LONG,
 * not a whole DTA buffer.  '*cursor' is (pool index + 1); 0 means
 * "start a new listing".
 */
typedef struct {
    BOOL used;
    DTAINFO dta;
} FAT_READDIR_SLOT;

static FAT_READDIR_SLOT fat_readdir_pool[CONF_PFS_MAX_SEARCHES];

static void fat_readdir_result(char *name, int namelen, PFSATTR *outattr, const DTAINFO *dt)
{
    strlcpy(name, dt->dt_fname, namelen);
    outattr->size = (ULONG)dt->dt_fileln;
    outattr->dos_attr = (UWORD)(UBYTE)dt->dt_fattr;
    outattr->date = dt->dt_td.date;
    outattr->time = dt->dt_td.time;
}

static LONG fat_readdir(PFSCOOKIE *dir, LONG *cursor, char *name, int namelen, PFSATTR *outattr)
{
    WORD slot;

    if (*cursor == 0)
    {
        char path[LEN_ZPATH];
        WORD i;
        LONG rc;

        for (i = 0; i < CONF_PFS_MAX_SEARCHES; i++)
            if (!fat_readdir_pool[i].used)
                break;
        if (i == CONF_PFS_MAX_SEARCHES)
            return ENHNDL;

        rc = fat_abspath(dir, "*.*", path, sizeof(path));
        if (rc < 0)
            return rc;

        fat_readdir_pool[i].dta.dt_offset_drive = -1;
        rc = ixsfirst(path, FAT_ALL_ATTR, &fat_readdir_pool[i].dta);
        if (rc < 0)
            return rc;

        fat_readdir_pool[i].used = TRUE;
        *cursor = i + 1;

        fat_readdir_result(name, namelen, outattr, &fat_readdir_pool[i].dta);
        return E_OK;
    }

    slot = (WORD)(*cursor - 1);
    if ((slot < 0) || (slot >= CONF_PFS_MAX_SEARCHES) || !fat_readdir_pool[slot].used)
        return ENMFIL;

    {
        FCB *f = ixsnext(&fat_readdir_pool[slot].dta);

        if (!f)
        {
            fat_readdir_pool[slot].used = FALSE;
            return ENMFIL;
        }

        makbuf(f, &fat_readdir_pool[slot].dta);
        fat_readdir_result(name, namelen, outattr, &fat_readdir_pool[slot].dta);
    }

    return E_OK;
}

static LONG fat_mkdir(PFSCOOKIE *dir, const char *name)
{
    char path[LEN_ZPATH];
    LONG rc = fat_abspath(dir, name, path, sizeof(path));

    if (rc < 0)
        return rc;

    return xmkdir(path);
}

static LONG fat_rmdir(PFSCOOKIE *dir, const char *name)
{
    char path[LEN_ZPATH];
    LONG rc = fat_abspath(dir, name, path, sizeof(path));

    if (rc < 0)
        return rc;

    return xrmdir(path);
}

static LONG fat_remove(PFSCOOKIE *dir, const char *name)
{
    char path[LEN_ZPATH];
    LONG rc = fat_abspath(dir, name, path, sizeof(path));

    if (rc < 0)
        return rc;

    return xunlink(path);
}

static LONG fat_rename(PFSCOOKIE *olddir, const char *oldname,
                        PFSCOOKIE *newdir, const char *newname)
{
    char oldpath[LEN_ZPATH], newpath[LEN_ZPATH];
    LONG rc;

    rc = fat_abspath(olddir, oldname, oldpath, sizeof(oldpath));
    if (rc < 0)
        return rc;
    rc = fat_abspath(newdir, newname, newpath, sizeof(newpath));
    if (rc < 0)
        return rc;

    return xrename(0, oldpath, newpath);
}

static LONG fat_chattr(PFSCOOKIE *dir, const char *name, BOOL set, UWORD *dos_attr)
{
    char path[LEN_ZPATH];
    LONG rc = fat_abspath(dir, name, path, sizeof(path));

    if (rc < 0)
        return rc;

    rc = xchmod(path, set ? 1 : 0, (char)*dos_attr);
    if (rc < 0)
        return rc;

    *dos_attr = (UWORD)rc;
    return E_OK;
}

static LONG fat_dfree(struct pfs_ops *fs, WORD drive, ULONG out[4])
{
    (void)fs;
    return xgetfree((long *)out, drive + 1);
}

static LONG fat_mediach(struct pfs_ops *fs, WORD drive)
{
    (void)fs;
    return Mediach(drive);
}

struct pfs_ops fat_pfs_ops = {
    fat_root,
    fat_lookup,
    fat_open,
    fat_create,
    fat_close,
    fat_read,
    fat_write,
    fat_readdir,
    fat_mkdir,
    fat_rmdir,
    fat_remove,
    fat_rename,
    fat_chattr,
    fat_dfree,
    fat_mediach,
    NULL            /* release: DND-based directory cookies and sft[]-
                     * backed file handles both outlive a single call
                     * already (the DND tree is a cache, and file
                     * handles are freed by close), so there is nothing
                     * per-cookie to release here. */
};
