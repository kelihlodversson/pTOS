/*
 * Virtio-9p host directories through the native FreeMiNT XFS interface.
 * GPL version 2 or later. See doc/license.txt.
 */
#include "config.h"

#if CONF_WITH_VIRTIO_9P

#include "portab.h"
#include "xfs_pfs.h"
#include "fs.h"
#include "gemerror.h"
#include "kprint.h"
#include "string.h"
#include "virtio_9p.h"

#define V9P_MAX_NAME       255
#define V9P_O_CREAT        0100UL
#define V9P_O_TRUNC        01000UL
#define V9P_NO_FID         ((ULONG)~0UL)

/* Cookie state is private to its owned fid. A directory entry can survive
 * a host-side deletion during listing, in which case it has no fid. */
#define V9P_COOKIE_DIR     0x01
#define V9P_COOKIE_OPEN    0x02
#define V9P_COOKIE_DIRENT  0x04

static FILESYS v9p_filesys;

static void v9p_cookie(fcookie *out, UWORD drive, ULONG fid, UWORD flags)
{
    out->fs = &v9p_filesys;
    out->dev = drive;
    out->index = (long)fid;
    out->aux = flags;
}

/* ------------------------------------------------------------------ */
/* GEMDOS 8.3 long<->short name mapping                                */
/* ------------------------------------------------------------------ */

/* Case-insensitive equality - include/string.h has no plain strcasecmp(),
 * only strncasecmp() (which would need an arbitrarily large 'n' to behave
 * like strcasecmp()), so this is simplest as its own tiny helper. */
static BOOL v9p_streqi(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (toupper((UBYTE)*a) != toupper((UBYTE)*b))
            return FALSE;
        a++;
        b++;
    }
    return (*a == *b) ? TRUE : FALSE;
}

/* Long name ('source') -> GEMDOS 8.3 short name ('dest', capacity
 * 'destlen'). Ported near-verbatim from ParaTos's gemdos/path.c
 * filename8_3() (see #157's plan for why this project, not a fresh
 * reimplementation): splits base/extension at the name's last '.',
 * mangles into 8.3 form, and - if the name doesn't already fit 8.3 -
 * unconditionally overwrites the base's last 4 characters with '~' plus
 * a 3-character tail from a rolling hash of the whole original name, so
 * two different long names can (rarely) collide onto the same short
 * name; accepted as-is, matching ParaTos - see #157's plan for why this
 * isn't addressed for v1. Unlike ParaTos (whose uppercasing depends on
 * the calling process's TOS-vs-MiNT "domain"), this always uppercases:
 * GEMDOS has no equivalent case-preserving domain, and Fsfirst/Fsnext
 * callers always expect uppercase 8.3 names. */
