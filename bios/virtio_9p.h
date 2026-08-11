/*
 * virtio_9p.h - virtio-9p (9P2000.L) transport/protocol client for the
 * QEMU virt-arm/virt-m68k boards
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef VIRTIO_9P_H
#define VIRTIO_9P_H

#include "portab.h"

#if CONF_WITH_VIRTIO_9P

/* Probes for a virtio-9p device on the virtio-mmio transport, negotiates
 * the 9P2000.L dialect via Tversion, and leaves the driver ready for a
 * pfs_ops adapter to attach against (see fs/virtio_9p_pfs.c, added in a
 * later stage - this header currently only exposes bring-up, no fid-level
 * API yet). No effect if no matching device is found. Called once from
 * bios/bios.c, after blkdev_init().
 */
void virtio_9p_init(void);

#endif /* CONF_WITH_VIRTIO_9P */

#endif /* VIRTIO_9P_H */
