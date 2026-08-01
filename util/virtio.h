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
    ULONG base;             /* mmio base address of this device's transport window */
    UWORD last_used_idx;    /* used->idx last consumed by virtio_handle_interrupt() */
    volatile BOOL done;     /* set by virtio_handle_interrupt(), cleared by virtio_submit() */
    VIRTIO_DESC  desc[VIRTIO_QUEUE_SIZE];
    VIRTIO_AVAIL avail;
    VIRTIO_USED  used;
} VIRTIO_DEV;

/* Probes one virtio-mmio slot: checks magic/version/device-id, negotiates
 * VIRTIO_F_VERSION_1 only. On success dev->base/last_used_idx/done are
 * initialized and the device is left in the FEATURES_OK state (no queue
 * configured yet, DRIVER_OK not set). Returns FALSE if the slot is empty,
 * is a different device type, or isn't version-2 virtio-mmio. */
BOOL virtio_probe(ULONG base, UWORD want_device_id, VIRTIO_DEV *dev);

/* Configures queue 0 from dev->desc/avail/used and sets DRIVER_OK.
 * Must be called exactly once after a successful virtio_probe(). */
BOOL virtio_setup_queue(VIRTIO_DEV *dev);

/* Writes one descriptor slot (LE-encoded). addr must already be the
 * physical address the device should DMA to/from -- on boards where the
 * kernel's own addresses are not physical addresses (see virt_to_phys()
 * in bios/machine/virt-arm/virt_mmu.h), the caller is responsible for the
 * translation before calling this. addr_hi is always 0 (neither virt
 * board needs a 64-bit DMA address). */
void virtio_desc_set(VIRTIO_DEV *dev, UWORD index, ULONG addr, ULONG len, UWORD flags, UWORD next);

/* Appends head_index to the avail ring and bumps avail->idx. Clears dev->done. */
void virtio_submit(VIRTIO_DEV *dev, UWORD head_index);

/* Rings the device's doorbell (QueueNotify). */
void virtio_notify(VIRTIO_DEV *dev);

/* Called from the board's IRQ dispatch once it has identified this device
 * as the interrupt source. Acks the device's InterruptStatus and, if the
 * used ring advanced, sets dev->done. */
void virtio_handle_interrupt(VIRTIO_DEV *dev);

#endif /* VIRTIO_H */