static void v9p_filename8_3(char *dest, int destlen, const char *source)
{
    BOOL needshash = FALSE;
    int slen;
    const char *dot = NULL;
    const char *s;
    char buf[13];
    char *d = buf;
    int i;

    for (slen = 0; source[slen]; slen++)
        ;

    if ((source[0] == '.') && ((slen == 1) || ((slen == 2) && (source[1] == '.'))))
    {
        strlcpy(dest, source, (size_t)destlen);
        return;
    }

    /* Scans backward for the last '.' without ever forming a pointer
     * before 'source' - checking (s >= source) after an unconditional
     * s-- would do exactly that on the final step, which is undefined
     * behavior even though every real target here has flat, unsegmented
     * memory where it would cause no actual fault. */
    if (slen > 0)
    {
        s = source + slen - 1;
        for (;;)
        {
            if (*s == '.')
            {
                dot = s;
                break;
            }
            if (s == source)
                break;
            s--;
        }
    }
    if (dot == source)
        dot = NULL;     /* a leading dot doesn't count as an extension separator */

    if ((slen > 12) ||
        (dot && (((dot - source) > 8) || ((slen - (int)(dot - source)) > 4))) ||
        (!dot && (slen > 8)))
    {
        needshash = TRUE;
    }

    s = source;
    for (i = 0; i < 8; i++)
    {
        if (!*s || (dot && (s == dot)))
        {
            if (needshash)
                for (; i < 8; i++)
                    *d++ = '~';
            break;
        }
        if (*s == '.')
        {
            /* an embedded dot before the real extension separator */
            *d++ = '_';
            s++;
            needshash = TRUE;
        }
        else
        {
            *d++ = (char)toupper((UBYTE)*s);
            s++;
        }
    }

    if (dot)
    {
        *d++ = '.';
        s = dot + 1;
        for (i = 0; i < 3; i++)
        {
            if (!*s)
                break;
            *d++ = (char)toupper((UBYTE)*s);
            s++;
        }
    }
    *d = 0;

    if (needshash)
    {
        static const char alphabet[32] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345";
        UWORD hash = 0xf00f;

        for (s = source; s[1]; s++)
            hash = (UWORD)((hash << 3) ^ (hash >> 5) ^ (UBYTE)*s ^ ((UWORD)(UBYTE)s[1] << 8));
        hash = (UWORD)((hash << 3) ^ (hash >> 5) ^ (UBYTE)*s);

        buf[4] = '~';
        buf[5] = alphabet[(hash >> 10) & 0x1f];
        buf[6] = alphabet[(hash >> 5) & 0x1f];
        buf[7] = alphabet[hash & 0x1f];
    }

    strlcpy(dest, buf, (size_t)destlen);
}

/* ------------------------------------------------------------------ */
/* directory-listing cache for short->long name resolution              */
/* ------------------------------------------------------------------ */

/* Fixed pool, one slot per cached directory listing, amortising
 * v9p_resolve_name()'s linear Treaddir scan across repeated lookups
 * against the same directory - the common case for e.g. a desktop copy
 * of several files out of one source directory, since fs/pfs.c's own
 * current-directory cache (pfs_cwd_get()) keeps handing back the same
 * fid across separate Fopen/Fcreate/... calls for a relative path.
 *
 * Slots are keyed by fid number. Release drops the slot before the
 * transport can reuse the number. Absolute paths and cached current
 * directories can hold different fids for the same host directory, so
 * successful directory mutations invalidate all listings.
 *
 * Cache-assisted, not cache-required: v9p_resolve_name() only ever
 * trusts the cache for a HIT (a positive match), never for a miss - the
 * host directory can change from outside this driver's view at any time
 * (live host directory access while a developer edits files on the host
 * is this whole driver's reason to exist, see #157), so a cached
 * "definitely absent" answer could go stale and let create()/mkdir()/
 * rename() silently do the wrong thing against a name that now exists
 * for real. A miss - whether there is no slot for 'dirfid', or the slot
 * exists but doesn't contain 'name' - always falls back to a live
 * rescan; a directory with more real entries than
 * CONF_VIRTIO_9P_MAX_DIRCACHE_ENTRIES, or a real name too long for
 * V9P_DIRCACHE_NAMELEN, simply never gets that entry cached, so it's
 * always resolved live too. */
#define V9P_DIRCACHE_NAMELEN  32

typedef struct
{
    char short_name[13];
    char real_name[V9P_DIRCACHE_NAMELEN];
} V9P_DIRCACHE_ENTRY;

typedef struct
{
    BOOL used;
    ULONG fid;
    WORD nentries;
    V9P_DIRCACHE_ENTRY entries[CONF_VIRTIO_9P_MAX_DIRCACHE_ENTRIES];
} V9P_DIRCACHE_SLOT;

static V9P_DIRCACHE_SLOT v9p_dircache[CONF_VIRTIO_9P_MAX_DIRCACHE];

