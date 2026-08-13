/*
 * virtio_9p.c - virtio-9p (9P2000.L) transport/protocol client for the
 * QEMU virt-arm/virt-m68k boards
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * Stage 1 of #157 established MMIO discovery, the mount-tag config-space
 * read, and the Tversion/Rversion handshake, on top of v9p_transact(),
 * the one generic request/response primitive every later stage reuses.
 * Stage 2 adds fid lifecycle management (a fixed pool, one slot per live
 * fid), Tattach (the persistent root fid), Twalk (fid resolution/dup) and
 * Tclunk (fid release) - still no fs/pfs.h registration or open/read/
 * write/readdir, which land in later stages.
 */

/*#define ENABLE_KDEBUG*/

#include "config.h"

#if CONF_WITH_VIRTIO_9P

#include "portab.h"
#include "gemerror.h"
#include "kprint.h"
#include "string.h"
#include "endian.h"
#include "mfp.h"
#include "tosvars.h"
#include "virtio.h"
#include "virtio_9p.h"

#if defined(MACHINE_VIRT_ARM)
#include "virt_memmap.h"
#include "virt_pic.h"
#include "processor.h"
#define VIRTIO_MMIO_BASE    VIRT_VIRTIO_MMIO_BASE
#define VIRTIO_MMIO_STRIDE  VIRT_VIRTIO_MMIO_STRIDE
#define VIRTIO_MMIO_COUNT   VIRT_VIRTIO_MMIO_COUNT
#elif defined(MACHINE_VIRT_M68K)
#include "goldfish_pic.h"
#define VIRTIO_MMIO_BASE    0xff010000UL
#define VIRTIO_MMIO_STRIDE  0x200UL
#define VIRTIO_MMIO_COUNT   128
#endif

/* Confirmed against Linux's include/uapi/linux/virtio_ids.h - not to be
 * confused with any other device id, this value is correctness-critical
 * and was wrong (38) in an earlier draft of this driver's design. */
#define VIRTIO_ID_9P  9

#define V9P_TIMEOUT_MSEC   1000UL
#define V9P_TIMEOUT_TICKS  ((V9P_TIMEOUT_MSEC*CLOCKS_PER_SEC+999)/1000)

/* Every request but Tversion reuses this fixed tag: the driver is fully
 * synchronous (one request in flight at a time, see the design notes in
 * the PR/issue), so there is never more than one outstanding tag to
 * disambiguate. */
#define V9P_NOTAG  0xFFFFU
#define V9P_TAG    0U

#define P9_TVERSION  100
#define P9_RVERSION  101
#define P9_TATTACH   104
#define P9_RATTACH   105
#define P9_TWALK     110
#define P9_RWALK     111
#define P9_TCLUNK    120
#define P9_RCLUNK    121
#define P9_TLOPEN    12
#define P9_RLOPEN    13
#define P9_TREAD     116
#define P9_RREAD     117
#define P9_TGETATTR  24
#define P9_RGETATTR  25
#define P9_TREADDIR  40
#define P9_RREADDIR  41
#define P9_TLCREATE  14
#define P9_RLCREATE  15
#define P9_TWRITE    118
#define P9_RWRITE    119
#define P9_TMKDIR    72
#define P9_RMKDIR    73
#define P9_TUNLINKAT 76
#define P9_RUNLINKAT 77
#define P9_TRENAMEAT 74
#define P9_RRENAMEAT 75
#define P9_TSTATFS   8
#define P9_RSTATFS   9
#define P9_RLERROR   7

#define V9P_MAX_MOUNT_TAG  32   /* QEMU's own MAX_TAG_LEN */
#define V9P_NOFID    0xFFFFFFFFUL
#define V9P_MAXWELEM 16         /* 9P2000.L's own per-Twalk component cap */
#define V9P_QTDIR    0x80       /* qid.type bit: this qid is a directory */
#define P9_GETATTR_BASIC  ((UQUAD)0x7ff)  /* mode|nlink|uid|gid|rdev|atime|mtime|ctime|ino|size|blocks */

static VIRTIO_DEV v9p_dev;

/* Latched on a request timeout: the abandoned request may still complete
 * later into a buffer a subsequent call has since reused (same hazard
 * virtio_blk_failed[] guards against in bios/virtio_blk.c) - refuse
 * further requests rather than keep trusting the shared Tbuf/Rbuf. */
static BOOL v9p_failed;

/* Negotiated with the device via Tversion; never larger than
 * sizeof(v9p_tbuf)/sizeof(v9p_rbuf). */
static ULONG v9p_msize;

static UBYTE v9p_tbuf[CONF_VIRTIO_9P_MSIZE] __attribute__((aligned(VIRTIO_CACHE_LINE)));
static UBYTE v9p_rbuf[CONF_VIRTIO_9P_MSIZE] __attribute__((aligned(VIRTIO_CACHE_LINE)));

static char v9p_mount_tag[V9P_MAX_MOUNT_TAG + 1];

/* Fixed pool of fid numbers: the pool slot index doubles as the fid
 * number sent to the server, so allocation needs no counter - the same
 * index-as-handle idiom fs/fatfs_pfs.c uses for its own readdir-cursor
 * pool. Each PFSCOOKIE this driver ever hands out owns exactly one fid
 * (see the "empty path = dup" contract in fs/pfs.h - a dup is just
 * another pool slot from a fresh Twalk); there is no refcounting. */
typedef struct
{
    BOOL used;
    QID9P qid;
} V9P_FID_SLOT;

static V9P_FID_SLOT v9p_fid_pool[CONF_VIRTIO_9P_MAX_FIDS];

/* The one fid Tattach establishes at init and that lives for the whole
 * driver lifetime - root() Twalks from this rather than re-attaching. */
static ULONG v9p_attach_fid;
static BOOL v9p_attached;

/* ------------------------------------------------------------------ */
/* wire-format helpers - 9P integers are little-endian on the wire,    */
/* strings are a u16 length prefix + raw bytes, never NUL-terminated.  */
/* ------------------------------------------------------------------ */

static UBYTE *v9p_put8(UBYTE *p, UBYTE v)
{
    *p = v;
    return p + 1;
}

static UBYTE *v9p_put16(UBYTE *p, UWORD v)
{
    UWORD le = cpu2le16(v);
    memcpy(p, &le, 2);
    return p + 2;
}

