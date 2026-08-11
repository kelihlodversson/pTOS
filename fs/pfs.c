/*
 * pfs.c - pluggable filesystem layer core
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * Owns: the per-drive driver table, GEMDOS path-based call dispatch,
 * current-directory tracking, and Fsfirst/Fsnext search state.  See
 * fs/pfs.h for the driver-facing interface, and the design notes in
 * this branch's PR description for the overall architecture.
 */

#include "config.h"
#include "portab.h"
#include "pfs.h"
#include "fs.h"
#include "gemerror.h"
#include "biosbind.h"
#include "string.h"
#include "kprint.h"
#include "../bios/tosvars.h"    /* for drvbits, same convention as
                                 * bdos/proc.c and bdos/umem.c */

/*
 * GEMDOS function numbers this layer dispatches.  Fread(0x3F)/
 * Fwrite(0x40)/Fclose(0x3E) are deliberately absent - see pfs.h.
 */
#define FN_DFREE        0x36
#define FN_DCREATE      0x39
#define FN_DDELETE      0x3A
#define FN_DSETPATH     0x3B
#define FN_FCREATE      0x3C
#define FN_FOPEN        0x3D
#define FN_FDELETE      0x41
#define FN_FATTRIB      0x43
#define FN_DGETPATH     0x47
#define FN_FSFIRST      0x4E
#define FN_FSNEXT       0x4F
#define FN_FRENAME      0x56

BOOL pfs_is_fs_call(WORD fn)
{
    switch (fn)
    {
    case FN_DFREE:
    case FN_DCREATE:
    case FN_DDELETE:
    case FN_DSETPATH:
    case FN_FCREATE:
    case FN_FOPEN:
    case FN_FDELETE:
    case FN_FATTRIB:
    case FN_DGETPATH:
    case FN_FSFIRST:
    case FN_FSNEXT:
    case FN_FRENAME:
        return TRUE;
    default:
        return FALSE;
    }
}


/* ------------------------------------------------------------------ */
/* drive table                                                        */
/* ------------------------------------------------------------------ */

static struct pfs_ops *pfs_drive[BLKDEVNUM];

LONG pfs_register_drive(WORD drive, struct pfs_ops *fs)
{
    PFSCOOKIE root;
    LONG rc;

    if ((drive < 0) || (drive >= BLKDEVNUM))
        return EDRIVE;

    if (!fs || !fs->root)
        return EINVFN;

    rc = fs->root(fs, drive, &root);
    if (rc < 0)
        return rc;
    if (fs->release)
        fs->release(&root);

    pfs_drive[drive] = fs;
    drvbits |= (1L << drive);

    return E_OK;
}

/* Which driver serves 'drive'?  Anything nobody explicitly claimed
 * falls back to the built-in FAT code, lazily mounted on first access
 * exactly like ckdrv() does today - fat_pfs_ops.root() takes care of
 * that the first time it is actually called for a given drive.
 */
static struct pfs_ops *pfs_drive_fs(WORD drive)
{
    if ((drive < 0) || (drive >= BLKDEVNUM))
        return NULL;

    return pfs_drive[drive] ? pfs_drive[drive] : &fat_pfs_ops;
}


/* ------------------------------------------------------------------ */
/* path helpers                                                       */
/* ------------------------------------------------------------------ */

/* GEMDOS drive letter (0=A:) an absolute or relative path names, and a
 * pointer just past any "X:" prefix.  Defaults to the current drive.
 */
static WORD pfs_path_drive(const char *path, const char **rest)
{
    WORD drive = run->p_curdrv;

    if (path[0] && (path[1] == ':'))
    {
        char c = path[0];
        if ((c >= 'a') && (c <= 'z'))
            c -= 'a' - 'A';
        drive = c - 'A';
        path += 2;
    }

    *rest = path;
    return drive;
}

/* Split 'path' (already past any drive prefix) into the directory
 * portion (copied into 'dirbuf', empty string if 'path' has no
 * separator) and returns a pointer to the final component within
 * 'path' itself.
 */