static WORD v9p_dircache_find(ULONG fid)
{
    WORD i;

    for (i = 0; i < CONF_VIRTIO_9P_MAX_DIRCACHE; i++)
        if (v9p_dircache[i].used && (v9p_dircache[i].fid == fid))
            return i;

    return -1;
}

/* Other fids can alias the directory changed by this operation. */
static void v9p_dircache_invalidate(void)
{
    WORD i;

    for (i = 0; i < CONF_VIRTIO_9P_MAX_DIRCACHE; i++)
        v9p_dircache[i].used = FALSE;
}

/* Drop the cache before clunk can release the transport's fid slot. */
static LONG v9p_clunk_uncached(ULONG fid)
{
    WORD i = v9p_dircache_find(fid);

    if (i >= 0)
        v9p_dircache[i].used = FALSE;
    return v9p_clunk(fid);
}

/* Finds 'fid's existing slot, or allocates one - first an unused slot,
 * falling back to evicting slot 0 if the pool is full (see #157's plan:
 * "exhaustion evicts slot 0", no recency tracking needed for a handful
 * of slots). Either way the returned slot is reset to empty: a reused
 * slot must start over rather than accumulate duplicate entries across
 * repeated rescans. */
static WORD v9p_dircache_slot_for(ULONG fid)
{
    WORD i = v9p_dircache_find(fid);

    if (i < 0)
    {
        for (i = 0; i < CONF_VIRTIO_9P_MAX_DIRCACHE; i++)
            if (!v9p_dircache[i].used)
                break;
        if (i == CONF_VIRTIO_9P_MAX_DIRCACHE)
            i = 0;
    }

    v9p_dircache[i].used = TRUE;
    v9p_dircache[i].fid = fid;
    v9p_dircache[i].nentries = 0;

    return i;
}

/* Short (8.3, as GEMDOS hands to open()/create()/...) -> long name
 * resolution: checks 'dirfid's directory-listing cache first for a HIT,
 * falling back to a live linear scan of 'dirfid's real entries (ParaTos's
 * tos_path_to_unix() does the same, for the same reason - a directory's
 * real contents are the only source of truth for what a given hash tail
 * actually maps to) whenever the cache doesn't have one, populating/
 * replacing the cache slot as it goes. A cache MISS is never trusted as
 * "definitely absent" by itself - see this section's own comment on why.
 * Matches "." and ".." unchanged, bypassing the cache entirely - they are
 * never real Treaddir entries. */
static LONG v9p_resolve_name(ULONG dirfid, const char *name, char *realname, int realname_len)
{
    UQUAD offset = 0;
    char candidate8_3[13];
    QID9P qid;
    BOOL is_dir;
    LONG rc;
    WORD slot;

    if ((name[0] == '.') && ((name[1] == 0) || ((name[1] == '.') && (name[2] == 0))))
    {
        strlcpy(realname, name, (size_t)realname_len);
        return E_OK;
    }

    slot = v9p_dircache_find(dirfid);
    if (slot >= 0)
    {
        WORD i;

        for (i = 0; i < v9p_dircache[slot].nentries; i++)
            if (v9p_streqi(v9p_dircache[slot].entries[i].short_name, name))
            {
                strlcpy(realname, v9p_dircache[slot].entries[i].real_name, (size_t)realname_len);
                return E_OK;
            }
    }

    /* Cache miss (no slot, or a slot that doesn't contain 'name') -
     * rescan live, replacing whatever data that slot held; a miss here
     * is only ever "not found *right now*", not cached as a standing
     * answer. */
    slot = v9p_dircache_slot_for(dirfid);

    for (;;)
    {
        rc = v9p_readdir_one(dirfid, &offset, realname, realname_len, &qid, &is_dir);
        if (rc < 0)
            return (rc == ENMFIL) ? EFILNF : rc;

        if ((strcmp(realname, ".") == 0) || (strcmp(realname, "..") == 0))
            continue;

        v9p_filename8_3(candidate8_3, sizeof(candidate8_3), realname);

        if ((v9p_dircache[slot].nentries < CONF_VIRTIO_9P_MAX_DIRCACHE_ENTRIES) &&
            (strlen(realname) < V9P_DIRCACHE_NAMELEN))
        {
            WORD n = v9p_dircache[slot].nentries;

            strlcpy(v9p_dircache[slot].entries[n].short_name, candidate8_3,
                    sizeof(v9p_dircache[slot].entries[n].short_name));
            strlcpy(v9p_dircache[slot].entries[n].real_name, realname,
                    sizeof(v9p_dircache[slot].entries[n].real_name));
            v9p_dircache[slot].nentries = (WORD)(n + 1);
        }

        if (v9p_streqi(realname, name) || v9p_streqi(candidate8_3, name))
            return E_OK;                        /* realname already holds the match */
    }
}