static UBYTE *v9p_put32(UBYTE *p, ULONG v)
{
    ULONG le = cpu2le32(v);
    memcpy(p, &le, 4);
    return p + 4;
}

static UBYTE *v9p_put64(UBYTE *p, UQUAD v)
{
    UQUAD le = cpu2le64(v);
    memcpy(p, &le, 8);
    return p + 8;
}

static UBYTE *v9p_putstr(UBYTE *p, const char *s)
{
    UWORD len = (UWORD)strlen(s);
    p = v9p_put16(p, len);
    memcpy(p, s, len);
    return p + len;
}

static const UBYTE *v9p_get8(const UBYTE *p, UBYTE *out)
{
    *out = *p;
    return p + 1;
}

static const UBYTE *v9p_get16(const UBYTE *p, UWORD *out)
{
    UWORD le;
    memcpy(&le, p, 2);
    *out = le2cpu16(le);
    return p + 2;
}

static const UBYTE *v9p_get32(const UBYTE *p, ULONG *out)
{
    ULONG le;
    memcpy(&le, p, 4);
    *out = le2cpu32(le);
    return p + 4;
}

static const UBYTE *v9p_get64(const UBYTE *p, UQUAD *out)
{
    UQUAD le;
    memcpy(&le, p, 8);
    *out = le2cpu64(le);
    return p + 8;
}

static const UBYTE *v9p_getqid(const UBYTE *p, QID9P *out)
{
    p = v9p_get8(p, &out->type);
    p = v9p_get32(p, &out->version);
    p = v9p_get64(p, &out->path);
    return p;
}

/* Rlerror's body is a Linux errno, not a gemerror.h code or a 9p-legacy
 * string - map the handful this driver can actually distinguish a
 * response for; anything else falls back to EGENRL. Not exhaustive by
 * design (see #157's plan: build out mappings against real failures as
 * later stages exercise them, not speculatively). */
static LONG v9p_errno_to_gemerror(ULONG errno_val)
{
    switch (errno_val)
    {
    case 2:  return EFILNF;    /* ENOENT */
    case 13: return EACCDN;    /* EACCES */
    case 17: return EACCDN;    /* EEXIST - closest available BDOS code */
    case 20: return EPTHNF;    /* ENOTDIR */
    case 21: return EACCDN;    /* EISDIR */
    case 22: return ERANGE;    /* EINVAL */
    case 28: return ENSMEM;    /* ENOSPC */
    case 30: return EACCDN;    /* EROFS */
    case 36: return ERANGE;    /* ENAMETOOLONG */
    case 39: return EACCDN;    /* ENOTEMPTY */
    default: return EGENRL;
    }
}

/* ------------------------------------------------------------------ */
/* device config space (offset 0x100, raw MMIO - bypasses the transport */
/* API entirely, same as virtio_blk's/virtio_input's own config reads)  */
/* ------------------------------------------------------------------ */

static void v9p_read_mount_tag(ULONG base)
{
    volatile UWORD *taglen_reg = (volatile UWORD *)(base + 0x100);
    volatile UBYTE *tag_reg = (volatile UBYTE *)(base + 0x102);
    UWORD taglen;
    UWORD i;

    taglen = le2cpu16(*taglen_reg);
    if (taglen > V9P_MAX_MOUNT_TAG)
    {
        KDEBUG(("virtio_9p: mount tag too long (%u > %d), truncating\n",
                taglen, V9P_MAX_MOUNT_TAG));
        taglen = V9P_MAX_MOUNT_TAG;
    }
    for (i = 0; i < taglen; i++)
        v9p_mount_tag[i] = (char)tag_reg[i];
    v9p_mount_tag[taglen] = 0;
}

/* ------------------------------------------------------------------ */
/* generic synchronous request/response                                */
/* ------------------------------------------------------------------ */

/* Sends the 'len'-byte message already built in v9p_tbuf (size field
 * included) and waits for a reply into v9p_rbuf.  Returns the reply
 * length on success, or a negative gemerror.h code. */
static LONG v9p_transact(ULONG len)
{
    ULONG phys_offset = v9p_dev.phys_offset;
    ULONG idx, rlen;
    LONG timeout;

    if (v9p_failed)
        return EDRVNR;

    virtio_desc_set(&v9p_dev, 0, (ULONG)v9p_tbuf + phys_offset, len,
                     VIRTIO_DESC_F_NEXT, 1);
    virtio_desc_set(&v9p_dev, 1, (ULONG)v9p_rbuf + phys_offset, v9p_msize,
                     VIRTIO_DESC_F_WRITE, 0);

#if ARCH_ARM
    flush_data_cache(v9p_tbuf, (long)len);
#endif

    virtio_submit(&v9p_dev, 0);
    virtio_notify(&v9p_dev);

    timeout = hz_200 + V9P_TIMEOUT_TICKS;
    while (!v9p_dev.done)
    {
        if (hz_200 >= timeout)
        {
            v9p_failed = TRUE;
            KDEBUG(("virtio_9p: request timed out\n"));
            return ETIMEDOUT;
        }
#if ARCH_ARM
        __asm__ volatile("wfi");
#endif
    }

    if (!virtio_pop_used(&v9p_dev, &idx, &rlen))
    {
        /* dev->done was set but the used ring had nothing new - do not
         * trust this state for a follow-up request. */
        v9p_failed = TRUE;
        KDEBUG(("virtio_9p: done set but nothing to pop\n"));
        return EINTRN;
    }
    if (idx != 0)
    {
        /* Only descriptor chain 0 is ever submitted (see above); a
         * different completed index means the device or the transport
         * is in a state this driver does not understand. */
        v9p_failed = TRUE;
        KDEBUG(("virtio_9p: unexpected descriptor index %lu\n", idx));
        return EINTRN;
    }

#if ARCH_ARM
    invalidate_data_cache(v9p_rbuf, (long)rlen);
#endif

    return (LONG)rlen;
}

/* ------------------------------------------------------------------ */
/* Tversion                                                             */
/* ------------------------------------------------------------------ */

