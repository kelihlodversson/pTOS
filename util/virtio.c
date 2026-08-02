/*
 * virtio.c - shared virtio-mmio transport (modern/version-2 only)
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#include "config.h"
#include "portab.h"
#include "endian.h"
#include "virtio.h"

#define VIRTIO_MAGIC  0x74726976UL   /* "virt" */

#define VIRTIO_STATUS_ACKNOWLEDGE  0x01
#define VIRTIO_STATUS_DRIVER       0x02
#define VIRTIO_STATUS_DRIVER_OK    0x04
#define VIRTIO_STATUS_FEATURES_OK  0x08
#define VIRTIO_STATUS_FAILED       0x80

typedef struct
{
    volatile ULONG magic_value;         /* 0x000 */
    volatile ULONG version;             /* 0x004 */
    volatile ULONG device_id;           /* 0x008 */
    volatile ULONG vendor_id;           /* 0x00c */
    volatile ULONG device_features;     /* 0x010 */
    volatile ULONG device_features_sel; /* 0x014 */
    ULONG pad1[2];                      /* 0x018 */
    volatile ULONG driver_features;     /* 0x020 */
    volatile ULONG driver_features_sel; /* 0x024 */
    ULONG pad2[2];                      /* 0x028 */
    volatile ULONG queue_sel;           /* 0x030 */
    volatile ULONG queue_num_max;       /* 0x034 */
    volatile ULONG queue_num;           /* 0x038 */
    ULONG pad3[2];                      /* 0x03c */
    volatile ULONG queue_ready;         /* 0x044 */
    ULONG pad4[2];                      /* 0x048 */
    volatile ULONG queue_notify;        /* 0x050 */
    ULONG pad5[3];                      /* 0x054 */
    volatile ULONG interrupt_status;    /* 0x060 */
    volatile ULONG interrupt_ack;       /* 0x064 */
    ULONG pad6[2];                      /* 0x068 */
    volatile ULONG status;              /* 0x070 */
    ULONG pad7[3];                      /* 0x074 */
    volatile ULONG queue_desc_low;      /* 0x080 */
    volatile ULONG queue_desc_high;     /* 0x084 */
    ULONG pad8[2];                      /* 0x088 */
    volatile ULONG queue_driver_low;    /* 0x090 */
    volatile ULONG queue_driver_high;   /* 0x094 */
    ULONG pad9[2];                      /* 0x098 */
    volatile ULONG queue_device_low;    /* 0x0a0 */
    volatile ULONG queue_device_high;   /* 0x0a4 */
    ULONG pad10[21];                    /* 0x0a8 */
    volatile ULONG config_generation;   /* 0x0fc */
} VIRTIO_MMIO_REGS;

static ULONG vreg_read(volatile ULONG *reg)
{
    return le2cpu32(*reg);
}

static void vreg_write(volatile ULONG *reg, ULONG val)
{
    *reg = cpu2le32(val);
}