static const char *pfs_split(const char *path, char *dirbuf, int dirbuflen)
{
    const char *last = NULL;
    const char *p;
    int len;

    for (p = path; *p; p++)
        if (*p == SLASH)
            last = p;

    if (!last)
    {
        dirbuf[0] = 0;
        return path;
    }

    len = (int)(last - path);
    if (len >= dirbuflen)
        len = dirbuflen - 1;
    memcpy(dirbuf, path, len);
    dirbuf[len] = 0;

    return last + 1;
}

/* Simple DOS-style '?'/'*' wildcard match, case-insensitive, applied
 * separately to the base and extension (split at the last '.').  '*'
 * matches the rest of the segment it appears in; '?' matches exactly
 * one character.  Both 'name' and 'pattern' are already GEMDOS 8.3
 * names, as guaranteed by pfs_ops.readdir()'s contract.
 */
static BOOL pfs_seg_match(const char *name, const char *pattern)
{
    for (;;)
    {
        if (*pattern == '*')
            return TRUE;        /* matches whatever remains */

        if (*pattern == 0)
            return *name == 0;

        if (*name == 0)
            return FALSE;

        if ((*pattern != '?') &&
            (toupper((UBYTE)*pattern) != toupper((UBYTE)*name)))
            return FALSE;

        name++;
        pattern++;
    }
}

static BOOL pfs_match(const char *name, const char *pattern)
{
    char nbase[9], next[4], pbase[9], pext[4];
    const char *dot;
    int len;

    dot = strchr(name, '.');
    len = dot ? (int)(dot - name) : (int)strlen(name);
    if (len > 8) len = 8;
    memcpy(nbase, name, len); nbase[len] = 0;
    strlcpy(next, dot ? dot + 1 : "", sizeof(next));

    dot = strchr(pattern, '.');
    len = dot ? (int)(dot - pattern) : (int)strlen(pattern);
    if (len > 8) len = 8;
    memcpy(pbase, pattern, len); pbase[len] = 0;
    strlcpy(pext, dot ? dot + 1 : "", sizeof(pext));

    return pfs_seg_match(nbase, pbase) && pfs_seg_match(next, pext);
}


/* ------------------------------------------------------------------ */
/* current-directory tracking                                         */
/* ------------------------------------------------------------------ */

/*
 * Reuses PD.p_curdir[] (already part of the real Atari basepage layout,
 * sized BLKDEVNUM) as an index into this table instead of the legacy
 * dirtbl[] - safe because fat_pfs_ops never relies on ambient
 * CWD/dirtbl[] state (it always reconstructs absolute paths, see
 * fs/fatfs_pfs.c), so dirtbl[] is simply unused for the lifetime of a
 * pluggable-fs build.  Index 0 is reserved to mean "not cached yet, use
 * this drive's root" - which is exactly what a freshly Pexec()'d
 * process's zero-initialised p_curdir[] already reads as.
 */
#define PFS_MAX_CWD 16

typedef struct {
    struct pfs_ops *fs;
    WORD drive;
    WORD use;
    PFSCOOKIE cwd;
    char path[LEN_ZPATH];      /* absolute, no drive prefix, no leading
                                 * or trailing backslash; empty = root */
} PFS_DIRTBL_ENTRY;

static PFS_DIRTBL_ENTRY pfs_dirtbl[PFS_MAX_CWD];

/* One more process now shares pfs_dirtbl[n] - e.g. bdos/proc.c's
 * init_pd_files() inheriting p_curdir[] into a freshly created process,
 * mirroring the bump it already does to the legacy dirtbl[n].use for a
 * non-pluggable drive. n==0 (root sentinel) is a no-op, matching how it
 * is never a real slot.
 */
void pfs_cwd_addref(WORD n)
{
    if ((n > 0) && (n < PFS_MAX_CWD) && pfs_dirtbl[n].use)
        pfs_dirtbl[n].use++;
}