static LONG v9p_version(void)
{
    UBYTE *p = v9p_tbuf;
    UBYTE *size_field;
    ULONG len;
    LONG rc;
    const UBYTE *rp;
    UBYTE rtype;
    UWORD rtag;
    ULONG rmsize;
    UWORD verlen;

    size_field = p;
    p += 4;                                  /* size, filled in below */
    p = v9p_put8(p, P9_TVERSION);
    p = v9p_put16(p, V9P_NOTAG);
    p = v9p_put32(p, (ULONG)sizeof(v9p_tbuf));   /* proposed msize */
    p = v9p_putstr(p, "9P2000.L");

    len = (ULONG)(p - v9p_tbuf);
    v9p_put32(size_field, len);

    /* v9p_msize isn't negotiated yet - use the full reply buffer for
     * this one call. */
    v9p_msize = (ULONG)sizeof(v9p_rbuf);

    rc = v9p_transact(len);
    if (rc < 0)
        return rc;

    if ((ULONG)rc < 7)      /* size[4] type[1] tag[2] is the minimum any reply has */
    {
        KDEBUG(("virtio_9p: Tversion reply too short (%ld bytes)\n", rc));
        return EINTRN;
    }

    rp = v9p_rbuf + 4;      /* skip the reply's own size field */
    rp = v9p_get8(rp, &rtype);
    rp = v9p_get16(rp, &rtag);

    if (rtag != V9P_NOTAG)
    {
        KDEBUG(("virtio_9p: Rversion tag 0x%x != NOTAG\n", rtag));
        return EINTRN;
    }

    if (rtype == P9_RLERROR)
    {
        ULONG ecode;
        v9p_get32(rp, &ecode);
        KDEBUG(("virtio_9p: Tversion rejected, errno %lu\n", ecode));
        return v9p_errno_to_gemerror(ecode);
    }
    if (rtype != P9_RVERSION)
    {
        KDEBUG(("virtio_9p: unexpected reply type %u to Tversion\n", rtype));
        return EINTRN;
    }

    rp = v9p_get32(rp, &rmsize);
    rp = v9p_get16(rp, &verlen);

    if ((verlen != 8) || (memcmp(rp, "9P2000.L", 8) != 0))
    {
        KDEBUG(("virtio_9p: device did not accept the 9P2000.L dialect\n"));
        return EACCDN;
    }

    v9p_msize = (rmsize < (ULONG)sizeof(v9p_tbuf)) ? rmsize : (ULONG)sizeof(v9p_tbuf);
    KDEBUG(("virtio_9p: negotiated msize %lu\n", v9p_msize));

    return E_OK;
}

/* ------------------------------------------------------------------ */
/* fid pool                                                             */
/* ------------------------------------------------------------------ */

static LONG v9p_fid_alloc(void)
{
    WORD i;

    for (i = 0; i < CONF_VIRTIO_9P_MAX_FIDS; i++)
        if (!v9p_fid_pool[i].used)
        {
            v9p_fid_pool[i].used = TRUE;
            return i;
        }

    return -1;
}

static void v9p_fid_release_slot(ULONG fid)
{
    if (fid < CONF_VIRTIO_9P_MAX_FIDS)
        v9p_fid_pool[fid].used = FALSE;
}

/* ------------------------------------------------------------------ */
/* Tattach                                                              */
/* ------------------------------------------------------------------ */

static LONG v9p_attach(void)
{
    UBYTE *p = v9p_tbuf;
    UBYTE *size_field;
    ULONG len;
    LONG rc;
    LONG fid;
    const UBYTE *rp;
    UBYTE rtype;
    UWORD rtag;

    fid = v9p_fid_alloc();
    if (fid < 0)
        return ENHNDL;

    size_field = p;
    p += 4;
    p = v9p_put8(p, P9_TATTACH);
    p = v9p_put16(p, V9P_TAG);
    p = v9p_put32(p, (ULONG)fid);
    p = v9p_put32(p, V9P_NOFID);       /* afid: no auth needed for -fsdev local */
    p = v9p_putstr(p, "");             /* uname: pTOS has no multi-user concept */
    p = v9p_putstr(p, "");             /* aname: QEMU's -fsdev local exposes one tree */
    p = v9p_put32(p, 0);               /* n_uname: uid 0 */

    len = (ULONG)(p - v9p_tbuf);
    v9p_put32(size_field, len);

    rc = v9p_transact(len);
    if (rc < 0)
    {
        v9p_fid_release_slot((ULONG)fid);
        return rc;
    }
    if ((ULONG)rc < 7)
    {
        v9p_fid_release_slot((ULONG)fid);
        return EINTRN;
    }

    rp = v9p_rbuf + 4;
    rp = v9p_get8(rp, &rtype);
    rp = v9p_get16(rp, &rtag);

    if (rtag != V9P_TAG)
    {
        KDEBUG(("virtio_9p: Rattach tag 0x%x != 0x%x\n", rtag, V9P_TAG));
        v9p_fid_release_slot((ULONG)fid);
        return EINTRN;
    }
    if (rtype == P9_RLERROR)
    {
        ULONG ecode;
        v9p_get32(rp, &ecode);
        KDEBUG(("virtio_9p: Tattach failed, errno %lu\n", ecode));
        v9p_fid_release_slot((ULONG)fid);
        return v9p_errno_to_gemerror(ecode);
    }
    if (rtype != P9_RATTACH)
    {
        KDEBUG(("virtio_9p: unexpected reply type %u to Tattach\n", rtype));
        v9p_fid_release_slot((ULONG)fid);
        return EINTRN;
    }

    v9p_getqid(rp, &v9p_fid_pool[fid].qid);
    v9p_attach_fid = (ULONG)fid;

    return E_OK;
}

/* ------------------------------------------------------------------ */
/* Twalk                                                                */
/* ------------------------------------------------------------------ */

