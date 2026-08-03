/*
 * virtio_input.c - virtio-input keyboard/mouse driver for the QEMU
 * virt-arm/virt-m68k boards
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*#define ENABLE_KDEBUG*/

#include "config.h"

#if CONF_WITH_VIRTIO_INPUT

#include "portab.h"
#include "asm.h"
#include "kprint.h"
#include "endian.h"
#include "virtio.h"
#include "virtio_input.h"
#include "ikbd.h"
#include "tosvars.h"
#include "virtio_input_keytbl.h"
#include "lineavars.h"

#if defined(MACHINE_VIRT_ARM)
#include "virt_memmap.h"
#include "virt_pic.h"
#define VIRTIO_MMIO_BASE    VIRT_VIRTIO_MMIO_BASE
#define VIRTIO_MMIO_STRIDE  VIRT_VIRTIO_MMIO_STRIDE
#define VIRTIO_MMIO_COUNT   VIRT_VIRTIO_MMIO_COUNT
#elif defined(MACHINE_VIRT_M68K)
#include "goldfish_pic.h"
#define VIRTIO_MMIO_BASE    0xff010000UL
#define VIRTIO_MMIO_STRIDE  0x200UL
#define VIRTIO_MMIO_COUNT   128
#endif

#if ARCH_ARM
extern void invalidate_data_cache(void *start, long size);
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

#define REL_X  0x00
#define REL_Y  0x01

#define BTN_LEFT    0x110
#define BTN_RIGHT   0x111
#define BTN_MIDDLE  0x112

#define VIRTIO_INPUT_MOUSE_LEFT   0x02   /* MOUSE_REL_POS_REPORT button bits -- see
                                           * bios/ikbd.c's private LEFT_BUTTON_DOWN/
                                           * RIGHT_BUTTON_DOWN #defines, confirmed also
                                           * by usb/udd_mouse.c's identical packet[0]
                                           * construction */
#define VIRTIO_INPUT_MOUSE_RIGHT  0x01

static VIRTIO_DEV virtio_input_kbd_dev;
static VIRTIO_DEV virtio_input_ptr_dev;
static BOOL virtio_input_kbd_present;
static BOOL virtio_input_ptr_present;
static BOOL virtio_input_ptr_is_abs;      /* TRUE: tablet (EV_ABS), FALSE: mouse (EV_REL) */
static LONG virtio_input_abs_min_x, virtio_input_abs_max_x;
static LONG virtio_input_abs_min_y, virtio_input_abs_max_y;

/* Wire format of one eventq entry (virtio-input spec 5.8.6.2): 8 bytes,
 * every field little-endian regardless of guest endianness. */
struct virtio_input_event
{
    UWORD type;
    UWORD code;
    ULONG value;
};

/* Each event buffer -- the whole VIRTIO_QUEUE_SIZE-entry array, not each
 * individual entry -- is aligned to its own cache-line-aligned span,
 * following bios/virtio_blk.c's status-slot precedent: the per-entry
 * invalidate_data_cache() in virtio_input_drain() (needed to see the
 * device's fresh write past whatever the CPU had cached) is a pure
 * invalidate, so a driver static sharing a line with the array would
 * silently lose its pending CPU writes on every input interrupt.  Aligning
 * the array's base also guarantees no individual entry straddles a line
 * boundary (which would leave part of it uninvalidated and hence read back
 * stale), since an 8-byte event size divides evenly into a 64-byte line:
 * every entry starts and ends within a single line, never spanning two. */
static struct virtio_input_event virtio_input_kbd_buf[VIRTIO_QUEUE_SIZE]
        __attribute__((aligned(VIRTIO_CACHE_LINE)));
static struct virtio_input_event virtio_input_ptr_buf[VIRTIO_QUEUE_SIZE]
        __attribute__((aligned(VIRTIO_CACHE_LINE)));

#define VIRTIO_INPUT_KEY_AUTOREPEAT  2
#define VIRTIO_INPUT_KEY_RELEASED    0x80   /* mirrors ikbd.c's private
                                              * KEY_RELEASED bit, which
                                              * isn't exported via ikbd.h */

typedef enum { VIRTIO_INPUT_ROLE_KEYBOARD, VIRTIO_INPUT_ROLE_POINTER } VIRTIO_INPUT_ROLE;

static UBYTE virtio_input_ptr_buttons;
static BOOL virtio_input_x_valid, virtio_input_y_valid;   /* have we seen a baseline EV_ABS sample yet? */
static LONG virtio_input_last_scaled_x, virtio_input_last_scaled_y;

