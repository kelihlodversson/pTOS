/*
 * virtio_input.c - virtio-input keyboard/mouse driver for the QEMU
 * virt-arm/virt-m68k boards
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#define ENABLE_KDEBUG

#include "config.h"

#if CONF_WITH_VIRTIO_INPUT

#include "portab.h"
#include "kprint.h"
#include "endian.h"
#include "virtio.h"
#include "virtio_input.h"

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

#define VIRTIO_INPUT_DEVICE_ID  18

/* Config space, at offset 0x100 from the device's mmio base (virtio-input
 * spec 5.8.5): select/subsel pick a query, size says how many bytes of
 * the union at +0x08 the device filled in (0 == "not supported"). */
#define VIRTIO_INPUT_CFG_SELECT  0x100
#define VIRTIO_INPUT_CFG_SUBSEL  0x101
#define VIRTIO_INPUT_CFG_SIZE    0x102
#define VIRTIO_INPUT_CFG_DATA    0x108

#define VIRTIO_INPUT_CFG_EV_BITS   0x11
#define VIRTIO_INPUT_CFG_ABS_INFO  0x12

#define EV_KEY  0x01
#define EV_REL  0x02
#define EV_ABS  0x03

#define ABS_X  0x00
#define ABS_Y  0x01

static VIRTIO_DEV virtio_input_kbd_dev;
static VIRTIO_DEV virtio_input_ptr_dev;
static BOOL virtio_input_kbd_present;
static BOOL virtio_input_ptr_present;
static BOOL virtio_input_ptr_is_abs;      /* TRUE: tablet (EV_ABS), FALSE: mouse (EV_REL) */
static LONG virtio_input_abs_min_x, virtio_input_abs_max_x;
static LONG virtio_input_abs_min_y, virtio_input_abs_max_y;

/* Writes select/subsel, returns the "size" byte -- nonzero means the
 * device supports that (select, subsel) query. */
static UBYTE virtio_input_cfg_query(ULONG base, UBYTE select, UBYTE subsel)
{
    volatile UBYTE *cfg = (volatile UBYTE *)base;

    cfg[VIRTIO_INPUT_CFG_SELECT] = select;
    cfg[VIRTIO_INPUT_CFG_SUBSEL] = subsel;
    return cfg[VIRTIO_INPUT_CFG_SIZE];
}

static void virtio_input_read_absinfo(ULONG base, UBYTE axis, LONG *out_min, LONG *out_max)
{
    volatile ULONG *data = (volatile ULONG *)(base + VIRTIO_INPUT_CFG_DATA);
    UBYTE size;

    size = virtio_input_cfg_query(base, VIRTIO_INPUT_CFG_ABS_INFO, axis);
    if (size > 0)
    {
        *out_min = (LONG)le2cpu32(data[0]);
        *out_max = (LONG)le2cpu32(data[1]);
    }
    else
    {
        *out_min = 0;
        *out_max = 0;
    }
}

void virtio_input_init(void)
{
    WORD slot;
    ULONG base;
    VIRTIO_DEV probe_dev;

    virtio_input_kbd_present = FALSE;
    virtio_input_ptr_present = FALSE;

    for (slot = 0; slot < VIRTIO_MMIO_COUNT; slot++)
    {
        base = VIRTIO_MMIO_BASE + (ULONG)slot * VIRTIO_MMIO_STRIDE;

        if (!virtio_probe(base, VIRTIO_INPUT_DEVICE_ID, &probe_dev))
            continue;

        /* EV_ABS/EV_REL are checked before EV_KEY: QEMU's virtio-tablet-device
         * and virtio-mouse-device both also report nonzero EV_KEY size (for
         * their button codes), so an EV_KEY-first check would misclassify
         * them as a second keyboard and never detect the pointer. No real
         * keyboard reports motion capability, so "EV_KEY set, EV_ABS/EV_REL
         * not" is what actually identifies a keyboard. */
        if (virtio_input_cfg_query(base, VIRTIO_INPUT_CFG_EV_BITS, EV_ABS) != 0)
        {
            if (virtio_input_ptr_present)
            {
                KDEBUG(("virtio_input_init: slot %d is another pointer, ignored\n", slot));
                continue;
            }
            virtio_input_ptr_dev = probe_dev;
            virtio_input_ptr_is_abs = TRUE;
            virtio_input_read_absinfo(base, ABS_X, &virtio_input_abs_min_x, &virtio_input_abs_max_x);
            virtio_input_read_absinfo(base, ABS_Y, &virtio_input_abs_min_y, &virtio_input_abs_max_y);
            virtio_input_ptr_present = TRUE;
            KDEBUG(("virtio_input_init: tablet at slot %d (base 0x%08lx, x %ld..%ld, y %ld..%ld)\n",
                    slot, base, virtio_input_abs_min_x, virtio_input_abs_max_x,
                    virtio_input_abs_min_y, virtio_input_abs_max_y));
        }
        else if (virtio_input_cfg_query(base, VIRTIO_INPUT_CFG_EV_BITS, EV_REL) != 0)
        {
            if (virtio_input_ptr_present)
            {
                KDEBUG(("virtio_input_init: slot %d is another pointer, ignored\n", slot));
                continue;
            }
            virtio_input_ptr_dev = probe_dev;
            virtio_input_ptr_is_abs = FALSE;
            virtio_input_ptr_present = TRUE;
            KDEBUG(("virtio_input_init: mouse at slot %d (base 0x%08lx)\n", slot, base));
        }
        else if (virtio_input_cfg_query(base, VIRTIO_INPUT_CFG_EV_BITS, EV_KEY) != 0)
        {
            if (virtio_input_kbd_present)
            {
                KDEBUG(("virtio_input_init: slot %d is another keyboard, ignored\n", slot));
                continue;
            }
            virtio_input_kbd_dev = probe_dev;
            virtio_input_kbd_present = TRUE;
            KDEBUG(("virtio_input_init: keyboard at slot %d (base 0x%08lx)\n", slot, base));
        }
        else
        {
            KDEBUG(("virtio_input_init: slot %d has no recognized role, ignored\n", slot));
        }
    }

    KDEBUG(("virtio_input_init: keyboard %s, pointer %s\n",
            virtio_input_kbd_present ? "present" : "absent",
            virtio_input_ptr_present ? (virtio_input_ptr_is_abs ? "present (tablet)" : "present (mouse)") : "absent"));
}

#endif /* CONF_WITH_VIRTIO_INPUT */