LONG v9p_walk(ULONG fromfid, WORD nwname, const char *const *wname,
              ULONG *outfid, QID9P *outqid)
{
    UBYTE *p = v9p_tbuf;
    UBYTE *size_field;
    ULONG len;
    LONG rc;
    LONG newfid;
    const UBYTE *rp;
    UBYTE rtype;
    UWORD rtag;
    UWORD nwqid;
    WORD i;
    QID9P qid;

    if ((nwname < 0) || (nwname > V9P_MAXWELEM))
        return EINVFN;

    newfid = v9p_fid_alloc();
    if (newfid < 0)
        return ENHNDL;

    size_field = p;
    p += 4;
    p = v9p_put8(p, P9_TWALK);
    p = v9p_put16(p, V9P_TAG);
    p = v9p_put32(p, fromfid);
    p = v9p_put32(p, (ULONG)newfid);
    p = v9p_put16(p, (UWORD)nwname);
    for (i = 0; i < nwname; i++)
        p = v9p_putstr(p, wname[i]);

    len = (ULONG)(p - v9p_tbuf);
    v9p_put32(size_field, len);

    rc = v9p_transact(len);
    if (rc < 0)
    {
        v9p_fid_release_slot((ULONG)newfid);
        return rc;
    }
    if ((ULONG)rc < 7)
    {
        v9p_fid_release_slot((ULONG)newfid);
        return EINTRN;
    }

    rp = v9p_rbuf + 4;
    rp = v9p_get8(rp, &rtype);
    rp = v9p_get16(rp, &rtag);

    if (rtag != V9P_TAG)
    {
        KDEBUG(("virtio_9p: Rwalk tag 0x%x != 0x%x\n", rtag, V9P_TAG));
        v9p_fid_release_slot((ULONG)newfid);
        return EINTRN;
    }
    if (rtype == P9_RLERROR)
    {
        ULONG ecode;
        v9p_get32(rp, &ecode);
        v9p_fid_release_slot((ULONG)newfid);
        return v9p_errno_to_gemerror(ecode);
    }
    if (rtype != P9_RWALK)
    {
        KDEBUG(("virtio_9p: unexpected reply type %u to Twalk\n", rtype));
        v9p_fid_release_slot((ULONG)newfid);
        return EINTRN;
    }

    rp = v9p_get16(rp, &nwqid);
    if (nwqid != (UWORD)nwname)
    {
        /* Partial walk: the server stopped at a missing intermediate
         * component. Per 9P2000.L, newfid is not created in this case -
         * nothing to Tclunk - but this driver's own pool slot for it
         * still needs freeing. */
        KDEBUG(("virtio_9p: Twalk partial (%u of %d components)\n", nwqid, nwname));
        v9p_fid_release_slot((ULONG)newfid);
        return EPTHNF;
    }

    if (nwqid == 0)
        qid = v9p_fid_pool[fromfid].qid;   /* 0-component walk: qid is fromfid's own */
    else
        for (i = 0; i < nwqid; i++)
            rp = v9p_getqid(rp, &qid);      /* only the last one matters */

    v9p_fid_pool[newfid].qid = qid;
    if (outqid)
        *outqid = qid;
    *outfid = (ULONG)newfid;

    return E_OK;
}

/* ------------------------------------------------------------------ */
/* Tclunk                                                               */
/* ------------------------------------------------------------------ */

LONG v9p_clunk(ULONG fid)
{
    UBYTE *p = v9p_tbuf;
    UBYTE *size_field;
    ULONG len;
    LONG rc;
    const UBYTE *rp;
    UBYTE rtype;
    UWORD rtag;

    size_field = p;
    p += 4;
    p = v9p_put8(p, P9_TCLUNK);
    p = v9p_put16(p, V9P_TAG);
    p = v9p_put32(p, fid);

    len = (ULONG)(p - v9p_tbuf);
    v9p_put32(size_field, len);

    rc = v9p_transact(len);
    /* Release our pool slot regardless of what the server says: a fid we
     * can no longer account for on our side must never be reused. */
    v9p_fid_release_slot(fid);

    if (rc < 0)
        return rc;
    if ((ULONG)rc < 7)
        return EINTRN;

    rp = v9p_rbuf + 4;
    rp = v9p_get8(rp, &rtype);
    rp = v9p_get16(rp, &rtag);

    if (rtag != V9P_TAG)
    {
        KDEBUG(("virtio_9p: Rclunk tag 0x%x != 0x%x\n", rtag, V9P_TAG));
        return EINTRN;
    }
    if (rtype == P9_RLERROR)
    {
        ULONG ecode;
        v9p_get32(rp, &ecode);
        return v9p_errno_to_gemerror(ecode);
    }
    if (rtype != P9_RCLUNK)
    {
        KDEBUG(("virtio_9p: unexpected reply type %u to Tclunk\n", rtype));
        return EINTRN;
    }

    return E_OK;
}

ULONG v9p_root_fid(void)
{
    return v9p_attach_fid;
}

BOOL virtio_9p_present(void)
{
    return v9p_attached;
}

/* ------------------------------------------------------------------ */
/* Tlopen                                                               */
/* ------------------------------------------------------------------ */

LONG v9p_lopen(ULONG fid, ULONG flags)
{
    UBYTE *p = v9p_tbuf;
    UBYTE *size_field;
    ULONG len;
    LONG rc;
    const UBYTE *rp;
    UBYTE rtype;
    UWORD rtag;
    QID9P qid;
    ULONG iounit;

    size_field = p;
    p += 4;
    p = v9p_put8(p, P9_TLOPEN);
    p = v9p_put16(p, V9P_TAG);
    p = v9p_put32(p, fid);
    p = v9p_put32(p, flags);

    len = (ULONG)(p - v9p_tbuf);
    v9p_put32(size_field, len);

    rc = v9p_transact(len);
    if (rc < 0)
        return rc;
    if ((ULONG)rc < 7)
        return EINTRN;

    rp = v9p_rbuf + 4;
    rp = v9p_get8(rp, &rtype);
    rp = v9p_get16(rp, &rtag);

    if (rtag != V9P_TAG)
    {
        KDEBUG(("virtio_9p: Rlopen tag 0x%x != 0x%x\n", rtag, V9P_TAG));
        return EINTRN;
    }
    if (rtype == P9_RLERROR)
    {
        ULONG ecode;
        v9p_get32(rp, &ecode);
        return v9p_errno_to_gemerror(ecode);
    }
    if (rtype != P9_RLOPEN)
    {
        KDEBUG(("virtio_9p: unexpected reply type %u to Tlopen\n", rtype));
        return EINTRN;
    }
    if ((ULONG)rc < 7 + 13 + 4)
    {
        KDEBUG(("virtio_9p: Rlopen reply too short\n"));
        return EINTRN;
    }

    rp = v9p_getqid(rp, &qid);
    rp = v9p_get32(rp, &iounit);
    (void)iounit;   /* not used - v9p_read()/future v9p_write() chunk
                     * against the negotiated msize instead, which is
                     * always a safe bound regardless of what the server
                     * reports here (0 legitimately means "no opinion"). */

    v9p_fid_pool[fid].qid = qid;

    return E_OK;
}