BOOL virtio_probe(ULONG base, UWORD want_device_id, VIRTIO_DEV *dev)
{
    volatile VIRTIO_MMIO_REGS *regs = (volatile VIRTIO_MMIO_REGS *)base;
    ULONG feat_hi;

    if (vreg_read(&regs->magic_value) != VIRTIO_MAGIC)
        return FALSE;
    if (vreg_read(&regs->version) != 2)
        return FALSE;
    if (vreg_read(&regs->device_id) != (ULONG)want_device_id)
        return FALSE;

    vreg_write(&regs->status, 0);
    vreg_write(&regs->status, VIRTIO_STATUS_ACKNOWLEDGE);
    vreg_write(&regs->status, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    vreg_write(&regs->device_features_sel, 1);
    feat_hi = vreg_read(&regs->device_features);
    if (!(feat_hi & 1))   /* VIRTIO_F_VERSION_1 is bit 32 (bit 0 of the sel=1 word) */
    {
        vreg_write(&regs->status, VIRTIO_STATUS_FAILED);
        return FALSE;
    }

    vreg_write(&regs->driver_features_sel, 0);
    vreg_write(&regs->driver_features, 0);
    vreg_write(&regs->driver_features_sel, 1);
    vreg_write(&regs->driver_features, 1);

    vreg_write(&regs->status, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
    if (!(vreg_read(&regs->status) & VIRTIO_STATUS_FEATURES_OK))
    {
        vreg_write(&regs->status, VIRTIO_STATUS_FAILED);
        return FALSE;
    }

    dev->base = base;
    dev->phys_offset = 0;
    dev->last_used_idx = 0;
    dev->pop_idx = 0;
    dev->done = FALSE;
    return TRUE;
}

BOOL virtio_setup_queue(VIRTIO_DEV *dev)
{
    volatile VIRTIO_MMIO_REGS *regs = (volatile VIRTIO_MMIO_REGS *)dev->base;
    ULONG qmax;
    WORD i;

    vreg_write(&regs->queue_sel, 0);
    if (vreg_read(&regs->queue_ready) != 0)
        return FALSE;

    qmax = vreg_read(&regs->queue_num_max);
    if (qmax < VIRTIO_QUEUE_SIZE)
        return FALSE;

    for (i = 0; i < VIRTIO_QUEUE_SIZE; i++)
        virtio_desc_set(dev, i, 0, 0, 0, 0);
    dev->avail.flags = 0;
    dev->avail.idx = 0;
    dev->used.flags = 0;
    dev->used.idx = 0;

    /* The device is not behind this port's MMU (where one exists), so the
     * three queue base registers take physical addresses; dev->phys_offset
     * turns our own linked addresses into those (0 where they coincide). */
    vreg_write(&regs->queue_num, VIRTIO_QUEUE_SIZE);
    vreg_write(&regs->queue_desc_low,   (ULONG)dev->desc + dev->phys_offset);
    vreg_write(&regs->queue_desc_high,  0);
    vreg_write(&regs->queue_driver_low, (ULONG)&dev->avail + dev->phys_offset);
    vreg_write(&regs->queue_driver_high,0);
    vreg_write(&regs->queue_device_low, (ULONG)&dev->used + dev->phys_offset);
    vreg_write(&regs->queue_device_high,0);
    vreg_write(&regs->queue_ready, 1);

    vreg_write(&regs->status, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER
                             | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);
    return TRUE;
}

void virtio_desc_set(VIRTIO_DEV *dev, UWORD index, ULONG addr, ULONG len, UWORD flags, UWORD next)
{
    dev->desc[index].addr_lo = cpu2le32(addr);
    dev->desc[index].addr_hi = cpu2le32(0);
    dev->desc[index].len     = cpu2le32(len);
    dev->desc[index].flags   = cpu2le16(flags);
    dev->desc[index].next    = cpu2le16(next);
}

void virtio_submit(VIRTIO_DEV *dev, UWORD head_index)
{
    UWORD slot = le2cpu16(dev->avail.idx) % VIRTIO_QUEUE_SIZE;

    dev->avail.ring[slot] = cpu2le16(head_index);
    dev->avail.idx = cpu2le16((UWORD)(le2cpu16(dev->avail.idx) + 1));
    dev->done = FALSE;
}

#if ARCH_ARM
extern void flush_data_cache(void *start, long size);
extern void invalidate_data_cache(void *start, long size);
#endif

void virtio_notify(VIRTIO_DEV *dev)
{
    volatile VIRTIO_MMIO_REGS *regs = (volatile VIRTIO_MMIO_REGS *)dev->base;

#if ARCH_ARM
    flush_data_cache(dev->desc, sizeof(dev->desc));
    flush_data_cache(&dev->avail, sizeof(dev->avail));
#endif
    vreg_write(&regs->queue_notify, 0);
}

void virtio_handle_interrupt(VIRTIO_DEV *dev)
{
    volatile VIRTIO_MMIO_REGS *regs = (volatile VIRTIO_MMIO_REGS *)dev->base;
    ULONG isr = vreg_read(&regs->interrupt_status);

    vreg_write(&regs->interrupt_ack, isr);

#if ARCH_ARM
    invalidate_data_cache(&dev->used, sizeof(dev->used));
#endif

    if (le2cpu16(dev->used.idx) != dev->last_used_idx)
    {
        dev->last_used_idx = le2cpu16(dev->used.idx);
        dev->done = TRUE;
    }
}

BOOL virtio_pop_used(VIRTIO_DEV *dev, UWORD *out_index, ULONG *out_len)
{
    UWORD slot;

    if (dev->pop_idx == le2cpu16(dev->used.idx))
        return FALSE;

    slot = dev->pop_idx % VIRTIO_QUEUE_SIZE;
    *out_index = (UWORD)le2cpu32(dev->used.ring[slot].id);
    *out_len = le2cpu32(dev->used.ring[slot].len);
    dev->pop_idx++;

    return TRUE;
}