/* Drop this process's reference to pfs_dirtbl[n] (n==0, the root
 * sentinel, is a no-op); releases the driver's cookie once nothing
 * references the slot any more.  Shared by pfs_cwd_set() (replacing a
 * process's own cached directory) and pfs_proc_exit() (process
 * termination).
 */
static void pfs_dirtbl_release(WORD n)
{
    if ((n > 0) && (n < PFS_MAX_CWD) && pfs_dirtbl[n].use)
    {
        if (!--pfs_dirtbl[n].use)
        {
            if (pfs_dirtbl[n].fs->release)
                pfs_dirtbl[n].fs->release(&pfs_dirtbl[n].cwd);
            pfs_dirtbl[n].fs = NULL;
        }
    }
}

/* Cookie + absolute path string for 'drive' in the calling process. */
static LONG pfs_cwd_get(struct pfs_ops *fs, WORD drive, PFSCOOKIE *out, const char **path)
{
    WORD n = run->p_curdir[drive];

    if ((n > 0) && (n < PFS_MAX_CWD) && pfs_dirtbl[n].use &&
        (pfs_dirtbl[n].fs == fs) && (pfs_dirtbl[n].drive == drive))
    {
        *out = pfs_dirtbl[n].cwd;
        *path = pfs_dirtbl[n].path;
        return E_OK;
    }

    if (!fs->root)
        return EINVFN;

    *path = "";
    return fs->root(fs, drive, out);
}

/* Record a newly-resolved current directory for 'drive', replacing
 * whatever slot the process previously had for it.
 */
static LONG pfs_cwd_set(WORD drive, struct pfs_ops *fs, PFSCOOKIE *cwd, const char *path)
{
    WORD old = run->p_curdir[drive];
    WORD i;

    pfs_dirtbl_release(old);

    if (!path[0])
    {
        /* root: no slot needed, the sentinel (0) already means this */
        run->p_curdir[drive] = 0;
        return E_OK;
    }

    for (i = 1; i < PFS_MAX_CWD; i++)
        if (!pfs_dirtbl[i].use)
            break;
    if (i == PFS_MAX_CWD)
        return ENSMEM;

    pfs_dirtbl[i].fs = fs;
    pfs_dirtbl[i].drive = drive;
    pfs_dirtbl[i].use = 1;
    pfs_dirtbl[i].cwd = *cwd;
    strlcpy(pfs_dirtbl[i].path, path, sizeof(pfs_dirtbl[i].path));

    run->p_curdir[drive] = i;

    return E_OK;
}


/* ------------------------------------------------------------------ */
/* Fsfirst/Fsnext search state                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    DTA *owner;         /* NULL = free slot */
    PD *proc;
    PFSCOOKIE dir;
    LONG cursor;
    UWORD attr;
    char pattern[LEN_ZFNAME];
} PFS_SEARCH;

static PFS_SEARCH pfs_searches[CONF_PFS_MAX_SEARCHES];

static void pfs_search_free(PFS_SEARCH *s)
{
    if (s->dir.fs && s->dir.fs->release)
        s->dir.fs->release(&s->dir);
    s->owner = NULL;
    s->proc = NULL;
}

/* Is a directory entry with attribute byte 'entry_attr' visible to an
 * Fsfirst/Fsnext search whose caller-supplied filter is 'searchattr'?
 * Standard GEMDOS rule: FA_RO/FA_ARCHIVE entries are always visible;
 * FA_HIDDEN/FA_SYSTEM/FA_SUBDIR/FA_VOL entries are visible only if the
 * search filter also sets that bit.
 */
static BOOL pfs_attr_visible(UWORD entry_attr, UWORD searchattr)
{
    UWORD gated = entry_attr & (FA_HIDDEN | FA_SYSTEM | FA_SUBDIR | FA_VOL);

    return (gated & ~searchattr) == 0;
}