/* ------------------------------------------------------------------ */
/* Tread                                                                */
/* ------------------------------------------------------------------ */

LONG v9p_read(ULONG fid, UQUAD offset, ULONG count, UBYTE *buf)
{
    UBYTE *p = v9p_tbuf;
    UBYTE *size_field;
    ULONG len, maxdata, rcount;
    LONG rc;
    const UBYTE *rp;
    UBYTE rtype;
    UWORD rtag;

    /* Rread's own envelope (size[4] type[1] tag[2] count[4], 11 bytes)
     * has to fit alongside the requested data within v9p_msize. */
    maxdata = (v9p_msize > 11) ? (v9p_msize - 11) : 0;
    if (count > maxdata)
        count = maxdata;
    if (count == 0)
        return 0;

    size_field = p;
    p += 4;
    p = v9p_put8(p, P9_TREAD);
    p = v9p_put16(p, V9P_TAG);
    p = v9p_put32(p, fid);
    p = v9p_put64(p, offset);
    p = v9p_put32(p, count);

    len = (ULONG)(p - v9p_tbuf);
    v9p_put32(size_field, len);

    rc = v9p_transact(len);
    if (rc < 0)
        return rc;
    if ((ULONG)rc < 7)
        return EINTRN;

    rp = v9p_rbuf + 4;
    rp = v9p_get8(rp, &rtype);
    rp = v9p_get16(rp, &rtag);

    if (rtag != V9P_TAG)
    {
        KDEBUG(("virtio_9p: Rread tag 0x%x != 0x%x\n", rtag, V9P_TAG));
        return EINTRN;
    }
    if (rtype == P9_RLERROR)
    {
        ULONG ecode;
        v9p_get32(rp, &ecode);
        return v9p_errno_to_gemerror(ecode);
    }
    if (rtype != P9_RREAD)
    {
        KDEBUG(("virtio_9p: unexpected reply type %u to Tread\n", rtype));
        return EINTRN;
    }

    rp = v9p_get32(rp, &rcount);
    if ((ULONG)rc < 11 + rcount)
    {
        KDEBUG(("virtio_9p: Rread reply shorter than its own count field\n"));
        return EINTRN;
    }

    memcpy(buf, rp, rcount);

    return (LONG)rcount;
}

/* ------------------------------------------------------------------ */
/* Tgetattr                                                             */
/* ------------------------------------------------------------------ */

LONG v9p_getattr(ULONG fid, P9GETATTR *out)
{
    UBYTE *p = v9p_tbuf;
    UBYTE *size_field;
    ULONG len;
    LONG rc;
    const UBYTE *rp;
    UBYTE rtype;
    UWORD rtag;

    size_field = p;
    p += 4;
    p = v9p_put8(p, P9_TGETATTR);
    p = v9p_put16(p, V9P_TAG);
    p = v9p_put32(p, fid);
    p = v9p_put64(p, P9_GETATTR_BASIC);

    len = (ULONG)(p - v9p_tbuf);
    v9p_put32(size_field, len);

    rc = v9p_transact(len);
    if (rc < 0)
        return rc;
    if ((ULONG)rc < 7)
        return EINTRN;

    rp = v9p_rbuf + 4;
    rp = v9p_get8(rp, &rtype);
    rp = v9p_get16(rp, &rtag);

    if (rtag != V9P_TAG)
    {
        KDEBUG(("virtio_9p: Rgetattr tag 0x%x != 0x%x\n", rtag, V9P_TAG));
        return EINTRN;
    }
    if (rtype == P9_RLERROR)
    {
        ULONG ecode;
        v9p_get32(rp, &ecode);
        return v9p_errno_to_gemerror(ecode);
    }
    if (rtype != P9_RGETATTR)
    {
        KDEBUG(("virtio_9p: unexpected reply type %u to Tgetattr\n", rtype));
        return EINTRN;
    }

    /* valid[8] qid[13] mode[4] uid[4] gid[4] nlink[8] rdev[8] size[8]
     * blksize[8] blocks[8] atime_sec[8] atime_nsec[8] mtime_sec[8] ... -
     * a fixed layout regardless of what request_mask actually asked for
     * (only 'valid' - trusted but not separately checked here - says
     * which fields the server considers meaningful). Only mode/size/
     * mtime_sec are kept: the rest has no GEMDOS use this driver needs
     * yet (see P9GETATTR's own comment in virtio_9p.h). */
    if ((ULONG)rc < 7 + 8 + 13 + 4 + 4 + 4 + 8 + 8 + 8 + 8 + 8 + 8 + 8 + 8)
    {
        KDEBUG(("virtio_9p: Rgetattr reply too short\n"));
        return EINTRN;
    }

    rp += 8;                              /* valid */
    rp = v9p_getqid(rp, &out->qid);
    rp = v9p_get32(rp, &out->mode);
    rp += 4 + 4;                          /* uid, gid */
    rp += 8 + 8;                          /* nlink, rdev */
    rp = v9p_get64(rp, &out->size);
    rp += 8 + 8;                          /* blksize, blocks */
    rp += 8 + 8;                          /* atime_sec, atime_nsec */
    rp = v9p_get64(rp, &out->mtime_sec);

    return E_OK;
}

/* ------------------------------------------------------------------ */
/* Treaddir                                                             */
/* ------------------------------------------------------------------ */

