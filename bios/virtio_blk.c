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
#include "string.h"
#include "endian.h"
#include "mfp.h"
#include "tosvars.h"
#include "virtio.h"
#include "virtio_blk.h"

#define VIRTIO_BLK_TIMEOUT_MSEC   1000UL
#define VIRTIO_BLK_TIMEOUT_TICKS  ((VIRTIO_BLK_TIMEOUT_MSEC*CLOCKS_PER_SEC+999)/1000)

#if defined(MACHINE_VIRT_ARM)
#include "virt_memmap.h"
#include "virt_pic.h"
#include "virt_mmu.h"
#define VIRTIO_MMIO_BASE    VIRT_VIRTIO_MMIO_BASE
#define VIRTIO_MMIO_STRIDE  VIRT_VIRTIO_MMIO_STRIDE
#define VIRTIO_MMIO_COUNT   VIRT_VIRTIO_MMIO_COUNT
/* Descriptor addresses must be physical, not the kernel's own (virtual,
 * low-window) addresses -- see virt_to_phys()'s comment in virt_mmu.h. */
#define VIRTIO_BLK_PHYS(p)  virt_to_phys((void *)(p))
#elif defined(MACHINE_VIRT_M68K)
#define VIRTIO_MMIO_BASE    0xff010000UL
#define VIRTIO_MMIO_STRIDE  0x200UL
#define VIRTIO_MMIO_COUNT   128
/* m68k has no MMU aliasing: virtual and physical addresses coincide. */
#define VIRTIO_BLK_PHYS(p)  ((ULONG)(p))
#endif

#define VIRTIO_BLK_DEVICE_ID  2

#define VIRTIO_BLK_T_IN   0UL
#define VIRTIO_BLK_T_OUT  1UL

struct virtio_blk_req
{
    ULONG type;
    ULONG reserved;
    QUAD  sector;
};

static VIRTIO_DEV virtio_blk_dev[DEVICES_PER_BUS];
static ULONG virtio_blk_capacity[DEVICES_PER_BUS];   /* in 512-byte sectors */
static struct virtio_blk_req virtio_blk_hdr[DEVICES_PER_BUS];
static UBYTE virtio_blk_status[DEVICES_PER_BUS];
static WORD virtio_blk_count;

static ULONG virtio_blk_read_capacity(ULONG base)
{
    volatile ULONG *config = (volatile ULONG *)(base + 0x100);
    return le2cpu32(config[0]);   /* capacity is a 64-bit LE field; the low word is enough here */
}

static void virtio_blk_isr_common(WORD unit)
{
    virtio_handle_interrupt(&virtio_blk_dev[unit]);
}

#define VIRTIO_BLK_ISR(n) \
static void virtio_blk_isr_##n(void) { virtio_blk_isr_common(n); }

VIRTIO_BLK_ISR(0)
VIRTIO_BLK_ISR(1)
VIRTIO_BLK_ISR(2)
VIRTIO_BLK_ISR(3)
VIRTIO_BLK_ISR(4)
VIRTIO_BLK_ISR(5)
VIRTIO_BLK_ISR(6)
VIRTIO_BLK_ISR(7)

static PFVOID const virtio_blk_isr_table[DEVICES_PER_BUS] =
{
    virtio_blk_isr_0, virtio_blk_isr_1, virtio_blk_isr_2, virtio_blk_isr_3,
    virtio_blk_isr_4, virtio_blk_isr_5, virtio_blk_isr_6, virtio_blk_isr_7
};

static void virtio_blk_connect_irq(WORD slot, WORD unit)
{
#if defined(MACHINE_VIRT_ARM)
    virt_connect_irq(VIRT_VIRTIO_IRQ_BASE + slot, virtio_blk_isr_table[unit]);
#elif defined(MACHINE_VIRT_M68K)
    goldfish_pic_connect_irq((WORD)(1 + slot / 32), (WORD)(slot % 32), virtio_blk_isr_table[unit]);
#endif
}

#ifdef ENABLE_KDEBUG
/* Safe on this driver specifically: both virt boards are QEMU-only, no real
 * hardware exists for either (see the design doc), so there is no risk of
 * clobbering a real disk. Exercises the write path, which disk_init_all()'s
 * boot-time partition scan never does on its own (it only reads). */
static void virtio_blk_selftest(WORD unit)
{
    static UBYTE pattern[SECTOR_SIZE];
    static UBYTE readback[SECTOR_SIZE];
    WORD i;
    LONG ret;

    for (i = 0; i < SECTOR_SIZE; i++)
        pattern[i] = (UBYTE)(i ^ 0xa5);

    ret = virtio_blk_rw(RW_WRITE, 1, 1, pattern, unit);
    KDEBUG(("virtio_blk_selftest: write returned %ld\n", ret));

    ret = virtio_blk_rw(RW_READ, 1, 1, readback, unit);
    KDEBUG(("virtio_blk_selftest: read returned %ld\n", ret));

    if (memcmp(pattern, readback, SECTOR_SIZE) == 0)
        KDEBUG(("virtio_blk_selftest: unit %d PASS\n", unit));
    else
        KDEBUG(("virtio_blk_selftest: unit %d FAIL\n", unit));
}
#endif

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

        if (!virtio_setup_queue(&virtio_blk_dev[virtio_blk_count]))
        {
            KDEBUG(("virtio_blk_init: slot %d found but queue setup failed\n", slot));
            continue;
        }

        virtio_blk_capacity[virtio_blk_count] = virtio_blk_read_capacity(base);
        virtio_blk_connect_irq(slot, virtio_blk_count);

        KDEBUG(("virtio_blk_init: unit %d at slot %d (base 0x%08lx, %lu sectors)\n",
                virtio_blk_count, slot, base, virtio_blk_capacity[virtio_blk_count]));

        /* Bump the count before the self-test: virtio_blk_rw()/_ioctl() both
         * guard on "dev >= virtio_blk_count", so a self-test against the
         * unit that's still being registered would be rejected as EUNDEV. */
        virtio_blk_count++;

