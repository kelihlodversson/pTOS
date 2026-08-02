/*
 * virtio.h - shared virtio-mmio transport (modern/version-2 only)
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef VIRTIO_H
#define VIRTIO_H

#include "portab.h"

#define VIRTIO_QUEUE_SIZE  8   /* descriptors/ring slots per queue; power of two */

#define VIRTIO_DESC_F_NEXT   1
#define VIRTIO_DESC_F_WRITE  2

/* One virtqueue descriptor. Fields are stored LE-encoded (see virtio_desc_set()
 * in virtio.c) since the device reads this memory directly, regardless of
 * guest endianness. */
typedef struct
{
    ULONG addr_lo;
    ULONG addr_hi;
    ULONG len;
    UWORD flags;
    UWORD next;
} VIRTIO_DESC;

typedef struct
{
    UWORD flags;
    UWORD idx;
    UWORD ring[VIRTIO_QUEUE_SIZE];
    UWORD used_event;
} VIRTIO_AVAIL;

typedef struct
{
    ULONG id;
    ULONG len;
} VIRTIO_USED_ELEM;

typedef struct
{
    UWORD flags;
    UWORD idx;
    VIRTIO_USED_ELEM ring[VIRTIO_QUEUE_SIZE];
    UWORD avail_event;
} VIRTIO_USED;

typedef struct
{
    /* The virtio spec requires the descriptor table to be 16-byte aligned;
     * it's listed first so the struct's own __attribute__((aligned(16)))
     * below (which also pads sizeof(VIRTIO_DEV) up to a multiple of 16,
     * keeping every element of an array of these aligned too) applies to
     * it directly, rather than to whatever offset it would otherwise land
     * at behind the smaller fields. */
    VIRTIO_DESC  desc[VIRTIO_QUEUE_SIZE];
    VIRTIO_AVAIL avail;
    VIRTIO_USED  used;
    ULONG base;             /* mmio base address of this device's transport window */
    ULONG phys_offset;      /* physical_address - virtual(linked)_address, for any
                             * RAM address associated with this device's virtqueue
                             * memory (dev->desc/avail/used, and any caller-owned
                             * buffer descriptors point at).  The device sits
                             * outside this port's MMU (where one exists) and
                             * only understands physical addresses; add this
                             * offset to a linked address to get the physical one.
                             * 0 on boards with no such aliasing.  virtio_probe()
                             * zeroes this; the caller sets it once, right after a
                             * successful virtio_probe(), before calling
                             * virtio_setup_queue(). */
    UWORD last_used_idx;    /* used->idx last consumed by virtio_handle_interrupt() */
    UWORD pop_idx;          /* used->idx last consumed by virtio_pop_used() -- independent
                             * of last_used_idx: that one tracks "did anything complete"
                             * for a single synchronous waiter (virtio_blk's model), this
                             * one tracks "which entries has the caller actually drained"
                             * for a queue that keeps several buffers in flight at once
                             * (virtio-input's eventq). */
    volatile BOOL done;     /* set by virtio_handle_interrupt(), cleared by virtio_submit() */
} __attribute__((aligned(16))) VIRTIO_DEV;

/* Probes one virtio-mmio slot: checks magic/version/device-id, negotiates
 * VIRTIO_F_VERSION_1 only. On success dev->base/last_used_idx/done are
 * initialized, dev->phys_offset is zeroed (the caller must set it before
 * virtio_setup_queue() if this board's RAM is aliased), and the device is
 * left in the FEATURES_OK state (no queue configured yet, DRIVER_OK not
 * set). Returns FALSE if the slot is empty, is a different device type,
 * or isn't version-2 virtio-mmio. */
BOOL virtio_probe(ULONG base, UWORD want_device_id, VIRTIO_DEV *dev);

/* Configures queue 0 from dev->desc/avail/used and sets DRIVER_OK.  The
 * three queue base registers are programmed with dev->phys_offset applied,
 * so the caller must have set that field first (see virtio_probe()).
 * Must be called exactly once after a successful virtio_probe(). */
BOOL virtio_setup_queue(VIRTIO_DEV *dev);

/* Writes one descriptor slot (LE-encoded). addr must already be the
 * physical address the device should DMA to/from -- on boards where the
 * kernel's own addresses are not physical addresses, the caller is
 * responsible for the translation (adding dev->phys_offset) before
 * calling this. addr_hi is always 0 (neither virt board needs a 64-bit
 * DMA address). */
void virtio_desc_set(VIRTIO_DEV *dev, UWORD index, ULONG addr, ULONG len, UWORD flags, UWORD next);

/* Appends head_index to the avail ring and bumps avail->idx. Clears dev->done. */
void virtio_submit(VIRTIO_DEV *dev, UWORD head_index);

/* Rings the device's doorbell (QueueNotify). */
void virtio_notify(VIRTIO_DEV *dev);

/* Called from the board's IRQ dispatch once it has identified this device
 * as the interrupt source. Acks the device's InterruptStatus and, if the
 * used ring advanced, sets dev->done. */
void virtio_handle_interrupt(VIRTIO_DEV *dev);

/* Drains one not-yet-consumed used-ring entry: returns TRUE and fills
 * *out_index (the descriptor index the device completed) and *out_len
 * (bytes the device wrote/read), advancing past it; returns FALSE once
 * the caller has caught up with dev->used.idx. Call this after
 * virtio_handle_interrupt() has run (so dev->used is fresh). Unlike
 * dev->done, which a single synchronous waiter clears by re-submitting,
 * this lets a caller that keeps several buffers in flight (like
 * virtio-input's eventq) drain them all in one interrupt. */
BOOL virtio_pop_used(VIRTIO_DEV *dev, UWORD *out_index, ULONG *out_len);

#endif /* VIRTIO_H */