/* Sends one or more 3-byte IKBD relative-mouse packets covering (dx, dy),
 * chunking into signed-byte-range steps if either component overflows
 * it. Always sends at least one packet (even (0, 0), for button-only
 * changes), since the ST always reports current button state alongside
 * whatever motion happened. */
static void virtio_input_send_mouse_delta(WORD dx, WORD dy)
{
    UBYTE packet[3];
    WORD step_x, step_y;

    do
    {
        step_x = (dx > 127) ? 127 : (dx < -128) ? -128 : dx;
        step_y = (dy > 127) ? 127 : (dy < -128) ? -128 : dy;

        packet[0] = (UBYTE)(0xf8 | virtio_input_ptr_buttons);
        packet[1] = (UBYTE)step_x;
        packet[2] = (UBYTE)step_y;
        call_mousevec(packet);

        dx = (WORD)(dx - step_x);
        dy = (WORD)(dy - step_y);
    } while (dx != 0 || dy != 0);
}

static LONG virtio_input_scale_abs(LONG value, LONG min, LONG max, WORD screen_max)
{
    if (max <= min)
        return 0;
    return (value - min) * (LONG)screen_max / (max - min);
}

static void virtio_input_handle_pointer(UWORD type, UWORD code, ULONG raw_value)
{
    LONG value = (LONG)raw_value;

    switch (type)
    {
    case EV_KEY:
        switch (code)
        {
        case BTN_LEFT:
            if (value)
                virtio_input_ptr_buttons |= VIRTIO_INPUT_MOUSE_LEFT;
            else
                virtio_input_ptr_buttons &= ~VIRTIO_INPUT_MOUSE_LEFT;
            virtio_input_send_mouse_delta(0, 0);
            break;
        case BTN_RIGHT:
            if (value)
                virtio_input_ptr_buttons |= VIRTIO_INPUT_MOUSE_RIGHT;
            else
                virtio_input_ptr_buttons &= ~VIRTIO_INPUT_MOUSE_RIGHT;
            virtio_input_send_mouse_delta(0, 0);
            break;
        case BTN_MIDDLE:
            /* No 3rd button bit in the relative-mouse packet; matches
             * usb/udd_mouse.c's handling of its own 3rd button. */
            mousexvec(value ? 0x37 : 0xb7);
            break;
        default:
            break;
        }
        break;

    case EV_REL:
        /* (WORD)value truncates rather than clamps a delta outside
         * [-32768, 32767]; accepted rather than fixed. QEMU's virtio-mouse
         * derives REL_X/REL_Y from per-poll host mouse motion, which is
         * single-digit to low-hundreds, orders of magnitude under the
         * truncation point. Worst case is one wrong-direction jump,
         * self-corrected by the next event -- no crash, no persistent
         * state damage (unlike the ABS path, this keeps no accumulator). */
        if (code == REL_X)
            virtio_input_send_mouse_delta((WORD)value, 0);
        else if (code == REL_Y)
            virtio_input_send_mouse_delta(0, (WORD)value);
        break;

    case EV_ABS:
        if (code == ABS_X)
        {
            LONG scaled = virtio_input_scale_abs(value, virtio_input_abs_min_x, virtio_input_abs_max_x,
                                                  (WORD)(linea_vars.V_REZ_HZ - 1));
            if (virtio_input_x_valid)
                virtio_input_send_mouse_delta((WORD)(scaled - virtio_input_last_scaled_x), 0);
            virtio_input_last_scaled_x = scaled;
            virtio_input_x_valid = TRUE;
        }
        else if (code == ABS_Y)
        {
            LONG scaled = virtio_input_scale_abs(value, virtio_input_abs_min_y, virtio_input_abs_max_y,
                                                  (WORD)(linea_vars.V_REZ_VT - 1));
            if (virtio_input_y_valid)
                virtio_input_send_mouse_delta(0, (WORD)(scaled - virtio_input_last_scaled_y));
            virtio_input_last_scaled_y = scaled;
            virtio_input_y_valid = TRUE;
        }
        break;

    default:
        break;   /* EV_SYN and anything else: nothing to do per-event */
    }
}