#ifdef ENABLE_KDEBUG
        virtio_blk_selftest(virtio_blk_count - 1);
#endif
    }

    KDEBUG(("virtio_blk_init: %d device(s) found\n", virtio_blk_count));
}

LONG virtio_blk_ioctl(UWORD drv, UWORD ctrl, void *arg)
{
    if (drv >= (UWORD)virtio_blk_count)
        return EUNDEV;

    switch (ctrl)
    {
    case GET_DISKNAME:
        strlcpy((char *)arg, "Virtio-Blk", 40);
        return 0;
    case GET_DISKINFO:
    {
        ULONG *info = (ULONG *)arg;
        info[0] = virtio_blk_capacity[drv];
        info[1] = SECTOR_SIZE;
        return 0;
    }
    case GET_MEDIACHANGE:
        return MEDIANOCHANGE;
    default:
        return EUNDEV;
    }
}

LONG virtio_blk_rw(WORD rw, LONG sector, WORD count, UBYTE *buf, WORD dev)
{
    struct virtio_blk_req *hdr;
    UBYTE *status;
    WORD i;
    LONG ret = 0;

    if (dev >= (WORD)virtio_blk_count)
        return EUNDEV;

    hdr = &virtio_blk_hdr[dev];
    status = &virtio_blk_status[dev];

    for (i = 0; i < (WORD)count; i++)
    {
        hdr->type = cpu2le32((rw & RW_RW) ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN);
        hdr->reserved = cpu2le32(0);
        hdr->sector = (QUAD)cpu2le64((UQUAD)(sector + i));
        *status = 0xff;

        virtio_desc_set(&virtio_blk_dev[dev], 0, VIRTIO_BLK_PHYS(hdr), (ULONG)sizeof(*hdr), VIRTIO_DESC_F_NEXT, 1);
        virtio_desc_set(&virtio_blk_dev[dev], 1, VIRTIO_BLK_PHYS(buf + i * SECTOR_SIZE), SECTOR_SIZE,
                         (UWORD)(VIRTIO_DESC_F_NEXT | ((rw & RW_RW) ? 0 : VIRTIO_DESC_F_WRITE)), 2);
        virtio_desc_set(&virtio_blk_dev[dev], 2, VIRTIO_BLK_PHYS(status), 1, VIRTIO_DESC_F_WRITE, 0);

#if ARCH_ARM
        {
            extern void flush_data_cache(void *start, long size);
            extern void invalidate_data_cache(void *start, long size);

            flush_data_cache(hdr, sizeof(*hdr));
            if (rw & RW_RW)
                flush_data_cache(buf + i * SECTOR_SIZE, SECTOR_SIZE);
            invalidate_data_cache(status, 1);
        }
#endif

        virtio_submit(&virtio_blk_dev[dev], 0);
        virtio_notify(&virtio_blk_dev[dev]);

        {
            /* Every other block-I/O driver in this tree bounds its hardware
             * wait with a timeout (see e.g. bios/sd.c's SD_READ_TIMEOUT_TICKS
             * idiom) rather than looping forever; a misrouted interrupt or an
             * unresponsive device must not hang the BIOS permanently. */
            LONG timeout = hz_200 + VIRTIO_BLK_TIMEOUT_TICKS;

            while (!virtio_blk_dev[dev].done)
            {
                if (hz_200 >= timeout)
                {
                    ret = (rw & RW_RW) ? EWRITF : EREADF;
                    KDEBUG(("virtio_blk_rw: unit %d timed out\n", dev));
                    return ret;
                }
#if ARCH_ARM
                __asm__ volatile("wfi");
#endif
            }
        }

#if ARCH_ARM
        {
            extern void invalidate_data_cache(void *start, long size);

            invalidate_data_cache(status, 1);
            if (!(rw & RW_RW))
                invalidate_data_cache(buf + i * SECTOR_SIZE, SECTOR_SIZE);
        }
#endif

        if (*status != 0)
        {
            ret = (rw & RW_RW) ? EWRITF : EREADF;
            break;
        }
    }

    KDEBUG(("virtio_blk_rw(%d,%ld,%d,%p,%d) rc=%ld\n", rw, sector, count, buf, dev, ret));
    return ret;
}

#endif /* CONF_WITH_VIRTIO_BLK */