static void pfs_attr_to_dta(DTAINFO *dt, const char *name, const PFSATTR *a)
{
    dt->dt_fattr = (char)a->dos_attr;
    dt->dt_td.time = a->time;
    dt->dt_td.date = a->date;
    dt->dt_fileln = (long)a->size;
    strlcpy(dt->dt_fname, name, sizeof(dt->dt_fname));
}


/* ------------------------------------------------------------------ */
/* dir-cookie resolution for a path-based call                        */
/* ------------------------------------------------------------------ */

/* Resolve the containing directory of 'path' (drive prefix already
 * stripped) into 'dircookie', and return a pointer to the final
 * component.  Absolute paths (leading backslash) resolve from the
 * drive's root; relative paths resolve from the cached current
 * directory.
 */
static LONG pfs_resolve_dir(struct pfs_ops *fs, WORD drive, const char *path,
                             PFSCOOKIE *dircookie, const char **name)
{
    char dirpart[LEN_ZPATH];
    const char *base;
    PFSCOOKIE start;
    const char *startpath;
    LONG rc;

    *name = pfs_split(path, dirpart, sizeof(dirpart));

    if (path[0] == SLASH)
    {
        if (!fs->root)
            return EINVFN;
        rc = fs->root(fs, drive, &start);
        base = dirpart[0] ? dirpart + 1 : dirpart;     /* skip the leading slash */
    }
    else
    {
        rc = pfs_cwd_get(fs, drive, &start, &startpath);
        base = dirpart;
    }
    if (rc < 0)
        return rc;

    if (!base[0])
    {
        *dircookie = start;
        return E_OK;
    }

    if (!fs->lookup)
        return EPTHNF;

    rc = fs->lookup(&start, base, dircookie);
    if (fs->release)
        fs->release(&start);

    return rc;
}


/* ------------------------------------------------------------------ */
/* per-call handlers                                                  */
/* ------------------------------------------------------------------ */

static LONG pfs_do_dfree(WORD drv, ULONG *buf)
{
    struct pfs_ops *fs;
    WORD drive = drv ? (WORD)(drv - 1) : run->p_curdrv;

    fs = pfs_drive_fs(drive);
    if (!fs)
        return EDRIVE;
    if (!fs->dfree)
        return EINVFN;

    return fs->dfree(fs, drive, buf);
}

static LONG pfs_do_mkdir(const char *path)
{
    struct pfs_ops *fs;
    WORD drive = pfs_path_drive(path, &path);
    PFSCOOKIE dir;
    const char *name;
    LONG rc;

    fs = pfs_drive_fs(drive);
    if (!fs)
        return EDRIVE;

    rc = pfs_resolve_dir(fs, drive, path, &dir, &name);
    if (rc < 0)
        return rc;

    rc = fs->mkdir ? fs->mkdir(&dir, name) : EACCDN;
    if (fs->release)
        fs->release(&dir);

    return rc;
}

static LONG pfs_do_rmdir(const char *path)
{
    struct pfs_ops *fs;
    WORD drive = pfs_path_drive(path, &path);
    PFSCOOKIE dir;
    const char *name;
    LONG rc;

    fs = pfs_drive_fs(drive);
    if (!fs)
        return EDRIVE;

    rc = pfs_resolve_dir(fs, drive, path, &dir, &name);
    if (rc < 0)
        return rc;

    rc = fs->rmdir ? fs->rmdir(&dir, name) : EACCDN;
    if (fs->release)
        fs->release(&dir);

    return rc;
}