static LONG v9p_open_directory(fcookie *dir)
{
    LONG rc;

    if (!(dir->aux & V9P_COOKIE_DIR))
        return EPTHNF;
    if (dir->aux & V9P_COOKIE_OPEN)
        return E_OK;
    rc = v9p_lopen((ULONG)dir->index, 0);
    if (rc >= 0)
        dir->aux |= V9P_COOKIE_OPEN;
    return rc;
}

static LONG v9p_resolve_component(fcookie *dir, const char *name, char *realname)
{
    LONG rc = v9p_open_directory(dir);

    if (rc < 0)
        return rc;
    return v9p_resolve_name((ULONG)dir->index, name, realname, V9P_MAX_NAME + 1);
}

static long _cdecl v9p_xfs_release(fcookie *cookie)
{
    ULONG fid = (ULONG)cookie->index;

    cookie->fs = NULL;
    if (fid == V9P_NO_FID)
        return E_OK;
    return v9p_clunk_uncached(fid);
}

static long _cdecl v9p_xfs_root(int drive, fcookie *out)
{
    ULONG fid;
    LONG rc = v9p_walk(v9p_root_fid(), 0, NULL, &fid, NULL);

    if (rc < 0)
        return rc;
    v9p_cookie(out, (UWORD)drive, fid, V9P_COOKIE_DIR);
    rc = v9p_open_directory(out);
    if (rc < 0)
        v9p_xfs_release(out);
    return rc;
}

static long _cdecl v9p_xfs_dup(fcookie *out, fcookie *source)
{
    ULONG fid;
    LONG rc;

    if ((ULONG)source->index == V9P_NO_FID)
        return EIHNDL;
    rc = v9p_walk((ULONG)source->index, 0, NULL, &fid, NULL);
    if (rc >= 0)
        v9p_cookie(out, source->dev, fid, source->aux & V9P_COOKIE_DIR);
    return rc;
}

