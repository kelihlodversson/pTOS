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

    /*
     * Reject re-registering an already-claimed drive rather than
     * silently swapping the driver out from under it - existing
     * handles and pfs_dirtbl[]/pfs_searches[] cookies would keep
     * pointing at the old driver's cookies while pfs_drive[] now
     * dispatches new calls to a different one, an inconsistency no
     * caller here is prepared to reconcile.  A driver that genuinely
     * wants to replace another's claim needs an explicit unregister
     * step - not needed by anything in this tree yet.
     */
    if (pfs_drive[drive])
        return EACCDN;

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
 * separately to the base and extension (split at the name's '.').  '*'
 * matches the rest of the segment it appears in; '?' matches exactly
 * one character.  Both 'name' and 'pattern' are already GEMDOS 8.3
 * names, as guaranteed by pfs_ops.readdir()'s contract - so there is at
 * most one '.', and splitting on the first one found is unambiguous.
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
        {
            /*
             * 'name' ran out but 'pattern' hasn't - the built-in FAT
             * matcher (bdos/fsdir.c's builds()/match()) packs both
             * sides into fixed-width, space-padded 8.3 fields, so a
             * shorter name still has implicit trailing spaces for the
             * rest of 'pattern' to compare against: '?' matches that
             * padding like it matches anything else, a literal space
             * genuinely equals it, and a '*' anywhere in the remainder
             * still matches the (empty) rest - only a literal
             * non-space character is a real mismatch.
             */
            for (; *pattern; pattern++)
            {
                if (*pattern == '*')
                    return TRUE;
                if ((*pattern != '?') && (*pattern != ' '))
                    return FALSE;
            }
            return TRUE;
        }

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
#define PFS_MAX_CWD CONF_PFS_MAX_CWD

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

/* Cookie + absolute path string for 'drive' in the calling process.
 *
 * '*owned' tells the caller whether '*out' is a fresh reference it must
 * release itself (TRUE - the cache missed, so this came straight from
 * fs->root()) or a copy of the cookie pfs_dirtbl[] still owns (FALSE -
 * the cache hit).  Releasing a borrowed copy would tear down a resource
 * the cache still thinks is alive out from under it - harmless for FAT
 * today (release() no-ops for a plain directory cookie), but a real
 * hazard for a driver whose release() actually frees something (e.g. a
 * future 9p driver's fid).
 */
static LONG pfs_cwd_get(struct pfs_ops *fs, WORD drive, PFSCOOKIE *out, const char **path, BOOL *owned)
{
    WORD n = run->p_curdir[drive];

    if ((n > 0) && (n < PFS_MAX_CWD) && pfs_dirtbl[n].use &&
        (pfs_dirtbl[n].fs == fs) && (pfs_dirtbl[n].drive == drive))
    {
        *out = pfs_dirtbl[n].cwd;
        *path = pfs_dirtbl[n].path;
        *owned = FALSE;
        return E_OK;
    }

    if (!fs->root)
        return EINVFN;

    *path = "";
    *owned = TRUE;
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
    BOOL dir_owned;      /* does this slot own 'dir' (must release it),
                          * or is it a borrowed alias of a pfs_dirtbl[]
                          * entry (must not) - see pfs_cwd_get(). */
    LONG cursor;
    UWORD attr;
    char pattern[LEN_ZFNAME];
} PFS_SEARCH;

static PFS_SEARCH pfs_searches[CONF_PFS_MAX_SEARCHES];

static void pfs_search_free(PFS_SEARCH *s)
{
    if (s->dir_owned && s->dir.fs && s->dir.fs->release)
        s->dir.fs->release(&s->dir);
    s->owner = NULL;
    s->proc = NULL;
}

/* Is a directory entry with attribute byte 'entry_attr' visible to an
 * Fsfirst/Fsnext search whose caller-supplied filter is 'searchattr'?
 * Deliberately bug-for-bug matches bdos/fsdir.c's match()/ixsfirst(), not
 * a "clean" reading of the GEMDOS spec, since this is what FAT already
 * does today and this layer has to reproduce that exactly (see
 * fs/fatfs_pfs.c's file header) - including its quirk that an entry
 * combining a gated bit (FA_HIDDEN/FA_SYSTEM/FA_SUBDIR/FA_VOL) with
 * FA_ARCHIVE or FA_RO is visible even when the gated bit wasn't
 * requested, because ixsfirst() ORs FA_ARCHIVE|FA_RO into the search
 * mask before match()'s plain bitwise-AND test - a plain "is the gated
 * subset a subset of the requested bits" check (as this function
 * previously was) does not reproduce that.
 */
static BOOL pfs_attr_visible(UWORD entry_attr, UWORD searchattr)
{
    UWORD effective;

    if (searchattr == FA_VOL)
        return (entry_attr & FA_VOL) != 0;

    effective = searchattr | FA_ARCHIVE | FA_RO;

    if (!entry_attr)
        return TRUE;

    return (effective & entry_attr) != 0;
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
/* Resolve the containing directory of 'path', as above, and report via
 * '*owned' whether the caller must release '*dircookie' itself (TRUE) or
 * it's a borrowed alias of a cookie pfs_dirtbl[] still owns (FALSE - only
 * possible when 'path' is relative and empty, i.e. "." - the resolved
 * directory is the cached current directory itself, handed back as-is).
 * Every caller must gate its own fs->release(dircookie) call on *owned,
 * exactly like pfs_cwd_get() (which this delegates the same hazard to
 * for the relative-path case) already documents.
 */
static LONG pfs_resolve_dir(struct pfs_ops *fs, WORD drive, const char *path,
                             PFSCOOKIE *dircookie, const char **name, BOOL *owned)
{
    char dirpart[LEN_ZPATH];
    const char *base;
    PFSCOOKIE start;
    const char *startpath;
    BOOL start_owned;
    LONG rc;

    *name = pfs_split(path, dirpart, sizeof(dirpart));

    if (path[0] == SLASH)
    {
        if (!fs->root)
            return EINVFN;
        rc = fs->root(fs, drive, &start);
        start_owned = TRUE;   /* fs->root() always hands back a fresh reference */
        base = dirpart[0] ? dirpart + 1 : dirpart;     /* skip the leading slash */
    }
    else
    {
        rc = pfs_cwd_get(fs, drive, &start, &startpath, &start_owned);
        base = dirpart;
    }
    if (rc < 0)
        return rc;

    if (!base[0])
    {
        /* the resolved directory *is* 'start' - ownership passes through
         * unchanged, whichever way pfs_cwd_get()/fs->root() set it. */
        *dircookie = start;
        *owned = start_owned;
        return E_OK;
    }

    /* every non-empty 'base' means fs->lookup() below produces a brand
     * new cookie, always owned by the caller - regardless of whether
     * 'start' (released right below, only if it was actually ours to
     * release) was borrowed. */
    *owned = TRUE;

    if (!fs->lookup)
    {
        if (start_owned && fs->release)
            fs->release(&start);
        return EPTHNF;
    }

    rc = fs->lookup(&start, base, dircookie);
    if (start_owned && fs->release)
        fs->release(&start);

    return rc;
}


/* ------------------------------------------------------------------ */
/* per-call handlers                                                  */
/* ------------------------------------------------------------------ */

LONG pfs_do_dfree(WORD drv, ULONG *buf)
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

LONG pfs_do_mkdir(const char *path)
{
    struct pfs_ops *fs;
    WORD drive = pfs_path_drive(path, &path);
    PFSCOOKIE dir;
    const char *name;
    BOOL owned;
    LONG rc;

    fs = pfs_drive_fs(drive);
    if (!fs)
        return EDRIVE;

    rc = pfs_resolve_dir(fs, drive, path, &dir, &name, &owned);
    if (rc < 0)
        return rc;

    rc = fs->mkdir ? fs->mkdir(&dir, name) : EACCDN;
    if (owned && fs->release)
        fs->release(&dir);

    return rc;
}

LONG pfs_do_rmdir(const char *path)
{
    struct pfs_ops *fs;
    WORD drive = pfs_path_drive(path, &path);
    PFSCOOKIE dir;
    const char *name;
    BOOL owned;
    LONG rc;

    fs = pfs_drive_fs(drive);
    if (!fs)
        return EDRIVE;

    rc = pfs_resolve_dir(fs, drive, path, &dir, &name, &owned);
    if (rc < 0)
        return rc;

    rc = fs->rmdir ? fs->rmdir(&dir, name) : EACCDN;
    if (owned && fs->release)
        fs->release(&dir);

    return rc;
}

/* Append 'tail' (a possibly multi-component relative path, e.g. what
 * pfs_do_chdir() just handed to fs->lookup()) onto the cached
 * current-directory path string '*newpath' (capacity 'cap'), folding "."
 * and ".." components the way a canonical path does rather than blindly
 * concatenating them.  fs->lookup() itself may resolve those components
 * just fine (whatever tree-walking the driver does for the resolved
 * cookie, e.g. FAT's dcrack()/findit()), but the *cached path string* is
 * built up independently here - Dgetpath() returns it directly - so
 * without this folding it could end up with a dangling ".." component
 * that the built-in FAT implementation's DND-tree-derived Dgetpath()
 * (dopath()) could never produce.  ".." above the root is clamped there,
 * matching dopath()'s own behavior of never walking past a NULL
 * d_parent.  Returns E_OK, or ERANGE if a component wouldn't fit.
 */
static LONG pfs_path_append(char *newpath, size_t cap, const char *tail)
{
    const char *p = tail;

    for (;;)
    {
        const char *start = p;
        size_t len;

        while (*p && (*p != SLASH))
            p++;
        len = (size_t)(p - start);

        if ((len == 1) && (start[0] == '.'))
        {
            /* "." - no-op */
        }
        else if ((len == 2) && (start[0] == '.') && (start[1] == '.'))
        {
            /* pop the last component - no strrchr() in include/string.h,
             * so scan for it directly. */
            char *last = newpath;
            char *q;

            for (q = newpath; *q; q++)
                if (*q == SLASH)
                    last = q;

            if (last != newpath)
                *last = 0;
            else
                newpath[0] = 0;
        }
        else if (len)
        {
            size_t curlen = strlen(newpath);
            size_t sep = curlen ? 1 : 0;

            if (curlen + sep + len >= cap)
                return ERANGE;
            if (sep)
                newpath[curlen++] = SLASH;
            memcpy(newpath + curlen, start, len);
            newpath[curlen + len] = 0;
        }

        if (!*p)
            return E_OK;
        p++;    /* skip the SLASH */
    }
}

LONG pfs_do_chdir(const char *path)
{
    struct pfs_ops *fs;
    WORD drive;
    PFSCOOKIE dir;
    const char *name;
    PFSCOOKIE target;
    char newpath[LEN_ZPATH];
    BOOL dir_owned;
    LONG rc;
    const char *t;

    /* matches legacy xchdir()'s contains_wildcard_characters() check
     * (bdos/fsdir.c) - Dsetpath() is not a search, and a driver's
     * lookup() has no obligation to treat '*'/'?' as anything but
     * literal characters, so without this a wildcard path would either
     * behave inconsistently across drivers or just fail to resolve,
     * instead of the fixed EPTHNF GEMDOS callers can already rely on. */
    for (t = path; *t; t++)
        if ((*t == '*') || (*t == '?'))
            return EPTHNF;

    drive = pfs_path_drive(path, &path);

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
        dir_owned = TRUE;
        name = path + 1;       /* always strip the leading SLASH - even
                                 * when that's all 'path' is ("\\"),
                                 * leaving name[0]==0 so the Dsetpath("\\")
                                 * case below is recognised instead of
                                 * handing a leading-slash string to
                                 * fs->lookup(). */
        newpath[0] = 0;
    }
    else
    {
        const char *cwdpath;
        rc = pfs_cwd_get(fs, drive, &dir, &cwdpath, &dir_owned);
        name = path;
        strlcpy(newpath, cwdpath, sizeof(newpath));
    }
    if (rc < 0)
        return rc;

    if (!name[0])
    {
        /* Dsetpath(".") or Dsetpath("\\") - 'dir' *is* the new current
         * directory.  pfs_cwd_set() copies it into pfs_dirtbl[] on
         * success, taking over whatever reference it held (owned or
         * not) - release it ourselves only if that hand-off didn't
         * happen (cwd_set failed) and it was ours to begin with. */
        rc = pfs_cwd_set(drive, fs, &dir, newpath);
        if ((rc < 0) && dir_owned && fs->release)
            fs->release(&dir);
        return rc;
    }

    if (!fs->lookup)
    {
        if (dir_owned && fs->release)
            fs->release(&dir);
        return EPTHNF;
    }

    /* fs->lookup() always produces a fresh, caller-owned 'target',
     * regardless of whether 'dir' was borrowed. */
    rc = fs->lookup(&dir, name, &target);
    if (dir_owned && fs->release)
        fs->release(&dir);
    if (rc < 0)
        return rc;

    /* fold 'name's components (which may include "." / "..") onto the
     * cached path string - this is what Dgetpath() returns and what
     * later relative lookups are built from, so it needs to stay
     * canonical, not just whatever fs->lookup() was able to resolve. */
    rc = pfs_path_append(newpath, sizeof(newpath), name);
    if (rc >= 0)
        rc = pfs_cwd_set(drive, fs, &target, newpath);

    /* same hand-off logic as above: 'target' is always owned here, but
     * only ours to release if pfs_cwd_set() didn't take it over. */
    if ((rc < 0) && fs->release)
        fs->release(&target);

    return rc;
}

LONG pfs_do_getdir(char *buf, WORD drv)
{
    struct pfs_ops *fs;
    WORD drive = drv ? (WORD)(drv - 1) : run->p_curdrv;
    PFSCOOKIE cwd;
    const char *path;
    BOOL owned;
    LONG rc;

    fs = pfs_drive_fs(drive);
    if (!fs)
        return EDRIVE;

    rc = pfs_cwd_get(fs, drive, &cwd, &path, &owned);
    if (rc < 0)
    {
        *buf = 0;
        return rc;
    }
    if (owned && fs->release)
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

LONG pfs_do_open(const char *path, WORD mode)
{
    struct pfs_ops *fs;
    WORD drive = pfs_path_drive(path, &path);
    PFSCOOKIE dir, fc;
    const char *name;
    BOOL owned;
    LONG rc;

    fs = pfs_drive_fs(drive);
    if (!fs)
        return EDRIVE;

    rc = pfs_resolve_dir(fs, drive, path, &dir, &name, &owned);
    if (rc < 0)
        return rc;

    rc = fs->open ? fs->open(&dir, name, mode, &fc) : EACCDN;
    if (owned && fs->release)
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

LONG pfs_do_create(const char *path, UWORD attr)
{
    struct pfs_ops *fs;
    WORD drive = pfs_path_drive(path, &path);
    PFSCOOKIE dir, fc;
    const char *name;
    BOOL owned;
    LONG rc;

    fs = pfs_drive_fs(drive);
    if (!fs)
        return EDRIVE;

    rc = pfs_resolve_dir(fs, drive, path, &dir, &name, &owned);
    if (rc < 0)
        return rc;

    rc = fs->create ? fs->create(&dir, name, attr, &fc) : EACCDN;
    if (owned && fs->release)
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

LONG pfs_do_unlink(const char *path)
{
    struct pfs_ops *fs;
    WORD drive = pfs_path_drive(path, &path);
    PFSCOOKIE dir;
    const char *name;
    BOOL owned;
    LONG rc;

    fs = pfs_drive_fs(drive);
    if (!fs)
        return EDRIVE;

    rc = pfs_resolve_dir(fs, drive, path, &dir, &name, &owned);
    if (rc < 0)
        return rc;

    rc = fs->remove ? fs->remove(&dir, name) : EACCDN;
    if (owned && fs->release)
        fs->release(&dir);

    return rc;
}

LONG pfs_do_chmod(const char *path, WORD wrt, WORD mod)
{
    struct pfs_ops *fs;
    WORD drive = pfs_path_drive(path, &path);
    PFSCOOKIE dir;
    const char *name;
    UWORD attr;
    BOOL owned;
    LONG rc;

    fs = pfs_drive_fs(drive);
    if (!fs)
        return EDRIVE;

    rc = pfs_resolve_dir(fs, drive, path, &dir, &name, &owned);
    if (rc < 0)
        return rc;

    attr = (UWORD)mod;
    if (fs->chattr)
        rc = fs->chattr(&dir, name, wrt ? TRUE : FALSE, &attr);
    else
        rc = wrt ? EACCDN : EINVFN;    /* unimplemented write op -> EACCDN,
                                         * matching mkdir/remove/rename's
                                         * convention for "not supported by
                                         * this driver"; a plain get with
                                         * nothing to read is EINVFN, as
                                         * before. */
    if (owned && fs->release)
        fs->release(&dir);
    if (rc < 0)
        return rc;

    return attr;
}

LONG pfs_do_rename(const char *p1, const char *p2)
{
    struct pfs_ops *fs;
    WORD drive1 = pfs_path_drive(p1, &p1);
    WORD drive2 = pfs_path_drive(p2, &p2);
    PFSCOOKIE dir1, dir2;
    const char *name1, *name2;
    BOOL owned1, owned2;
    LONG rc;

    if (drive1 != drive2)
        return ENSAME;

    fs = pfs_drive_fs(drive1);
    if (!fs)
        return EDRIVE;

    rc = pfs_resolve_dir(fs, drive1, p1, &dir1, &name1, &owned1);
    if (rc < 0)
        return rc;

    rc = pfs_resolve_dir(fs, drive1, p2, &dir2, &name2, &owned2);
    if (rc < 0)
    {
        if (owned1 && fs->release) fs->release(&dir1);
        return rc;
    }

    rc = fs->rename ? fs->rename(&dir1, name1, &dir2, name2) : EACCDN;
    if (fs->release)
    {
        if (owned1) fs->release(&dir1);
        if (owned2) fs->release(&dir2);
    }

    return rc;
}

LONG pfs_do_sfirst(char *path, WORD att)
{
    struct pfs_ops *fs;
    WORD drive = pfs_path_drive(path, (const char **)&path);
    PFSCOOKIE dir;
    const char *name;
    BOOL owned;
    WORD i;
    LONG rc;

    fs = pfs_drive_fs(drive);
    if (!fs)
        return EDRIVE;

    rc = pfs_resolve_dir(fs, drive, path, &dir, &name, &owned);
    if (rc < 0)
        return rc;

    /*
     * A search's directory cookie lives in pfs_searches[] for as long as
     * the search runs, independently of whatever pfs_resolve_dir()
     * resolved it from - readdir() may attach driver-private state to
     * this exact copy (fat_readdir_pool[], via cookie->aux), which must
     * be freed by pfs_search_free() when the search is abandoned.  That
     * can only happen if this slot owns its cookie outright, so a
     * borrowed one (the cached current directory, see pfs_resolve_dir())
     * is upgraded here to an independent reference via an empty-path
     * lookup() - see fs/pfs.h's "empty path = dup" convention - rather
     * than stored as a bare alias pfs_search_free() must not release.
     */
    if (!owned)
    {
        PFSCOOKIE owned_dir;

        rc = fs->lookup ? fs->lookup(&dir, "", &owned_dir) : EINVFN;
        if (rc < 0)
            return rc;
        dir = owned_dir;
        owned = TRUE;
    }

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
            if (owned && fs->release) fs->release(&dir);
            return ENHNDL;
        }
    }

    pfs_searches[i].owner = run->p_xdta;
    pfs_searches[i].proc = run;
    pfs_searches[i].dir = dir;
    pfs_searches[i].dir_owned = owned;
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

LONG pfs_do_snext(void)
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

/* Only calls the driver's close(), never release() - a single cookie can
 * be referenced by several sft[] entries at once (std-handle redirection
 * via Fforce()/ixforce() bumps the same slot's f_use; xdup() copies the
 * cookie into a second slot), so release() must wait until the caller
 * (bdos/fsopnclo.c's xclose()) sees the last reference go away, exactly
 * like the legacy OFD path calls ixclose() on every close but only frees
 * the sft[] slot itself (sftdel()) once f_use reaches zero.
 */
LONG pfs_handle_close(PFSCOOKIE *fc)
{
    return fc->fs->close ? fc->fs->close(fc) : E_OK;
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
