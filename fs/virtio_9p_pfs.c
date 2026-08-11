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
 * Stage 3 of #157 added read-only browsing: open()/read()/readdir(), the
 * GEMDOS 8.3 long<->short name mapping (ported from ParaTos's
 * gemdos/path.c, see filename8_3()/v9p_resolve_name() below), and a
 * Unix-epoch-to-GEMDOS-date/time helper for readdir()'s PFSATTR.
 *
 * Stage 4 added the write path (create/write/mkdir/rmdir/remove/rename)
 * and multi-component lookup() (Dsetpath into a subdirectory several
 * levels deep, "."/".." handling). chattr() stays NULL - see fs/pfs.h's
 * own comment on why a lossless DOS-attribute mapping can't be built on
 * top of a Unix mode bit.
 *
 * Stage 5 added a directory-listing cache (v9p_dircache[], see its own
 * comment) amortising v9p_resolve_name()'s short->long scan across
 * repeated lookups against the same directory - correctness-preserving,
 * not new capability: every path that used to hit the server for every
 * single resolution still gets the right answer, just faster on a hit.
 *
 * Stage 6 adds dfree() (Tstatfs) and closes out #157's v1 scope -
 * mkdir/rmdir/remove/rename/dfree round out every pfs_ops entry this
 * driver ever intends to implement; mediach and native_handles were
 * never applicable here (see their own NULL/FALSE below).
 */

#include "config.h"

#if CONF_WITH_VIRTIO_9P

#include "portab.h"
#include "pfs.h"
#include "fs.h"             /* FA_RO, FA_SUBDIR */
#include "gemerror.h"
#include "kprint.h"
#include "string.h"
#include "virtio_9p.h"

#define V9P_MAX_NAME  255   /* generous cap for a single real (long) 9p
                             * directory-entry name - not a GEMDOS limit,
                             * just this adapter's scratch buffer size */

static LONG v9p_pfs_root(struct pfs_ops *fs, WORD drive, PFSCOOKIE *out)
{
    ULONG fid;
    QID9P qid;
    LONG rc;

    (void)drive;    /* this driver only ever serves CONF_VIRTIO_9P_DRIVE */

    rc = v9p_walk(v9p_root_fid(), 0, NULL, &fid, &qid);
    if (rc < 0)
        return rc;

    /* Treaddir requires the fid to already be Tlopen'd - unlike Tgetattr,
     * which works on any walked fid. */
    rc = v9p_lopen(fid, 0);
    if (rc < 0)
    {
        v9p_clunk(fid);
        return rc;
    }

    out->fs = fs;
    out->index = (LONG)fid;
    out->aux = 0;
    out->pos = 0;

    return E_OK;
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

    for (s = source + slen - 1; s >= source; s--)
        if (*s == '.')
        {
            dot = s;
            break;
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
 * Keyed by the directory's *fid number*, not its 9p qid.path as #157's
 * plan originally called for: PFSCOOKIE (fs/pfs.h) has no spare field to
 * stash a qid.path in alongside the fid it already carries in 'index'.
 * This is still fully correct as long as every place that clunks a fid
 * also invalidates its cache slot (v9p_pfs_release() below) - a live fid
 * always names the same directory for its whole lifetime, and once
 * clunked it can never be looked up again, so a later fid that happens
 * to reuse the same pool-slot number can never collide with a stale
 * entry - v9p_dircache_invalidate() has already dropped it by then.
 *
 * Cache-assisted, not cache-required: v9p_resolve_name() falls back to
 * a full uncached scan whenever the cache has no slot for 'dirfid', or
 * the slot exists but isn't 'complete' (the directory has more real
 * entries than CONF_VIRTIO_9P_MAX_DIRCACHE_ENTRIES, or some real name
 * was too long for V9P_DIRCACHE_NAMELEN to hold) - so a small pool only
 * costs speed, never a wrong answer. */
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
    BOOL complete;      /* FALSE: some real entry didn't fit - a MISS
                         * against 'entries' below still means "ask the
                         * server", not "definitely absent" */
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

/* Drops 'fid's cache slot, if any - called whenever 'fid' is clunked
 * (see this section's own comment on why that keeps the fid-keyed cache
 * safe) or whenever this driver mutates a directory it might have
 * cached (create/mkdir/rmdir/remove/either side of rename) - no
 * fine-grained patching, the whole slot just goes stale. */
static void v9p_dircache_invalidate(ULONG fid)
{
    WORD i = v9p_dircache_find(fid);

    if (i >= 0)
        v9p_dircache[i].used = FALSE;
}

/* Finds 'fid's existing slot, or allocates one - first an unused slot,
 * falling back to evicting slot 0 if the pool is full (see #157's plan:
 * "exhaustion evicts slot 0", no recency tracking needed for a handful
 * of slots). Either way the returned slot is reset to empty: a reused
 * existing-but-incomplete slot must start over rather than accumulate
 * duplicate entries across repeated rescans. */
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
    v9p_dircache[i].complete = FALSE;
    v9p_dircache[i].nentries = 0;

    return i;
}