static LONG pfs_do_chdir(const char *path)
{
    struct pfs_ops *fs;
    WORD drive = pfs_path_drive(path, &path);
    PFSCOOKIE dir;
    const char *name;
    PFSCOOKIE target;
    char newpath[LEN_ZPATH];
    LONG rc;

    fs = pfs_drive_fs(drive);
    if (!fs)
        return EDRIVE;

    /* the whole path is a directory path here - resolve it exactly like
     * pfs_resolve_dir()'s containing-directory step, but for the full
     * path rather than stopping one component short. */
    if (path[0] == SLASH)
    {
        if (!fs->root)
            return EINVFN;
        rc = fs->root(fs, drive, &dir);
        name = path[1] ? path + 1 : path;
        newpath[0] = 0;
    }
    else
    {
        const char *cwdpath;
        rc = pfs_cwd_get(fs, drive, &dir, &cwdpath);
        name = path;
        strlcpy(newpath, cwdpath, sizeof(newpath));
    }
    if (rc < 0)
        return rc;

    if (!name[0])
    {
        rc = pfs_cwd_set(drive, fs, &dir, newpath);
        if (fs->release)
            fs->release(&dir);
        return rc;
    }

    if (!fs->lookup)
    {
        if (fs->release)
            fs->release(&dir);
        return EPTHNF;
    }

    rc = fs->lookup(&dir, name, &target);
    if (fs->release)
        fs->release(&dir);
    if (rc < 0)
        return rc;

    /* append "\name" (or just "name" if newpath is still empty, i.e. the
     * new directory is directly under the drive root) to the cached
     * path string; a path that doesn't fit is an error, not something
     * to silently truncate - this string is what Dgetpath() returns and
     * what later relative lookups are built from. */
    {
        size_t len = strlen(newpath);
        size_t namelen = strlen(name);
        size_t sep = len ? 1 : 0;

        if (len + sep + namelen >= sizeof(newpath))
        {
            rc = ERANGE;
        }
        else
        {
            if (sep)
                newpath[len++] = SLASH;
            memcpy(newpath + len, name, namelen + 1);      /* + null terminator */
            rc = pfs_cwd_set(drive, fs, &target, newpath);
        }
    }

    if (fs->release)
        fs->release(&target);

    return rc;
}

static LONG pfs_do_getdir(char *buf, WORD drv)
{
    struct pfs_ops *fs;
    WORD drive = drv ? (WORD)(drv - 1) : run->p_curdrv;
    PFSCOOKIE cwd;
    const char *path;
    LONG rc;

    fs = pfs_drive_fs(drive);
    if (!fs)
        return EDRIVE;

    rc = pfs_cwd_get(fs, drive, &cwd, &path);
    if (rc < 0)
    {
        *buf = 0;
        return rc;
    }
    if (fs->release)
        fs->release(&cwd);

    strlcpy(buf, path, LEN_ZPATH);

    return E_OK;
}

static LONG pfs_alloc_handle(PFSCOOKIE *fc)
{
    WORD i;

    for (i = 0; i < OPNFILES; i++)
        if (!sft[i].f_own)
            break;
    if (i == OPNFILES)
        return ENHNDL;

    sft[i].f_own = run;
    sft[i].f_use = 1;
    sft[i].f_ofd = NULL;
    sft[i].f_pfs = *fc;

    return i + NUMSTD;
}

static LONG pfs_do_open(const char *path, WORD mode)
{
    struct pfs_ops *fs;
    WORD drive = pfs_path_drive(path, &path);
    PFSCOOKIE dir, fc;
    const char *name;
    LONG rc;

    fs = pfs_drive_fs(drive);
    if (!fs)
        return EDRIVE;

    rc = pfs_resolve_dir(fs, drive, path, &dir, &name);
    if (rc < 0)
        return rc;

    rc = fs->open ? fs->open(&dir, name, mode, &fc) : EACCDN;
    if (fs->release)
        fs->release(&dir);
    if (rc < 0)
        return rc;

    if (fs->native_handles)
        return fc.index;

    fc.pos = 0;
    rc = pfs_alloc_handle(&fc);
    if (rc < 0)
    {
        if (fs->close) fs->close(&fc);
        if (fs->release) fs->release(&fc);
    }

    return rc;
}