static long _cdecl v9p_xfs_lookup(fcookie *dir, const char *name, fcookie *out)
{
    char realname[V9P_MAX_NAME + 1];
    const char *component = realname;
    QID9P qid;
    ULONG fid;
    LONG rc;

    if (!strcmp(name, "."))
        return v9p_xfs_dup(out, dir);
    rc = v9p_resolve_component(dir, name, realname);
    if (rc < 0)
        return rc;
    rc = v9p_walk((ULONG)dir->index, 1, &component, &fid, &qid);
    if (rc >= 0)
        v9p_cookie(out, dir->dev, fid, qid.type & V9P_QTDIR ? V9P_COOKIE_DIR : 0);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Unix-epoch -> GEMDOS packed date/time                                */
/* ------------------------------------------------------------------ */

/* GEMDOS's packed date/time (bdos/fsdir.c, bios/clock.c: date = ((year -
 * 1980) << 9) | (month << 5) | day; time = (hour << 11) | (minute << 5)
 * | (second / 2)) has no Unix-epoch-conversion helper anywhere in this
 * tree to reuse - pTOS otherwise only ever deals in RTC year/month/day
 * fields directly, never epoch seconds. The year/month/day half is
 * Howard Hinnant's well-known constant-time "civil_from_days" algorithm
 * (public domain), chosen over a day-counting loop for simplicity and
 * correctness. Clamps to 1980-01-01 for anything before GEMDOS's
 * earliest representable date, and to 2107 (the packed field's 7-bit
 * year-offset limit) for anything absurdly far in the future.
 *
 * Takes a ULONG (32-bit), not the UQUAD mtime_sec Tgetattr actually
 * returns: this is a freestanding build with no libgcc 64-bit
 * division helper linked in (___aeabi_uldivmod is undefined), and
 * 32-bit Unix time doesn't overflow until 2106 anyway - comfortably
 * past this function's own 2107 clamp - so the caller truncates first. */
static void v9p_unixtime_to_gemdos(ULONG unixtime, UWORD *outdate, UWORD *outtime)
{
    LONG days, secs, z, era, doe, yoe, y, doy, mp, d, m;
    UWORD hh, mm, ss;

    if (unixtime < 315532800UL)    /* 1980-01-01 00:00:00 UTC */
    {
        *outdate = (UWORD)((1 << 5) | 1);      /* year offset 0, month 1, day 1 */
        *outtime = 0;
        return;
    }

    days = (LONG)(unixtime / 86400UL);
    secs = (LONG)(unixtime % 86400UL);

    z = days + 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    y = yoe + era * 400;
    doy = doe - (365*yoe + yoe/4 - yoe/100);
    mp = (5*doy + 2) / 153;
    d = doy - (153*mp + 2)/5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2) ? 1 : 0;

    if (y < 1980)
        y = 1980;
    if (y > 2107)
        y = 2107;

    hh = (UWORD)(secs / 3600);
    mm = (UWORD)((secs / 60) % 60);
    ss = (UWORD)(secs % 60);

    *outdate = (UWORD)(((y - 1980) << 9) | (m << 5) | d);
    *outtime = (UWORD)((hh << 11) | (mm << 5) | (ss / 2));
}

/* ------------------------------------------------------------------ */
/* attributes                                                           */
/* ------------------------------------------------------------------ */

static ULONG v9p_dos_count(UQUAD value)
{
    return value > UINT32_MAX ? UINT32_MAX : (ULONG)value;
}

static long _cdecl v9p_xfs_getxattr(fcookie *cookie, XATTR *out)
{
    ULONG fid = (ULONG)cookie->index;
    BOOL directory = (cookie->aux & V9P_COOKIE_DIR) != 0;
    P9GETATTR attr;
    LONG rc = fid == V9P_NO_FID ? EFILNF : v9p_getattr(fid, &attr);

    memset(out, 0, sizeof(*out));
    out->dev = cookie->dev;
    out->attr = directory ? FA_SUBDIR : 0;
    if (rc < 0) {
        /* Preserve the entry and its directory bit when the host removes
         * it between Treaddir and metadata lookup. */
        if (!(cookie->aux & V9P_COOKIE_DIRENT))
            return rc;
        out->mode = directory ? XFS_IFDIR : XFS_IFREG;
        return E_OK;
    }
    out->mode = (UWORD)attr.mode;
    if (!(attr.mode & 0200))
        out->attr |= FA_RO;
    out->size = (long)v9p_dos_count(attr.size);
    v9p_unixtime_to_gemdos((ULONG)attr.mtime_sec, &out->mdate, &out->mtime);
    return E_OK;
}

