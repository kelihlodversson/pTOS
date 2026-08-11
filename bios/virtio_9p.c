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
#define P9_RLERROR   7

#define V9P_MAX_MOUNT_TAG  32   /* QEMU's own MAX_TAG_LEN */
#define V9P_NOFID    0xFFFFFFFFUL
#define V9P_MAXWELEM 16         /* 9P2000.L's own per-Twalk component cap */

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
