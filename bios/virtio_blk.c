/*
 * virtio_blk.c - virtio-blk driver for the QEMU virt-arm/virt-m68k boards
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*#define ENABLE_KDEBUG*/

#include "config.h"

#if CONF_WITH_VIRTIO_BLK

#include "portab.h"
#include "disk.h"
#include "gemerror.h"
#include "kprint.h"
#include "virtio.h"
#include "virtio_blk.h"

#if defined(MACHINE_VIRT_ARM)
#include "virt_memmap.h"
#define VIRTIO_MMIO_BASE    VIRT_VIRTIO_MMIO_BASE
#define VIRTIO_MMIO_STRIDE  VIRT_VIRTIO_MMIO_STRIDE
#define VIRTIO_MMIO_COUNT   VIRT_VIRTIO_MMIO_COUNT
#elif defined(MACHINE_VIRT_M68K)
#define VIRTIO_MMIO_BASE    0xff010000UL
#define VIRTIO_MMIO_STRIDE  0x200UL
#define VIRTIO_MMIO_COUNT   128
#endif

#define VIRTIO_BLK_DEVICE_ID  2

static VIRTIO_DEV virtio_blk_dev[DEVICES_PER_BUS];
static WORD virtio_blk_count;

void virtio_blk_init(void)
{
    WORD slot;
    ULONG base;

    virtio_blk_count = 0;

    for (slot = 0; slot < VIRTIO_MMIO_COUNT && virtio_blk_count < DEVICES_PER_BUS; slot++)
    {
        base = VIRTIO_MMIO_BASE + (ULONG)slot * VIRTIO_MMIO_STRIDE;

        if (!virtio_probe(base, VIRTIO_BLK_DEVICE_ID, &virtio_blk_dev[virtio_blk_count]))
            continue;

        KDEBUG(("virtio_blk_init: found device at slot %d (base 0x%08lx)\n", slot, base));
        virtio_blk_count++;
    }

    KDEBUG(("virtio_blk_init: %d device(s) found\n", virtio_blk_count));
}

LONG virtio_blk_ioctl(UWORD drv, UWORD ctrl, void *arg)
{
    return EUNDEV;   /* implemented in Task 2 */
}

LONG virtio_blk_rw(WORD rw, LONG sector, WORD count, UBYTE *buf, WORD dev)
{
    return EUNDEV;   /* implemented in Task 2 */
}

#endif /* CONF_WITH_VIRTIO_BLK */