static long _cdecl v9p_xfs_creat(fcookie *dir, const char *name, unsigned mode,
                                int attr, fcookie *out)
{
    ULONG fid;
    LONG rc;

    (void)mode;
    (void)attr; /* Keep the existing 0644 host default. */
    /* Tlcreate replaces the directory fid with an opened file fid. */
    rc = v9p_walk((ULONG)dir->index, 0, NULL, &fid, NULL);
    if (rc < 0)
        return rc;
    rc = v9p_lcreate(fid, name, RW_MODE | V9P_O_CREAT | V9P_O_TRUNC, 0644, NULL);
    if (rc < 0) {
        v9p_clunk(fid);
        return rc;
    }
    v9p_dircache_invalidate();
    v9p_cookie(out, dir->dev, fid, V9P_COOKIE_OPEN);
    return E_OK;
}

static long _cdecl v9p_dev_open(FILEPTR *file)
{
    ULONG flags = file->flags & MODE_FAC;
    LONG rc;

    if (file->fc.aux & V9P_COOKIE_OPEN)
        return E_OK; /* Tlcreate has already opened a new file. */
    if (file->flags & XFS_TRUNC)
        flags |= V9P_O_TRUNC;
    rc = v9p_lopen((ULONG)file->fc.index, flags);
    if (rc >= 0)
        file->fc.aux |= V9P_COOKIE_OPEN;
    return rc;
}

static long _cdecl v9p_dev_close(FILEPTR *file, int pid)
{
    /* FILESYS.release owns the consuming Tclunk for this cookie. */
    (void)file;
    (void)pid;
    return E_OK;
}

static long _cdecl v9p_dev_read(FILEPTR *file, char *buffer, long len)
{
    ULONG fid = (ULONG)file->fc.index;
    UQUAD pos = (UQUAD)file->pos;
    LONG total = 0;

    while (len > 0) {
        LONG rc = v9p_read(fid, pos + total, (ULONG)len, (UBYTE *)buffer + total);

        if (rc < 0)
            return rc;
        if (!rc)
            break;
        total += rc;
        len -= rc;
    }
    file->pos += total;
    return total;
}

static long _cdecl v9p_dev_write(FILEPTR *file, const char *buffer, long len)
{
    ULONG fid = (ULONG)file->fc.index;
    UQUAD pos = (UQUAD)file->pos;
    LONG total = 0;

    while (len > 0) {
        LONG rc = v9p_write(fid, pos + total, (ULONG)len, (const UBYTE *)buffer + total);

        if (rc < 0)
            return rc;
        if (!rc)
            break;
        total += rc;
        len -= rc;
    }
    file->pos += total;
    return total;
}

static long _cdecl v9p_dev_seek(FILEPTR *file, long offset, int whence)
{
    P9GETATTR attr;
    LONG rc = v9p_getattr((ULONG)file->fc.index, &attr);

    if (rc < 0)
        return rc;
    rc = pfs_bounded_seek((LONG)file->pos, v9p_dos_count(attr.size), (LONG)offset, (WORD)whence);
    if (rc >= 0)
        file->pos = rc;
    return rc;
}

static DEVDRV v9p_dev = {
    .open = v9p_dev_open,
    .read = v9p_dev_read,
    .write = v9p_dev_write,
    .lseek = v9p_dev_seek,
    .close = v9p_dev_close
};

static DEVDRV * _cdecl v9p_xfs_getdev(fcookie *file, long *special)
{
    (void)file;
    *special = 0;
    return &v9p_dev;
}

/* DIR owns the wire offset. fsstuff is byte storage with no alignment
 * promise. The adapter duplicates fc before calling clone_dir. */
static long _cdecl v9p_xfs_rewinddir(DIR *dir)
{
    memset(dir->fsstuff, 0, sizeof(UQUAD));
    dir->index = 0;
    return E_OK;
}

static long _cdecl v9p_xfs_opendir(DIR *dir, int flags)
{
    LONG rc;

    if (flags != TOS_SEARCH)
        return EINVFN;
    rc = v9p_open_directory(&dir->fc);
    if (rc < 0)
        return rc;
    dir->flags = (UWORD)flags;
    return v9p_xfs_rewinddir(dir);
}