static void virtio_input_handle_key(UWORD code, ULONG value)
{
    UBYTE scancode;

    if (value == VIRTIO_INPUT_KEY_AUTOREPEAT)
        return;   /* bios/ikbd.c's kb_timerc_int() already owns repeat timing */

    if (code >= VIRTIO_INPUT_KEYTBL_SIZE || virtio_input_keytbl[code] == 0)
    {
        KDEBUG(("virtio_input: no scancode for evdev KEY code %u\n", code));
        return;
    }

    scancode = virtio_input_keytbl[code];
    if (value == 0)
        scancode |= VIRTIO_INPUT_KEY_RELEASED;

    kbd_int(scancode);
}

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

static void virtio_input_setup_eventq(VIRTIO_DEV *dev, struct virtio_input_event *buf)
{
    UWORD i;

#if ARCH_ARM
    /* Invalidate before the first submission: if the BSS clear at boot left
     * dirty cache lines over these buffers, a later natural eviction could
     * write stale zeros back over an event the device has already DMA'd in,
     * racing ahead of the ISR's own invalidate_data_cache() call in
     * virtio_input_drain() below. Not needed on m68k, which has no data
     * cache in this port. */
    invalidate_data_cache(buf, (long)sizeof(struct virtio_input_event) * VIRTIO_QUEUE_SIZE);
#endif

    for (i = 0; i < VIRTIO_QUEUE_SIZE; i++)
    {
        virtio_desc_set(dev, i, (ULONG)&buf[i] + dev->phys_offset,
                         (ULONG)sizeof(buf[i]), VIRTIO_DESC_F_WRITE, 0);
        virtio_submit(dev, i);
    }
    virtio_notify(dev);
}

/* Everything this driver dispatches into -- bios/ikbd.c's kbd_int(), and the
 * VDI/AES chain behind call_mousevec() (vdi/arch/m68k/vdi_entry.S's
 * mouse_int(), then the AES's far_bcha()/far_mcha() in
 * aes/arch/m68k/gemdosif.S) -- is written on the assumption that it cannot be
 * interrupted by the system timer tick.  On every other m68k machine that
 * holds for free: mouse and key packets arrive from the IKBD interrupt at the
 * same IPL as the tick (both are IPL 6 on the Atari -- ACIA and MFP timer C),
 * so the two paths can never nest.  aes/gemdisp.c's forkq() states the
 * requirement outright ("q a fork process, enter with ints OFF"), and both
 * far_mcha() and the AES's own tick routine _tikcod call it; those two also
 * switch to private stacks whose saved stack pointer lives in a single global
 * slot each (gstksave/tstksave, aes/arch/m68k/gemdosif.S), which likewise only
 * works if they cannot interrupt one another.
 *
 * QEMU's m68k virt board gives no such guarantee.  The virtio-mmio slots hang
 * off Goldfish PIC instances 1-4, which hw/intc/m68k_irqc.c takes at CPU
 * autovector levels 2-5 -- all *below* the Goldfish RTC on instance 5, whose
 * level 6 drives EmuTOS's 200 Hz tick.  So the tick preempts this dispatch
 * mid-flight and re-enters forkq(), corrupting the AES fork queue (fpt/fpcnt
 * and D.g_fpdx[]) until the dispatcher calls a garbage f_code.
 *
 * Masking to IPL 7 for the duration of the dispatch restores the mutual
 * exclusion those handlers require.  No tick is lost by it: the Goldfish RTC's
 * interrupt line stays asserted until goldfish_rtc_service() acknowledges it,
 * so a tick raised while we are masked is simply taken on the way out.
 *
 * Nothing is needed on ARM, where the CPU masks IRQs on exception entry.
 */