static LONG pfs_do_create(const char *path, UWORD attr)
{
    struct pfs_ops *fs;
    WORD drive = pfs_path_drive(path, &path);
    PFSCOOKIE dir, fc;
    const char *name;
    LONG rc;

    fs = pfs_drive_fs(drive);
    if (!fs)
        return EDRIVE;

    rc = pfs_resolve_dir(fs, drive, path, &dir, &name);
    if (rc < 0)
        return rc;

    rc = fs->create ? fs->create(&dir, name, attr, &fc) : EACCDN;
    if (fs->release)
        fs->release(&dir);
    if (rc < 0)
        return rc;

    if (fs->native_handles)
        return fc.index;

    fc.pos = 0;
    rc = pfs_alloc_handle(&fc);
    if (rc < 0)
    {
        if (fs->close) fs->close(&fc);
        if (fs->release) fs->release(&fc);
    }

    return rc;
}

static LONG pfs_do_unlink(const char *path)
{
    struct pfs_ops *fs;
    WORD drive = pfs_path_drive(path, &path);
    PFSCOOKIE dir;
    const char *name;
    LONG rc;

    fs = pfs_drive_fs(drive);
    if (!fs)
        return EDRIVE;

    rc = pfs_resolve_dir(fs, drive, path, &dir, &name);
    if (rc < 0)
        return rc;

    rc = fs->remove ? fs->remove(&dir, name) : EACCDN;
    if (fs->release)
        fs->release(&dir);

    return rc;
}

static LONG pfs_do_chmod(const char *path, WORD wrt, WORD mod)
{
    struct pfs_ops *fs;
    WORD drive = pfs_path_drive(path, &path);
    PFSCOOKIE dir;
    const char *name;
    UWORD attr;
    LONG rc;

    fs = pfs_drive_fs(drive);
    if (!fs)
        return EDRIVE;

    rc = pfs_resolve_dir(fs, drive, path, &dir, &name);
    if (rc < 0)
        return rc;

    attr = (UWORD)mod;
    rc = fs->chattr ? fs->chattr(&dir, name, wrt ? TRUE : FALSE, &attr) : EINVFN;
    if (fs->release)
        fs->release(&dir);
    if (rc < 0)
        return rc;

    return attr;
}

static LONG pfs_do_rename(const char *p1, const char *p2)
{
    struct pfs_ops *fs;
    WORD drive1 = pfs_path_drive(p1, &p1);
    WORD drive2 = pfs_path_drive(p2, &p2);
    PFSCOOKIE dir1, dir2;
    const char *name1, *name2;
    LONG rc;

    if (drive1 != drive2)
        return ENSAME;

    fs = pfs_drive_fs(drive1);
    if (!fs)
        return EDRIVE;

    rc = pfs_resolve_dir(fs, drive1, p1, &dir1, &name1);
    if (rc < 0)
        return rc;

    rc = pfs_resolve_dir(fs, drive1, p2, &dir2, &name2);
    if (rc < 0)
    {
        if (fs->release) fs->release(&dir1);
        return rc;
    }

    rc = fs->rename ? fs->rename(&dir1, name1, &dir2, name2) : EACCDN;
    if (fs->release)
    {
        fs->release(&dir1);
        fs->release(&dir2);
    }

    return rc;
}