/* Short (8.3, as GEMDOS hands to open()/create()/...) -> long name
 * resolution: checks 'dirfid's directory-listing cache first, falling
 * back to a live linear scan of 'dirfid's real entries (ParaTos's
 * tos_path_to_unix() does the same, for the same reason - a directory's
 * real contents are the only source of truth for what a given hash tail
 * actually maps to) on a miss, populating/replacing the cache slot as it
 * goes. Matches "." and ".." unchanged, bypassing the cache entirely -
 * they are never real Treaddir entries. */
static LONG v9p_resolve_name(ULONG dirfid, const char *name, char *realname, int realname_len)
{
    UQUAD offset = 0;
    char candidate8_3[13];
    QID9P qid;
    BOOL is_dir;
    LONG rc;
    WORD slot;
    BOOL overflowed = FALSE;

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

        if (v9p_dircache[slot].complete)
            return EFILNF;      /* every real entry is accounted for -
                                 * a miss here is a genuine miss */
    }

    /* Cache miss (no slot, or an incomplete one with no hit) - rescan
     * from scratch, replacing whatever partial data that slot held. */
    slot = v9p_dircache_slot_for(dirfid);

    for (;;)
    {
        rc = v9p_readdir_one(dirfid, &offset, realname, realname_len, &qid, &is_dir);
        if (rc < 0)
        {
            if (rc == ENMFIL)
            {
                v9p_dircache[slot].complete = !overflowed;
                return EFILNF;
            }
            return rc;
        }

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
        else
        {
            overflowed = TRUE;  /* won't fit - this slot can never be
                                 * trusted for a "definitely absent"
                                 * answer, even if we otherwise reach
                                 * end-of-directory below */
        }

        if (v9p_streqi(realname, name) || v9p_streqi(candidate8_3, name))
            return E_OK;                        /* realname already holds the match */
    }
}

/* Resolves a possibly multi-component directory path (Dsetpath, or the
 * containing-directory half of a longer Fopen/Fcreate/... path pfs.c's
 * own pfs_resolve_dir() has already split the final component off of) -
 * '.' components are dropped, '..' passed through as a literal Twalk
 * component (accepted by QEMU's 9pfs, verified against a real device
 * during Stage 4), everything else resolved short->long via
 * v9p_resolve_name() before being walked, one component at a time.
 *
 * One Twalk per component rather than batching up to the protocol's own
 * 16-component cap: unlike a plain 9p client, this driver can't hand the
 * server a batch of wnames up front, because resolving component N's
 * short name requires already having walked to component N-1's fid (to
 * list *its* real directory contents) - the dependency is inherent, not
 * a missed optimization.
 *
 * An empty 'path' still means the "fresh, independently releasable dup
 * of 'dir' itself" contract fs/pfs.h documents - falls out of the loop
 * below running zero times, no separate branch needed. */