static void virtio_input_drain(VIRTIO_DEV *dev, struct virtio_input_event *buf, VIRTIO_INPUT_ROLE role)
{
    ULONG idx;
    ULONG len;
    UWORD type, code;
    ULONG value;
#if defined(MACHINE_VIRT_M68K)
    WORD saved_sr;
#endif

    virtio_handle_interrupt(dev);

#if defined(MACHINE_VIRT_M68K)
    saved_sr = set_sr(0x2700);
#endif

    while (virtio_pop_used(dev, &idx, &len))
    {
        /* Never trust the device's descriptor index as an array subscript:
         * a malformed one would index outside buf[] and the descriptor
         * table.  Such an entry can't be recycled either (we have no valid
         * descriptor slot to rewrite), so just drop it. */
        if (idx >= VIRTIO_QUEUE_SIZE)
        {
            KDEBUG(("virtio_input: bad descriptor index %lu from used ring, dropped\n", idx));
            continue;
        }

#if ARCH_ARM
        invalidate_data_cache(&buf[idx], sizeof(buf[idx]));
#endif

        /* A short write means the device didn't produce a whole event;
         * decoding it would read fields it never filled in.  The buffer is
         * still ours, so it gets recycled below either way. */
        if (len < (ULONG)sizeof(buf[idx]))
        {
            KDEBUG(("virtio_input: short event, %ld of %ld bytes, ignored\n",
                    len, (LONG)sizeof(buf[idx])));
        }
        else
        {
            type  = le2cpu16(buf[idx].type);
            code  = le2cpu16(buf[idx].code);
            value = le2cpu32(buf[idx].value);

            if (role == VIRTIO_INPUT_ROLE_KEYBOARD)
            {
                if (type == EV_KEY)
                    virtio_input_handle_key(code, value);
            }
            else
            {
                virtio_input_handle_pointer(type, code, value);
            }
        }

        virtio_desc_set(dev, (UWORD)idx, (ULONG)&buf[idx] + dev->phys_offset,
                         (ULONG)sizeof(buf[idx]), VIRTIO_DESC_F_WRITE, 0);
        virtio_submit(dev, (UWORD)idx);
    }

#if defined(MACHINE_VIRT_M68K)
    set_sr(saved_sr);
#endif

    virtio_notify(dev);
}

static void virtio_input_kbd_isr(void)
{
    virtio_input_drain(&virtio_input_kbd_dev, virtio_input_kbd_buf, VIRTIO_INPUT_ROLE_KEYBOARD);
}

static void virtio_input_ptr_isr(void)
{
    virtio_input_drain(&virtio_input_ptr_dev, virtio_input_ptr_buf, VIRTIO_INPUT_ROLE_POINTER);
}

static void virtio_input_connect_irq(WORD slot, PFVOID handler)
{
#if defined(MACHINE_VIRT_ARM)
    virt_connect_irq(VIRT_VIRTIO_IRQ_BASE + slot, handler);
#elif defined(MACHINE_VIRT_M68K)
    goldfish_pic_connect_irq((WORD)(1 + slot / 32), (WORD)(slot % 32), handler);
#endif
}

void virtio_input_init(void)
{
    WORD slot;
    ULONG base;
    VIRTIO_DEV probe_dev;

    virtio_input_keytbl_init();

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
#if defined(MACHINE_VIRT_ARM)
            virtio_input_ptr_dev.phys_offset = VIRT_RAM_BASE;
#elif defined(MACHINE_VIRT_M68K)
            virtio_input_ptr_dev.phys_offset = 0;
#endif
            if (!virtio_setup_queue(&virtio_input_ptr_dev))
            {
                KDEBUG(("virtio_input_init: slot %d tablet queue setup failed\n", slot));
                continue;
            }
            virtio_input_read_absinfo(base, ABS_X, &virtio_input_abs_min_x, &virtio_input_abs_max_x);
            virtio_input_read_absinfo(base, ABS_Y, &virtio_input_abs_min_y, &virtio_input_abs_max_y);
            virtio_input_connect_irq(slot, virtio_input_ptr_isr);
            virtio_input_setup_eventq(&virtio_input_ptr_dev, virtio_input_ptr_buf);
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
#if defined(MACHINE_VIRT_ARM)
            virtio_input_ptr_dev.phys_offset = VIRT_RAM_BASE;
#elif defined(MACHINE_VIRT_M68K)
            virtio_input_ptr_dev.phys_offset = 0;
#endif
            if (!virtio_setup_queue(&virtio_input_ptr_dev))
            {
                KDEBUG(("virtio_input_init: slot %d mouse queue setup failed\n", slot));
                continue;
            }
            virtio_input_connect_irq(slot, virtio_input_ptr_isr);
            virtio_input_setup_eventq(&virtio_input_ptr_dev, virtio_input_ptr_buf);
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
#if defined(MACHINE_VIRT_ARM)
            virtio_input_kbd_dev.phys_offset = VIRT_RAM_BASE;
#elif defined(MACHINE_VIRT_M68K)
            virtio_input_kbd_dev.phys_offset = 0;
#endif
            if (!virtio_setup_queue(&virtio_input_kbd_dev))
            {
                KDEBUG(("virtio_input_init: slot %d keyboard queue setup failed\n", slot));
                continue;
            }
            virtio_input_connect_irq(slot, virtio_input_kbd_isr);
            virtio_input_setup_eventq(&virtio_input_kbd_dev, virtio_input_kbd_buf);
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