static LONG pfs_do_sfirst(char *path, WORD att)
{
    struct pfs_ops *fs;
    WORD drive = pfs_path_drive(path, (const char **)&path);
    PFSCOOKIE dir;
    const char *name;
    WORD i;
    LONG rc;

    fs = pfs_drive_fs(drive);
    if (!fs)
        return EDRIVE;

    rc = pfs_resolve_dir(fs, drive, path, &dir, &name);
    if (rc < 0)
        return rc;

    /* a new Fsfirst() on a DTA that already has a search running (common
     * - callers rarely exhaust a search before starting another) must
     * replace it, not leak a second slot and leave pfs_do_snext()
     * matching whichever of the two comes first in the table. */
    for (i = 0; i < CONF_PFS_MAX_SEARCHES; i++)
        if ((pfs_searches[i].owner == run->p_xdta) && (pfs_searches[i].proc == run))
            break;
    if (i < CONF_PFS_MAX_SEARCHES)
    {
        pfs_search_free(&pfs_searches[i]);
    }
    else
    {
        for (i = 0; i < CONF_PFS_MAX_SEARCHES; i++)
            if (!pfs_searches[i].owner)
                break;
        if (i == CONF_PFS_MAX_SEARCHES)
        {
            if (fs->release) fs->release(&dir);
            return ENHNDL;
        }
    }

    pfs_searches[i].owner = run->p_xdta;
    pfs_searches[i].proc = run;
    pfs_searches[i].dir = dir;
    pfs_searches[i].cursor = 0;
    pfs_searches[i].attr = att;
    strlcpy(pfs_searches[i].pattern, name, sizeof(pfs_searches[i].pattern));

    if (!fs->readdir)
    {
        pfs_search_free(&pfs_searches[i]);
        return EPTHNF;
    }

    for (;;)
    {
        char name8_3[LEN_ZFNAME];
        PFSATTR attr;

        rc = fs->readdir(&pfs_searches[i].dir, &pfs_searches[i].cursor,
                          name8_3, sizeof(name8_3), &attr);
        if (rc < 0)
        {
            pfs_search_free(&pfs_searches[i]);
            /* Fsfirst() reports "nothing matched" as EFILNF - ENMFIL
             * ("no more files") is Fsnext()'s exhaustion code, not
             * Fsfirst()'s, matching xsfirst()'s own convention
             * (bdos/fsdir.c). */
            return (rc == ENMFIL) ? EFILNF : rc;
        }

        if (pfs_match(name8_3, pfs_searches[i].pattern) &&
            pfs_attr_visible(attr.dos_attr, att))
        {
            pfs_attr_to_dta((DTAINFO *)run->p_xdta, name8_3, &attr);
            return E_OK;
        }
    }
}

static LONG pfs_do_snext(void)
{
    WORD i;

    for (i = 0; i < CONF_PFS_MAX_SEARCHES; i++)
        if ((pfs_searches[i].owner == run->p_xdta) && (pfs_searches[i].proc == run))
            break;
    if (i == CONF_PFS_MAX_SEARCHES)
        return ENMFIL;

    for (;;)
    {
        char name8_3[LEN_ZFNAME];
        PFSATTR attr;
        struct pfs_ops *fs = pfs_searches[i].dir.fs;
        LONG rc;

        if (!fs || !fs->readdir)
        {
            pfs_search_free(&pfs_searches[i]);
            return ENMFIL;
        }

        rc = fs->readdir(&pfs_searches[i].dir, &pfs_searches[i].cursor,
                          name8_3, sizeof(name8_3), &attr);
        if (rc < 0)
        {
            pfs_search_free(&pfs_searches[i]);
            return rc;
        }

        if (pfs_match(name8_3, pfs_searches[i].pattern) &&
            pfs_attr_visible(attr.dos_attr, pfs_searches[i].attr))
        {
            pfs_attr_to_dta((DTAINFO *)run->p_xdta, name8_3, &attr);
            return E_OK;
        }
    }
}

void pfs_proc_exit(PD *r)
{
    WORD i;

    for (i = 0; i < CONF_PFS_MAX_SEARCHES; i++)
        if (pfs_searches[i].proc == r)
            pfs_search_free(&pfs_searches[i]);

    /*
     * decrement pfs_dirtbl[] usage for every drive this process cached a
     * current directory for - p_curdir[] indexes pfs_dirtbl[] here, not
     * the legacy dirtbl[] (see the comment above PFS_MAX_CWD), so this
     * needs its own loop rather than reusing bdos/proc.c's
     * decr_curdir_usage() calls, which stay harmlessly pointed at the
     * (otherwise unused, while this option is on) legacy table.
     */
    for (i = 0; i < BLKDEVNUM; i++)
        pfs_dirtbl_release(r->p_curdir[i]);
}