LONG v9p_readdir_one(ULONG dir_fid, UQUAD *offset, char *name, int namelen,
                      QID9P *outqid, BOOL *is_dir)
{
    UBYTE *p = v9p_tbuf;
    UBYTE *size_field;
    ULONG len, reqcount;
    LONG rc;
    const UBYTE *rp;
    UBYTE rtype;
    UWORD rtag;
    ULONG datacount;
    QID9P qid;
    UQUAD entry_offset;
    UBYTE etype;
    UWORD wirenamelen;

    /* Ask for as much as fits, even though only the first packed dirent
     * in the reply is ever decoded (see v9p_readdir_one()'s own comment
     * in virtio_9p.h) - simplicity over efficiency for this stage; a
     * caching layer that amortises this over multiple entries per
     * request is a later stage's job, not this primitive's. */
    reqcount = (v9p_msize > 11) ? (v9p_msize - 11) : 11;

    size_field = p;
    p += 4;
    p = v9p_put8(p, P9_TREADDIR);
    p = v9p_put16(p, V9P_TAG);
    p = v9p_put32(p, dir_fid);
    p = v9p_put64(p, *offset);
    p = v9p_put32(p, reqcount);

    len = (ULONG)(p - v9p_tbuf);
    v9p_put32(size_field, len);

    rc = v9p_transact(len);
    if (rc < 0)
        return rc;
    if ((ULONG)rc < 7)
        return EINTRN;

    rp = v9p_rbuf + 4;
    rp = v9p_get8(rp, &rtype);
    rp = v9p_get16(rp, &rtag);

    if (rtag != V9P_TAG)
    {
        KDEBUG(("virtio_9p: Rreaddir tag 0x%x != 0x%x\n", rtag, V9P_TAG));
        return EINTRN;
    }
    if (rtype == P9_RLERROR)
    {
        ULONG ecode;
        v9p_get32(rp, &ecode);
        return v9p_errno_to_gemerror(ecode);
    }
    if (rtype != P9_RREADDIR)
    {
        KDEBUG(("virtio_9p: unexpected reply type %u to Treaddir\n", rtype));
        return EINTRN;
    }

    rp = v9p_get32(rp, &datacount);
    /* datacount is the device's own claim about how much dirent data
     * follows - never trust it (or anything derived from it, like
     * wirenamelen below) without first confirming the reply actually
     * contains that many bytes, the same way v9p_read() already checks
     * its own count field before touching it. */
    if ((ULONG)rc < 11 + datacount)
    {
        KDEBUG(("virtio_9p: Rreaddir reply shorter than its own count field\n"));
        return EINTRN;
    }
    if (datacount == 0)
        return ENMFIL;      /* end of directory */

    /* one packed dirent: qid[13] offset[8] type[1] name[s] */
    if (datacount < 13 + 8 + 1 + 2)
    {
        KDEBUG(("virtio_9p: Rreaddir entry too short\n"));
        return EINTRN;
    }

    rp = v9p_getqid(rp, &qid);
    rp = v9p_get64(rp, &entry_offset);
    rp = v9p_get8(rp, &etype);
    rp = v9p_get16(rp, &wirenamelen);
    (void)etype;    /* DT_*-style hint; qid.type's QTDIR bit is this
                     * driver's authoritative "is this a directory" -
                     * see is_dir below and virtio_9p.h's comment. */

    /* Clamp against what datacount itself accounts for (now verified
     * against the real reply length above), not just the caller's own
     * 'namelen' buffer below - a wirenamelen inconsistent with datacount
     * must never let the memcpy read past the confirmed-present region. */
    {
        ULONG max_from_datacount = datacount - (13 + 8 + 1 + 2);

        if ((ULONG)wirenamelen > max_from_datacount)
            wirenamelen = (UWORD)max_from_datacount;
    }
    if (wirenamelen >= (UWORD)namelen)
        wirenamelen = (UWORD)(namelen - 1);   /* truncate, never overflow */
    memcpy(name, rp, wirenamelen);
    name[wirenamelen] = 0;

    *offset = entry_offset;
    if (outqid)
        *outqid = qid;
    if (is_dir)
        *is_dir = (qid.type & V9P_QTDIR) ? TRUE : FALSE;

    return E_OK;
}

/* ------------------------------------------------------------------ */
/* Tlcreate                                                             */
/* ------------------------------------------------------------------ */

/* Unlike Twalk, Tlcreate does not allocate a new fid: 'fid' must already
 * name the containing directory, and on success it stops being a
 * directory fid and becomes the newly created (and already open) file's
 * fid instead - so the caller must pass a throwaway dup of the directory
 * fid it still wants to use afterwards, never the directory cookie's own
 * fid. */
LONG v9p_lcreate(ULONG fid, const char *name, ULONG flags, ULONG mode, QID9P *outqid)
{
    UBYTE *p = v9p_tbuf;
    UBYTE *size_field;
    ULONG len;
    LONG rc;
    const UBYTE *rp;
    UBYTE rtype;
    UWORD rtag;
    QID9P qid;
    ULONG iounit;

    size_field = p;
    p += 4;
    p = v9p_put8(p, P9_TLCREATE);
    p = v9p_put16(p, V9P_TAG);
    p = v9p_put32(p, fid);
    p = v9p_putstr(p, name);
    p = v9p_put32(p, flags);
    p = v9p_put32(p, mode);
    p = v9p_put32(p, 0);       /* gid: pTOS has no multi-user concept, same as Tattach's n_uname */

    len = (ULONG)(p - v9p_tbuf);
    v9p_put32(size_field, len);

    rc = v9p_transact(len);
    if (rc < 0)
        return rc;
    if ((ULONG)rc < 7)
        return EINTRN;

    rp = v9p_rbuf + 4;
    rp = v9p_get8(rp, &rtype);
    rp = v9p_get16(rp, &rtag);

    if (rtag != V9P_TAG)
    {
        KDEBUG(("virtio_9p: Rlcreate tag 0x%x != 0x%x\n", rtag, V9P_TAG));
        return EINTRN;
    }
    if (rtype == P9_RLERROR)
    {
        ULONG ecode;
        v9p_get32(rp, &ecode);
        return v9p_errno_to_gemerror(ecode);
    }
    if (rtype != P9_RLCREATE)
    {
        KDEBUG(("virtio_9p: unexpected reply type %u to Tlcreate\n", rtype));
        return EINTRN;
    }
    if ((ULONG)rc < 7 + 13 + 4)
    {
        KDEBUG(("virtio_9p: Rlcreate reply too short\n"));
        return EINTRN;
    }

    rp = v9p_getqid(rp, &qid);
    rp = v9p_get32(rp, &iounit);
    (void)iounit;   /* see v9p_lopen()'s own comment - chunk against msize instead */

    v9p_fid_pool[fid].qid = qid;
    if (outqid)
        *outqid = qid;

    return E_OK;
}

/* ------------------------------------------------------------------ */
/* Twrite                                                               */
/* ------------------------------------------------------------------ */

