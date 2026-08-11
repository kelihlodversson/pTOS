/*
 * virtio_9p.c - virtio-9p (9P2000.L) transport/protocol client for the
 * QEMU virt-arm/virt-m68k boards
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * Stage 1 of #157: MMIO discovery, the mount-tag config-space read, and
 * the Tversion/Rversion handshake only - no fid-level API yet (Twalk,
 * Tlopen, ...), and no fs/pfs.h registration.  Those land in later stages
 * on top of v9p_transact(), the one generic request/response primitive
 * this stage establishes.
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
#define P9_RLERROR   7

#define V9P_MAX_MOUNT_TAG  32   /* QEMU's own MAX_TAG_LEN */

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
        return EACCDN;
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

        KDEBUG(("virtio_9p_init: ready (msize=%lu)\n", v9p_msize));
        return;   /* v1 binds only the first device found - see #157's plan */
    }

    KDEBUG(("virtio_9p_init: no device found\n"));
}

#endif /* CONF_WITH_VIRTIO_9P */