/* ------------------------------------------------------------------ */
/* handle-based dispatch (Fread/Fwrite/Fclose)                        */
/* ------------------------------------------------------------------ */

LONG pfs_handle_read(PFSCOOKIE *fc, LONG len, UBYTE *buf)
{
    LONG n;

    if (!fc->fs->read)
        return EACCDN;

    n = fc->fs->read(fc, fc->pos, len, buf);
    if (n > 0)
        fc->pos += n;

    return n;
}

LONG pfs_handle_write(PFSCOOKIE *fc, LONG len, const UBYTE *buf)
{
    LONG n;

    if (!fc->fs->write)
        return EACCDN;

    n = fc->fs->write(fc, fc->pos, len, buf);
    if (n > 0)
        fc->pos += n;

    return n;
}

LONG pfs_handle_close(PFSCOOKIE *fc)
{
    LONG rc = fc->fs->close ? fc->fs->close(fc) : E_OK;

    if (fc->fs->release)
        fc->fs->release(fc);

    return rc;
}


/* ------------------------------------------------------------------ */
/* top-level dispatch                                                  */
/* ------------------------------------------------------------------ */

#ifdef __arm__
LONG pfs_dispatch(WORD fn, LONG *pw)
{
    switch (fn)
    {
    case FN_DFREE:
        return pfs_do_dfree((WORD)pw[2], (ULONG *)pw[1]);
    case FN_DCREATE:
        return pfs_do_mkdir((char *)pw[1]);
    case FN_DDELETE:
        return pfs_do_rmdir((char *)pw[1]);
    case FN_DSETPATH:
        return pfs_do_chdir((char *)pw[1]);
    case FN_FCREATE:
        return pfs_do_create((char *)pw[1], (UWORD)pw[2]);
    case FN_FOPEN:
        return pfs_do_open((char *)pw[1], (WORD)pw[2]);
    case FN_FDELETE:
        return pfs_do_unlink((char *)pw[1]);
    case FN_FATTRIB:
        return pfs_do_chmod((char *)pw[1], (WORD)pw[2], (WORD)pw[3]);
    case FN_DGETPATH:
        return pfs_do_getdir((char *)pw[1], (WORD)pw[2]);
    case FN_FSFIRST:
        return pfs_do_sfirst((char *)pw[1], (WORD)pw[2]);
    case FN_FSNEXT:
        return pfs_do_snext();
    case FN_FRENAME:
        return pfs_do_rename((char *)pw[2], (char *)pw[3]);
    default:
        return EINVFN;
    }
}
#else
LONG pfs_dispatch(WORD fn, WORD *pw)
{
    switch (fn)
    {
    case FN_DFREE:
        return pfs_do_dfree(pw[3], *(ULONG **)&pw[1]);
    case FN_DCREATE:
        return pfs_do_mkdir(*(char **)&pw[1]);
    case FN_DDELETE:
        return pfs_do_rmdir(*(char **)&pw[1]);
    case FN_DSETPATH:
        return pfs_do_chdir(*(char **)&pw[1]);
    case FN_FCREATE:
        return pfs_do_create(*(char **)&pw[1], pw[3]);
    case FN_FOPEN:
        return pfs_do_open(*(char **)&pw[1], pw[3]);
    case FN_FDELETE:
        return pfs_do_unlink(*(char **)&pw[1]);
    case FN_FATTRIB:
        return pfs_do_chmod(*(char **)&pw[1], pw[3], pw[4]);
    case FN_DGETPATH:
        return pfs_do_getdir(*(char **)&pw[1], pw[3]);
    case FN_FSFIRST:
        return pfs_do_sfirst(*(char **)&pw[1], pw[3]);
    case FN_FSNEXT:
        return pfs_do_snext();
    case FN_FRENAME:
        return pfs_do_rename(*(char **)&pw[2], *(char **)&pw[4]);
    default:
        return EINVFN;
    }
}
#endif