LONG v9p_write(ULONG fid, UQUAD offset, ULONG count, const UBYTE *buf)
{
    UBYTE *p = v9p_tbuf;
    UBYTE *size_field;
    ULONG len, maxdata, rcount;
    LONG rc;
    const UBYTE *rp;
    UBYTE rtype;
    UWORD rtag;

    /* Twrite's own envelope (size[4] type[1] tag[2] fid[4] offset[8]
     * count[4], 23 bytes) has to fit alongside the data being written
     * within v9p_msize. */
    maxdata = (v9p_msize > 23) ? (v9p_msize - 23) : 0;
    if (count > maxdata)
        count = maxdata;
    if (count == 0)
        return 0;

    size_field = p;
    p += 4;
    p = v9p_put8(p, P9_TWRITE);
    p = v9p_put16(p, V9P_TAG);
    p = v9p_put32(p, fid);
    p = v9p_put64(p, offset);
    p = v9p_put32(p, count);
    memcpy(p, buf, count);
    p += count;

    len = (ULONG)(p - v9p_tbuf);
    v9p_put32(size_field, len);

    rc = v9p_transact(len);
    if (rc < 0)
        return rc;
    if ((ULONG)rc < 7)
        return EINTRN;

    rp = v9p_rbuf + 4;
    rp = v9p_get8(rp, &rtype);
    rp = v9p_get16(rp, &rtag);

    if (rtag != V9P_TAG)
    {
        KDEBUG(("virtio_9p: Rwrite tag 0x%x != 0x%x\n", rtag, V9P_TAG));
        return EINTRN;
    }
    if (rtype == P9_RLERROR)
    {
        ULONG ecode;
        v9p_get32(rp, &ecode);
        return v9p_errno_to_gemerror(ecode);
    }
    if (rtype != P9_RWRITE)
    {
        KDEBUG(("virtio_9p: unexpected reply type %u to Twrite\n", rtype));
        return EINTRN;
    }
    if ((ULONG)rc < 11)
    {
        KDEBUG(("virtio_9p: Rwrite reply too short\n"));
        return EINTRN;
    }

    v9p_get32(rp, &rcount);

    return (LONG)rcount;
}

/* ------------------------------------------------------------------ */
/* Tmkdir                                                               */
/* ------------------------------------------------------------------ */

LONG v9p_mkdir(ULONG dfid, const char *name, ULONG mode, QID9P *outqid)
{
    UBYTE *p = v9p_tbuf;
    UBYTE *size_field;
    ULONG len;
    LONG rc;
    const UBYTE *rp;
    UBYTE rtype;
    UWORD rtag;
    QID9P qid;

    size_field = p;
    p += 4;
    p = v9p_put8(p, P9_TMKDIR);
    p = v9p_put16(p, V9P_TAG);
    p = v9p_put32(p, dfid);
    p = v9p_putstr(p, name);
    p = v9p_put32(p, mode);
    p = v9p_put32(p, 0);       /* gid */

    len = (ULONG)(p - v9p_tbuf);
    v9p_put32(size_field, len);

    rc = v9p_transact(len);
    if (rc < 0)
        return rc;
    if ((ULONG)rc < 7)
        return EINTRN;

    rp = v9p_rbuf + 4;
    rp = v9p_get8(rp, &rtype);
    rp = v9p_get16(rp, &rtag);

    if (rtag != V9P_TAG)
    {
        KDEBUG(("virtio_9p: Rmkdir tag 0x%x != 0x%x\n", rtag, V9P_TAG));
        return EINTRN;
    }
    if (rtype == P9_RLERROR)
    {
        ULONG ecode;
        v9p_get32(rp, &ecode);
        return v9p_errno_to_gemerror(ecode);
    }
    if (rtype != P9_RMKDIR)
    {
        KDEBUG(("virtio_9p: unexpected reply type %u to Tmkdir\n", rtype));
        return EINTRN;
    }
    if ((ULONG)rc < 7 + 13)
    {
        KDEBUG(("virtio_9p: Rmkdir reply too short\n"));
        return EINTRN;
    }

    rp = v9p_getqid(rp, &qid);
    if (outqid)
        *outqid = qid;

    return E_OK;
}

/* ------------------------------------------------------------------ */
/* Tunlinkat                                                            */
/* ------------------------------------------------------------------ */

LONG v9p_unlinkat(ULONG dfid, const char *name, ULONG flags)
{
    UBYTE *p = v9p_tbuf;
    UBYTE *size_field;
    ULONG len;
    LONG rc;
    const UBYTE *rp;
    UBYTE rtype;
    UWORD rtag;

    size_field = p;
    p += 4;
    p = v9p_put8(p, P9_TUNLINKAT);
    p = v9p_put16(p, V9P_TAG);
    p = v9p_put32(p, dfid);
    p = v9p_putstr(p, name);
    p = v9p_put32(p, flags);

    len = (ULONG)(p - v9p_tbuf);
    v9p_put32(size_field, len);

    rc = v9p_transact(len);
    if (rc < 0)
        return rc;
    if ((ULONG)rc < 7)
        return EINTRN;

    rp = v9p_rbuf + 4;
    rp = v9p_get8(rp, &rtype);
    rp = v9p_get16(rp, &rtag);

    if (rtag != V9P_TAG)
    {
        KDEBUG(("virtio_9p: Runlinkat tag 0x%x != 0x%x\n", rtag, V9P_TAG));
        return EINTRN;
    }
    if (rtype == P9_RLERROR)
    {
        ULONG ecode;
        v9p_get32(rp, &ecode);
        return v9p_errno_to_gemerror(ecode);
    }
    if (rtype != P9_RUNLINKAT)
    {
        KDEBUG(("virtio_9p: unexpected reply type %u to Tunlinkat\n", rtype));
        return EINTRN;
    }

    return E_OK;
}

/* ------------------------------------------------------------------ */
/* Trenameat                                                            */
/* ------------------------------------------------------------------ */