static long _cdecl v9p_xfs_readdir(DIR *dir, char *name, int size, fcookie *out)
{
    char realname[V9P_MAX_NAME + 1];
    char alias[13];
    const char *component = realname;
    ULONG dirfid = (ULONG)dir->fc.index, fid;
    UQUAD offset;
    BOOL directory;
    LONG rc;

    if (size <= 0)
        return ERANGE;
    memcpy(&offset, dir->fsstuff, sizeof(offset));
    do {
        rc = v9p_readdir_one(dirfid, &offset, realname, sizeof(realname), NULL, &directory);
        if (rc < 0)
            return rc;
    } while (!strcmp(realname, ".") || !strcmp(realname, ".."));

    v9p_filename8_3(alias, sizeof(alias), realname);
    if (strlcpy(name, alias, size) >= (size_t)size)
        return ERANGE;

    /* A missing attribute fid is tolerated for one directory entry only. */
    rc = v9p_walk(dirfid, 1, &component, &fid, NULL);
    v9p_cookie(out, dir->fc.dev, rc < 0 ? V9P_NO_FID : fid,
               V9P_COOKIE_DIRENT | (directory ? V9P_COOKIE_DIR : 0));
    memcpy(dir->fsstuff, &offset, sizeof(offset));
    dir->index++;
    return E_OK;
}

static long _cdecl v9p_xfs_closedir(DIR *dir)
{
    (void)dir; /* The directory fid remains owned by fc until release. */
    return E_OK;
}

static long _cdecl v9p_xfs_clone_dir(DIR *out, const DIR *source)
{
    LONG rc = v9p_xfs_opendir(out, source->flags);

    if (rc < 0)
        return rc;
    memcpy(out->fsstuff, source->fsstuff, sizeof(UQUAD));
    out->index = source->index;
    return E_OK;
}

/* ------------------------------------------------------------------ */
/* mkdir/rmdir/remove/rename                                            */
/* ------------------------------------------------------------------ */

/* Unlike Tlcreate, Tmkdir never repurposes 'dfid' - no throwaway dup
 * needed. The new subdirectory's real name is 'name' verbatim (GEMDOS's
 * own 8.3 form, uppercase) - no long-name synthesis on write, matching
 * v9p_xfs_creat()'s own scope for v1. */
static long _cdecl v9p_xfs_mkdir(fcookie *dir, const char *name, unsigned mode)
{
    char realname[V9P_MAX_NAME + 1];
    LONG rc;

    (void)mode; /* Preserve the existing host default permissions. */

    /* GEMDOS Dcreate() must fail (EACCDN, matching xrename()'s own
     * "destination already exists" convention in bdos/fsdir.c) if 'name'
     * already exists under its 8.3 mapping - a literal Tmkdir alone
     * would not catch an existing entry that only matches after
     * short<->long resolution (e.g. a case difference on a
     * case-sensitive host). */
    rc = v9p_resolve_component(dir, name, realname);
    if (rc >= 0)
        return EACCDN;
    if (rc != EFILNF)
        return rc;

    rc = v9p_mkdir((ULONG)dir->index, name, 0755, NULL);

    if (rc >= 0)
        v9p_dircache_invalidate();

    return rc;
}

static long _cdecl v9p_xfs_rmdir(fcookie *dir, const char *name)
{
    char realname[V9P_MAX_NAME + 1];
    LONG rc;

    rc = v9p_resolve_component(dir, name, realname);
    if (rc < 0)
        return rc;

    rc = v9p_unlinkat((ULONG)dir->index, realname, V9P_AT_REMOVEDIR);
    if (rc >= 0)
        v9p_dircache_invalidate();

    return rc;
}

static long _cdecl v9p_xfs_remove(fcookie *dir, const char *name)
{
    char realname[V9P_MAX_NAME + 1];
    LONG rc;

    rc = v9p_resolve_component(dir, name, realname);
    if (rc < 0)
        return rc;

    rc = v9p_unlinkat((ULONG)dir->index, realname, 0);
    if (rc >= 0)
        v9p_dircache_invalidate();

    return rc;
}