static LONG v9p_pfs_lookup(PFSCOOKIE *dir, const char *path, PFSCOOKIE *out)
{
    ULONG curfid = (ULONG)dir->index;
    BOOL owned = FALSE;
    QID9P qid;
    LONG rc;
    const char *p = path;

    while (*p)
    {
        char comp[V9P_MAX_NAME + 1];
        char realname[V9P_MAX_NAME + 1];
        const char *wname[1];
        ULONG newfid;
        int i = 0;

        while (*p && (*p != SLASH) && (i < V9P_MAX_NAME))
            comp[i++] = *p++;
        comp[i] = 0;
        if (*p == SLASH)
            p++;

        if (!comp[0] || ((comp[0] == '.') && (comp[1] == 0)))
            continue;

        if ((comp[0] == '.') && (comp[1] == '.') && (comp[2] == 0))
        {
            wname[0] = "..";
        }
        else
        {
            rc = v9p_resolve_name(curfid, comp, realname, sizeof(realname));
            if (rc < 0)
            {
                if (owned)
                    v9p_clunk(curfid);
                return rc;
            }
            wname[0] = realname;
        }

        rc = v9p_walk(curfid, 1, wname, &newfid, &qid);
        if (rc < 0)
        {
            if (owned)
                v9p_clunk(curfid);
            return rc;
        }

        if (owned)
            v9p_clunk(curfid);
        curfid = newfid;
        owned = TRUE;
    }

    if (!owned)
    {
        rc = v9p_walk(curfid, 0, NULL, &curfid, &qid);
        if (rc < 0)
            return rc;
        owned = TRUE;
    }

    /* Same Tlopen-before-Treaddir requirement as v9p_pfs_root() - every
     * directory cookie this driver hands out must remain readdir()-able. */
    rc = v9p_lopen(curfid, 0);
    if (rc < 0)
    {
        v9p_clunk(curfid);
        return rc;
    }

    out->fs = dir->fs;
    out->index = (LONG)curfid;
    out->aux = 0;
    out->pos = 0;

    return E_OK;
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

/* Fetches GEMDOS-style attributes for the single component 'name' inside
 * 'dirfid' - a Twalk+Tgetattr+Tclunk round trip against a throwaway fid,
 * since Tgetattr operates on a fid, not a bare qid (readdir() entries
 * only carry a qid). 'is_dir' is passed in rather than re-derived from
 * the fresh Tgetattr's own qid, since readdir() (this file's only
 * current caller) already knows it for free from the directory entry
 * itself. */
static LONG v9p_pfs_stat(ULONG dirfid, const char *name, BOOL is_dir, PFSATTR *out)
{
    ULONG fid;
    QID9P qid;
    P9GETATTR attr;
    LONG rc;
    const char *wname[1];
    UWORD gdate, gtime;

    wname[0] = name;
    rc = v9p_walk(dirfid, 1, wname, &fid, &qid);
    if (rc < 0)
        return rc;

    rc = v9p_getattr(fid, &attr);
    v9p_clunk(fid);
    if (rc < 0)
        return rc;

    v9p_unixtime_to_gemdos((ULONG)attr.mtime_sec, &gdate, &gtime);

    out->size = (ULONG)attr.size;
    out->dos_attr = (UWORD)((is_dir ? FA_SUBDIR : 0) | ((attr.mode & 0200) ? 0 : FA_RO));
    out->date = gdate;
    out->time = gtime;

    return E_OK;
}

/* ------------------------------------------------------------------ */
/* open/create/read/write                                               */
/* ------------------------------------------------------------------ */

static LONG v9p_pfs_open(PFSCOOKIE *dir, const char *name, WORD mode, PFSCOOKIE *out)
{
    char realname[V9P_MAX_NAME + 1];
    ULONG fid;
    QID9P qid;
    LONG rc;
    const char *wname[1];

    rc = v9p_resolve_name((ULONG)dir->index, name, realname, sizeof(realname));
    if (rc < 0)
        return rc;

    wname[0] = realname;
    rc = v9p_walk((ULONG)dir->index, 1, wname, &fid, &qid);
    if (rc < 0)
        return rc;

    /* GEMDOS's Fopen mode (0/1/2 = RO_MODE/WO_MODE/RW_MODE, bdos/fs.h)
     * maps directly onto Linux's O_RDONLY/O_WRONLY/O_RDWR (0/1/2) - see
     * v9p_lopen()'s own comment in virtio_9p.h. */
    rc = v9p_lopen(fid, (ULONG)mode);
    if (rc < 0)
    {
        v9p_clunk(fid);
        return rc;
    }

    out->fs = dir->fs;
    out->index = (LONG)fid;
    out->aux = 0;
    out->pos = 0;

    return E_OK;
}

static LONG v9p_pfs_read(PFSCOOKIE *fc, LONG pos, LONG len, UBYTE *buf)
{
    LONG total = 0;

    while (len > 0)
    {
        LONG rc = v9p_read((ULONG)fc->index, (UQUAD)pos, (ULONG)len, buf);

        if (rc < 0)
            return rc;
        if (rc == 0)
            break;      /* EOF */

        total += rc;
        buf += rc;
        pos += rc;
        len -= rc;
    }

    return total;
}

/* Linux open(2) flags Tlcreate needs that this driver otherwise has no
 * use for (v9p_lopen() only ever sees GEMDOS's already-Linux-shaped
 * RO/WO/RW mode) - confirmed against include/uapi/asm-generic/fcntl.h. */
#define V9P_O_CREAT   0100UL
#define V9P_O_TRUNC   01000UL

static LONG v9p_pfs_create(PFSCOOKIE *dir, const char *name, UWORD attr, PFSCOOKIE *out)
{
    char realname[V9P_MAX_NAME + 1];
    const char *createname;
    ULONG fid;
    QID9P qid;
    LONG rc;

    (void)attr; /* no lossless GEMDOS-attribute-bits -> 9p mapping at
                 * create time, same reasoning as chattr() staying NULL
                 * (see this file's header comment) */

    /* An existing entry may already map to this 8.3 name without being
     * a literal byte-for-byte match (e.g. a host file "foo.txt", whose
     * filename8_3() form is the same "FOO.TXT" GEMDOS is asking to
     * (re)create) - Tlcreate must target THAT real name so O_TRUNC
     * actually truncates it, not create a second, case/long-name-variant
     * file alongside it on a case-sensitive host. Only a genuinely new
     * name falls back to GEMDOS's own literal (always-uppercase) form. */
    rc = v9p_resolve_name((ULONG)dir->index, name, realname, sizeof(realname));
    if (rc == E_OK)
        createname = realname;
    else if (rc == EFILNF)
        createname = name;
    else
        return rc;

    /* Tlcreate repurposes whatever fid it's given into the new file's
     * fid (see v9p_lcreate()'s own comment in virtio_9p.h) - walk a
     * throwaway dup of the directory rather than handing it dir->index
     * directly, since 'dir' must stay a usable directory cookie. */
    rc = v9p_walk((ULONG)dir->index, 0, NULL, &fid, &qid);
    if (rc < 0)
        return rc;

    /* GEMDOS's Fcreate() always hands back a file open for read+write,
     * truncated to empty whether or not it already existed. */
    rc = v9p_lcreate(fid, createname, 2 /* O_RDWR */ | V9P_O_CREAT | V9P_O_TRUNC, 0644, &qid);
    if (rc < 0)
    {
        v9p_clunk(fid);
        return rc;
    }

    v9p_dircache_invalidate((ULONG)dir->index);

    out->fs = dir->fs;
    out->index = (LONG)fid;
    out->aux = 0;
    out->pos = 0;

    return E_OK;
}

static LONG v9p_pfs_write(PFSCOOKIE *fc, LONG pos, LONG len, const UBYTE *buf)
{
    LONG total = 0;

    while (len > 0)
    {
        LONG rc = v9p_write((ULONG)fc->index, (UQUAD)pos, (ULONG)len, buf);

        if (rc < 0)
            return rc;
        if (rc == 0)
            break;      /* shouldn't happen for a write the server accepted,
                         * but avoid looping forever if it ever does */

        total += rc;
        buf += rc;
        pos += rc;
        len -= rc;
    }

    return total;
}

/* ------------------------------------------------------------------ */
/* readdir                                                              */
/* ------------------------------------------------------------------ */

/* One slot per concurrent Fsfirst/Fsnext search against this driver,
 * exactly mirroring fs/fatfs_pfs.c's fat_readdir_pool[]: a directory
 * cookie's .aux field is (pool index + 1) while a search using it is
 * live, 0 otherwise, so release() can free an abandoned search's slot
 * (see v9p_pfs_release() below). The 9P offset is a full UQUAD and
 * doesn't fit PFSCOOKIE's plain LONG cursor field, hence keeping it
 * here rather than in *cursor directly - see v9p_readdir_one()'s own
 * comment in virtio_9p.h. */
typedef struct
{
    BOOL used;
    UQUAD offset;
} V9P_READDIR_SLOT;

static V9P_READDIR_SLOT v9p_readdir_pool[CONF_PFS_MAX_SEARCHES];

static LONG v9p_pfs_readdir(PFSCOOKIE *dir, LONG *cursor, char *name, int namelen, PFSATTR *outattr)
{
    WORD slot;
    UQUAD offset;
    char realname[V9P_MAX_NAME + 1];
    QID9P qid;
    BOOL is_dir;
    LONG rc;

    if (*cursor == 0)
    {
        WORD i;

        for (i = 0; i < CONF_PFS_MAX_SEARCHES; i++)
            if (!v9p_readdir_pool[i].used)
                break;
        if (i == CONF_PFS_MAX_SEARCHES)
            return ENHNDL;

        v9p_readdir_pool[i].used = TRUE;
        v9p_readdir_pool[i].offset = 0;
        *cursor = i + 1;
        dir->aux = i + 1;
    }

    slot = (WORD)(*cursor - 1);
    if ((slot < 0) || (slot >= CONF_PFS_MAX_SEARCHES) || !v9p_readdir_pool[slot].used)
        return ENMFIL;

    offset = v9p_readdir_pool[slot].offset;

    for (;;)
    {
        rc = v9p_readdir_one((ULONG)dir->index, &offset, realname, sizeof(realname), &qid, &is_dir);
        if (rc < 0)
        {
            if (rc == ENMFIL)
            {
                v9p_readdir_pool[slot].used = FALSE;
                dir->aux = 0;
            }
            return rc;
        }
        if ((strcmp(realname, ".") != 0) && (strcmp(realname, "..") != 0))
            break;
    }

    v9p_readdir_pool[slot].offset = offset;

    v9p_filename8_3(name, namelen, realname);

    rc = v9p_pfs_stat((ULONG)dir->index, realname, is_dir, outattr);
    if (rc < 0)
    {
        /* One entry's attributes failed (e.g. a race with a concurrent
         * host-side delete) - report it with zeroed attributes rather
         * than failing the whole listing over it. */
        outattr->size = 0;
        outattr->dos_attr = (UWORD)(is_dir ? FA_SUBDIR : 0);
        outattr->date = 0;
        outattr->time = 0;
    }

    return E_OK;
}

/* ------------------------------------------------------------------ */
/* mkdir/rmdir/remove/rename                                            */
/* ------------------------------------------------------------------ */

/* Unlike Tlcreate, Tmkdir never repurposes 'dfid' - no throwaway dup
 * needed. The new subdirectory's real name is 'name' verbatim (GEMDOS's
 * own 8.3 form, uppercase) - no long-name synthesis on write, matching
 * v9p_pfs_create()'s own scope for v1. */
static LONG v9p_pfs_mkdir(PFSCOOKIE *dir, const char *name)
{
    char realname[V9P_MAX_NAME + 1];
    LONG rc;

    /* GEMDOS Dcreate() must fail (EACCDN, matching xrename()'s own
     * "destination already exists" convention in bdos/fsdir.c) if 'name'
     * already exists under its 8.3 mapping - a literal Tmkdir alone
     * would not catch an existing entry that only matches after
     * short<->long resolution (e.g. a case difference on a
     * case-sensitive host). */
    rc = v9p_resolve_name((ULONG)dir->index, name, realname, sizeof(realname));
    if (rc >= 0)
        return EACCDN;
    if (rc != EFILNF)
        return rc;

    rc = v9p_mkdir((ULONG)dir->index, name, 0755, NULL);

    if (rc >= 0)
        v9p_dircache_invalidate((ULONG)dir->index);

    return rc;
}

static LONG v9p_pfs_rmdir(PFSCOOKIE *dir, const char *name)
{
    char realname[V9P_MAX_NAME + 1];
    LONG rc;

    rc = v9p_resolve_name((ULONG)dir->index, name, realname, sizeof(realname));
    if (rc < 0)
        return rc;

    rc = v9p_unlinkat((ULONG)dir->index, realname, V9P_AT_REMOVEDIR);
    if (rc >= 0)
        v9p_dircache_invalidate((ULONG)dir->index);

    return rc;
}

static LONG v9p_pfs_remove(PFSCOOKIE *dir, const char *name)
{
    char realname[V9P_MAX_NAME + 1];
    LONG rc;

    rc = v9p_resolve_name((ULONG)dir->index, name, realname, sizeof(realname));
    if (rc < 0)
        return rc;

    rc = v9p_unlinkat((ULONG)dir->index, realname, 0);
    if (rc >= 0)
        v9p_dircache_invalidate((ULONG)dir->index);

    return rc;
}

static LONG v9p_pfs_rename(PFSCOOKIE *olddir, const char *oldname,
                            PFSCOOKIE *newdir, const char *newname)
{
    char realoldname[V9P_MAX_NAME + 1];
    char realnewname[V9P_MAX_NAME + 1];
    LONG rc;

    rc = v9p_resolve_name((ULONG)olddir->index, oldname, realoldname, sizeof(realoldname));
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
    rc = v9p_resolve_name((ULONG)newdir->index, newname, realnewname, sizeof(realnewname));
    if (rc >= 0)
        return EACCDN;
    if (rc != EFILNF)
        return rc;

    rc = v9p_renameat((ULONG)olddir->index, realoldname, (ULONG)newdir->index, newname);
    if (rc >= 0)
    {
        v9p_dircache_invalidate((ULONG)olddir->index);
        v9p_dircache_invalidate((ULONG)newdir->index);
    }

    return rc;
}

/* ------------------------------------------------------------------ */
/* dfree                                                                 */
/* ------------------------------------------------------------------ */

/* Tstatfs takes any fid within the filesystem, not specifically a
 * directory's own - the persistent attach fid v9p_root_fid() already
 * keeps alive for the driver's whole lifetime is enough, no walk
 * needed. */
static LONG v9p_pfs_dfree(struct pfs_ops *fs, WORD drive, ULONG out[4])
{
    P9STATFS stat;
    LONG rc;

    (void)fs;
    (void)drive;    /* this driver only ever serves CONF_VIRTIO_9P_DRIVE */

    rc = v9p_statfs(v9p_root_fid(), &stat);
    if (rc < 0)
        return rc;

    /* GEMDOS's Dfree() has no concept of a 9p block, only DOS clusters -
     * map 1:1, one "cluster" == one 9p block, so out[3] (sectors/cluster)
     * is always 1. stat.blocks/.bfree are a full UQUAD; clamping them
     * into a 32-bit ULONG is a plain narrowing comparison/cast, not a
     * division, so - unlike v9p_unixtime_to_gemdos()'s UQUAD parameter
     * problem - this needs no libgcc 64-bit division helper this
     * freestanding build doesn't link in. */
    out[0] = (stat.bfree > 0xFFFFFFFFUL) ? 0xFFFFFFFFUL : (ULONG)stat.bfree;
    out[1] = (stat.blocks > 0xFFFFFFFFUL) ? 0xFFFFFFFFUL : (ULONG)stat.blocks;
    out[2] = stat.bsize;
    out[3] = 1;

    return E_OK;
}

/* ------------------------------------------------------------------ */

static void v9p_pfs_release(PFSCOOKIE *fc)
{
    if (fc->aux > 0)
    {
        WORD slot = (WORD)(fc->aux - 1);

        if ((slot >= 0) && (slot < CONF_PFS_MAX_SEARCHES))
            v9p_readdir_pool[slot].used = FALSE;
        fc->aux = 0;
    }
    v9p_dircache_invalidate((ULONG)fc->index);
    v9p_clunk((ULONG)fc->index);
}

static struct pfs_ops v9p_pfs_ops = {
    v9p_pfs_root,
    v9p_pfs_lookup,
    v9p_pfs_open,
    v9p_pfs_create,
    NULL,               /* close: nothing to flush - Tclunk (release())
                         * does the real cleanup */
    v9p_pfs_read,
    v9p_pfs_write,
    v9p_pfs_readdir,
    v9p_pfs_mkdir,
    v9p_pfs_rmdir,
    v9p_pfs_remove,
    v9p_pfs_rename,
    NULL,               /* chattr: no lossless GEMDOS-attribute<->9p mapping */
    v9p_pfs_dfree,
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