LONG v9p_renameat(ULONG olddfid, const char *oldname, ULONG newdfid, const char *newname)
{
    UBYTE *p = v9p_tbuf;
    UBYTE *size_field;
    ULONG len;
    LONG rc;
    const UBYTE *rp;
    UBYTE rtype;
    UWORD rtag;

    size_field = p;
    p += 4;
    p = v9p_put8(p, P9_TRENAMEAT);
    p = v9p_put16(p, V9P_TAG);
    p = v9p_put32(p, olddfid);
    p = v9p_putstr(p, oldname);
    p = v9p_put32(p, newdfid);
    p = v9p_putstr(p, newname);

    len = (ULONG)(p - v9p_tbuf);
    v9p_put32(size_field, len);

    rc = v9p_transact(len);
    if (rc < 0)
        return rc;
    if ((ULONG)rc < 7)
        return EINTRN;

    rp = v9p_rbuf + 4;
    rp = v9p_get8(rp, &rtype);
    rp = v9p_get16(rp, &rtag);

    if (rtag != V9P_TAG)
    {
        KDEBUG(("virtio_9p: Rrenameat tag 0x%x != 0x%x\n", rtag, V9P_TAG));
        return EINTRN;
    }
    if (rtype == P9_RLERROR)
    {
        ULONG ecode;
        v9p_get32(rp, &ecode);
        return v9p_errno_to_gemerror(ecode);
    }
    if (rtype != P9_RRENAMEAT)
    {
        KDEBUG(("virtio_9p: unexpected reply type %u to Trenameat\n", rtype));
        return EINTRN;
    }

    return E_OK;
}

/* ------------------------------------------------------------------ */
/* Tstatfs                                                              */
/* ------------------------------------------------------------------ */

LONG v9p_statfs(ULONG fid, P9STATFS *out)
{
    UBYTE *p = v9p_tbuf;
    UBYTE *size_field;
    ULONG len;
    LONG rc;
    const UBYTE *rp;
    UBYTE rtype;
    UWORD rtag;
    ULONG type;

    size_field = p;
    p += 4;
    p = v9p_put8(p, P9_TSTATFS);
    p = v9p_put16(p, V9P_TAG);
    p = v9p_put32(p, fid);

    len = (ULONG)(p - v9p_tbuf);
    v9p_put32(size_field, len);

    rc = v9p_transact(len);
    if (rc < 0)
        return rc;
    if ((ULONG)rc < 7)
        return EINTRN;

    rp = v9p_rbuf + 4;
    rp = v9p_get8(rp, &rtype);
    rp = v9p_get16(rp, &rtag);

    if (rtag != V9P_TAG)
    {
        KDEBUG(("virtio_9p: Rstatfs tag 0x%x != 0x%x\n", rtag, V9P_TAG));
        return EINTRN;
    }
    if (rtype == P9_RLERROR)
    {
        ULONG ecode;
        v9p_get32(rp, &ecode);
        return v9p_errno_to_gemerror(ecode);
    }
    if (rtype != P9_RSTATFS)
    {
        KDEBUG(("virtio_9p: unexpected reply type %u to Tstatfs\n", rtype));
        return EINTRN;
    }

    /* type[4] bsize[4] blocks[8] bfree[8] bavail[8] files[8] ffree[8]
     * fsid[8] namelen[4] - only bsize/blocks/bfree have a GEMDOS Dfree()
     * use (see v9p_pfs_dfree() in fs/virtio_9p_pfs.c). */
    if ((ULONG)rc < 7 + 4 + 4 + 8 + 8)
    {
        KDEBUG(("virtio_9p: Rstatfs reply too short\n"));
        return EINTRN;
    }

    rp = v9p_get32(rp, &type);
    (void)type;     /* the filesystem's magic number (statfs(2) f_type) - no GEMDOS use */
    rp = v9p_get32(rp, &out->bsize);
    rp = v9p_get64(rp, &out->blocks);
    rp = v9p_get64(rp, &out->bfree);

    return E_OK;
}

/* ------------------------------------------------------------------ */
/* bring-up                                                             */
/* ------------------------------------------------------------------ */

static void v9p_isr(void)
{
    virtio_handle_interrupt(&v9p_dev);
}

static void v9p_connect_irq(WORD slot)
{
#if defined(MACHINE_VIRT_ARM)
    virt_connect_irq(VIRT_VIRTIO_IRQ_BASE + slot, v9p_isr);
#elif defined(MACHINE_VIRT_M68K)
    goldfish_pic_connect_irq((WORD)(1 + slot / 32), (WORD)(slot % 32), v9p_isr);
#endif
}

void virtio_9p_init(void)
{
    WORD slot;
    ULONG base;

    v9p_failed = FALSE;
    v9p_attached = FALSE;

    for (slot = 0; slot < VIRTIO_MMIO_COUNT; slot++)
    {
        base = VIRTIO_MMIO_BASE + (ULONG)slot * VIRTIO_MMIO_STRIDE;

        if (!virtio_probe(base, VIRTIO_ID_9P, &v9p_dev))
            continue;

        /* Tell the (architecture-neutral) transport how this board's RAM
         * addresses look from the device's side - same as virtio_blk_init()
         * and virtio_input_init() do for their own device instances. */
#if defined(MACHINE_VIRT_ARM)
        v9p_dev.phys_offset = VIRT_RAM_BASE;
#elif defined(MACHINE_VIRT_M68K)
        v9p_dev.phys_offset = 0;
#endif

        if (!virtio_setup_queue(&v9p_dev))
        {
            KDEBUG(("virtio_9p_init: slot %d found but queue setup failed\n", slot));
            continue;
        }

        v9p_read_mount_tag(base);
        v9p_connect_irq(slot);

        KDEBUG(("virtio_9p_init: device at slot %d (base 0x%08lx, mount_tag \"%s\")\n",
                slot, base, v9p_mount_tag));

        if (v9p_version() < 0)
        {
            KDEBUG(("virtio_9p_init: Tversion handshake failed, disabling\n"));
            v9p_failed = TRUE;
            return;
        }

        if (v9p_attach() < 0)
        {
            KDEBUG(("virtio_9p_init: Tattach failed, disabling\n"));
            v9p_failed = TRUE;
            return;
        }

        v9p_attached = TRUE;
        KDEBUG(("virtio_9p_init: ready (msize=%lu, root fid=%lu)\n",
                v9p_msize, v9p_attach_fid));
        return;   /* v1 binds only the first device found - see #157's plan */
    }

    KDEBUG(("virtio_9p_init: no device found\n"));
}

#endif /* CONF_WITH_VIRTIO_9P */