static long _cdecl v9p_xfs_rename(fcookie *olddir, char *oldname,
                            fcookie *newdir, const char *newname)
{
    char realoldname[V9P_MAX_NAME + 1];
    char realnewname[V9P_MAX_NAME + 1];
    LONG rc;

    rc = v9p_resolve_component(olddir, oldname, realoldname);
    if (rc < 0)
        return rc;

    /* GEMDOS Frename() must fail (EACCDN, see xrename()'s own
     * ixsfirst()-based check in bdos/fsdir.c) if 'newname' already
     * exists - unlike Trenameat, which follows POSIX rename(2) semantics
     * and would otherwise silently replace an existing target (or, via
     * an 8.3 mapping that only matches after resolution, create a
     * case/long-name-variant duplicate instead of detecting the
     * collision at all). 'newname' itself is passed through as GEMDOS
     * gave it (an 8.3 name) once confirmed free, same "no long-name
     * synthesis on write" scope as create()/mkdir(). */
    rc = v9p_resolve_component(newdir, newname, realnewname);
    if (rc >= 0)
        return EACCDN;
    if (rc != EFILNF)
        return rc;

    rc = v9p_renameat((ULONG)olddir->index, realoldname, (ULONG)newdir->index, newname);
    if (rc >= 0)
        v9p_dircache_invalidate();

    return rc;
}

static long _cdecl v9p_xfs_dfree(fcookie *dir, long *out)
{
    P9STATFS stat;
    LONG rc = v9p_statfs((ULONG)dir->index, &stat);

    if (rc < 0)
        return rc;
    out[0] = (long)v9p_dos_count(stat.bfree);
    out[1] = (long)v9p_dos_count(stat.blocks);
    out[2] = (long)stat.bsize;
    out[3] = 1;
    return E_OK;
}

static long _cdecl v9p_xfs_ready(const FILEPTR *file, UWORD events)
{
    (void)file;
    (void)events;
    return EINVFN; /* The existing 9p provider has no readiness contract. */
}

static FILESYS v9p_filesys = {
    .root = v9p_xfs_root,
    .lookup = v9p_xfs_lookup,
    .creat = v9p_xfs_creat,
    .getdev = v9p_xfs_getdev,
    .getxattr = v9p_xfs_getxattr,
    .mkdir = v9p_xfs_mkdir,
    .rmdir = v9p_xfs_rmdir,
    .remove = v9p_xfs_remove,
    .rename = v9p_xfs_rename,
    .opendir = v9p_xfs_opendir,
    .readdir = v9p_xfs_readdir,
    .rewinddir = v9p_xfs_rewinddir,
    .closedir = v9p_xfs_closedir,
    .dfree = v9p_xfs_dfree,
    .release = v9p_xfs_release,
    .dupcookie = v9p_xfs_dup
};

void v9p_xfs_init(void)
{
    static struct ptos_xfs_mount mount;
    static const struct ptos_xfs_extensions extensions = {
        .version = PTOS_XFS_EXT_VERSION,
        .size = sizeof(extensions),
        .clone_dir = v9p_xfs_clone_dir,
        .ready = v9p_xfs_ready,
        .flags = PTOS_XFS_LEGACY_OPEN | PTOS_XFS_UNSIGNED_DOS_FIELDS | PTOS_XFS_NO_DOS_ATTRIB
    };
    LONG rc;

    if (!virtio_9p_present())
        return;
    rc = ptos_xfs_mount_drive(CONF_VIRTIO_9P_DRIVE, &mount, &v9p_filesys, &extensions);
    if (rc < 0)
        KDEBUG(("v9p_xfs_init: cannot mount host drive, rc=%ld\n", rc));
}

#endif /* CONF_WITH_VIRTIO_9P */
