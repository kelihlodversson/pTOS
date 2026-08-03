/*
 * virtio_blk.h - virtio-blk driver for the QEMU virt-arm/virt-m68k boards
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H

#include "portab.h"

#if CONF_WITH_VIRTIO_BLK

void virtio_blk_init(void);
LONG virtio_blk_ioctl(UWORD drv, UWORD ctrl, void *arg);
LONG virtio_blk_rw(WORD rw, LONG sector, WORD count, UBYTE *buf, WORD dev);

#endif /* CONF_WITH_VIRTIO_BLK */

#endif /* VIRTIO_BLK_H */
