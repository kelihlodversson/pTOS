/*
 * ps2.c - x86-64 legacy PS/2 keyboard/mouse controller (i8042)
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_PC_X86_64
#error This file must only be compiled for the pc-x86_64 machine
#endif

#include "portab.h"
#include "io.h"
#include "ps2.h"
#include "ikbd.h"
#include "bios.h"  /* kbdvecs -- ikbd.h's call_mousevec() macro reads it */

#define PS2_DATA    0x60
#define PS2_STATUS  0x64  /* read */
#define PS2_COMMAND 0x64  /* write */

#define PS2_STATUS_OUTPUT_FULL 0x01  /* a byte is waiting at PS2_DATA */
#define PS2_STATUS_INPUT_FULL  0x02  /* the controller hasn't consumed
                                      * the last byte written yet */

#define PS2_CMD_READ_CONFIG     0x20
#define PS2_CMD_WRITE_CONFIG    0x60
#define PS2_CMD_DISABLE_PORT2   0xA7
#define PS2_CMD_ENABLE_PORT2    0xA8
#define PS2_CMD_DISABLE_PORT1   0xAD
#define PS2_CMD_ENABLE_PORT1    0xAE
#define PS2_CMD_WRITE_PORT2     0xD4  /* next PS2_DATA write goes to the
                                       * aux (mouse) port instead of the
                                       * keyboard port */

/* Configuration byte bits (Intel/IBM PS/2 Hardware Interface Technical
 * Reference, i8042 controller command byte): */
#define PS2_CFG_PORT1_IRQ_ENABLE  0x01
#define PS2_CFG_PORT2_IRQ_ENABLE  0x02
#define PS2_CFG_PORT1_TRANSLATE   0x40

#define MOUSE_CMD_ENABLE_REPORTING 0xF4
#define MOUSE_ACK 0xFA

/*
 * Bounded retry counts, not a real elapsed-time timeout: hz_200 isn't
 * ticking yet when this runs (the PIT that drives it is programmed and
 * unmasked only after this returns, see irq.c's own x86_64_irq_init()),
 * and this is boot-time, one-shot setup, not a latency-sensitive path --
 * a plain iteration bound is the same defensive convention other early,
 * pre-tick x86-64 boot code already uses (e.g. gop.c's own firmware-
 * distrust posture) against a port that never responds (no PS/2
 * controller at all, or a stuck line) hanging boot forever instead of
 * just leaving that one feature unavailable.
 */
#define PS2_POLL_LIMIT 100000

static BOOL wait_input_clear(void)
{
    int i;

    for (i = 0; i < PS2_POLL_LIMIT; i++) {
        if (!(x86_64_inb(PS2_STATUS) & PS2_STATUS_INPUT_FULL))
            return TRUE;
    }
    return FALSE;
}

static BOOL wait_output_full(void)
{
    int i;

    for (i = 0; i < PS2_POLL_LIMIT; i++) {
        if (x86_64_inb(PS2_STATUS) & PS2_STATUS_OUTPUT_FULL)
            return TRUE;
    }
    return FALSE;
}

static void ps2_write_command(UBYTE cmd)
{
    if (wait_input_clear())
        x86_64_outb(PS2_COMMAND, cmd);
}

static void ps2_write_data(UBYTE data)
{
    if (wait_input_clear())
        x86_64_outb(PS2_DATA, data);
}

static UBYTE ps2_read_data(void)
{
    if (!wait_output_full())
        return 0;
    return x86_64_inb(PS2_DATA);
}

static void ps2_write_aux(UBYTE data)
{
    ps2_write_command(PS2_CMD_WRITE_PORT2);
    ps2_write_data(data);
}

void x86_64_ps2_init(void)
{
    UBYTE config;

    ps2_write_command(PS2_CMD_DISABLE_PORT1);
    ps2_write_command(PS2_CMD_DISABLE_PORT2);

    /* Flush whatever stale byte (if any) is left over from firmware's own
     * use of the controller before we start relying on the output-full
     * bit meaning "a byte we asked for." Bounded like every other poll
     * in this file: a controller whose status port is stuck reading
     * "output full" (no i8042 at all, or a wedged one) must not hang
     * boot here, before the PIT/PIC setup that follows this call ever
     * runs. */
    {
        int i;
        for (i = 0; i < PS2_POLL_LIMIT
                     && (x86_64_inb(PS2_STATUS) & PS2_STATUS_OUTPUT_FULL); i++)
            x86_64_inb(PS2_DATA);
    }

    ps2_write_command(PS2_CMD_READ_CONFIG);
    config = ps2_read_data();
    config |= PS2_CFG_PORT1_IRQ_ENABLE | PS2_CFG_PORT2_IRQ_ENABLE
              | PS2_CFG_PORT1_TRANSLATE;
    ps2_write_command(PS2_CMD_WRITE_CONFIG);
    ps2_write_data(config);

    ps2_write_command(PS2_CMD_ENABLE_PORT1);
    ps2_write_command(PS2_CMD_ENABLE_PORT2);

    /* Enable Data Reporting (the mouse's own default 3-byte packet,
     * 2-button streaming mode -- no "magic knock" sequence for the
     * 4-byte IntelliMouse wheel variant, deliberately: this driver only
     * ever parses a 3-byte packet). ACK is read and discarded: if the
     * mouse doesn't respond (headless/no-mouse QEMU config, or real
     * hardware with none attached), IRQ12 simply never fires again and
     * the keyboard/timer paths are unaffected. */
    ps2_write_aux(MOUSE_CMD_ENABLE_REPORTING);
    if (wait_output_full())
        x86_64_inb(PS2_DATA);
}

/*
 * Set 1 also has two "extended" prefix bytes ahead of keys with no place
 * in the original XT layout: 0xe0 for the dedicated arrow-key cluster,
 * Ins/Del/Home/End/PgUp/PgDn and right Ctrl/Alt, 0xe1 for Pause/Break
 * alone. bios/ikbd.c's own convert_scancode() indexes
 * current_keytbl.norm[] et al (its own comment: "128-byte direct
 * scancode lookup tables") with whatever kbd_int() is handed, so passing
 * either prefix through unfiltered is an out-of-bounds read there.
 *
 * The 0xe0 byte itself carries no key of its own, but the single byte
 * that follows it does: Set 1 reuses the same low byte for the dedicated
 * cluster as for the numpad-area/left-side key it sits next to (0xe0 0x48
 * for the physical Up arrow is the same 0x48 as numpad-8/KEY_UPARROW;
 * likewise 0x52/KEY_INSERT, 0x53/KEY_DELETE, 0x47/KEY_HOME, and so on),
 * so dropping just the prefix and forwarding the next byte as-is gives
 * kbd_int() a code it already understands -- no separate extended-key
 * mapping needed.
 *
 * 0xe1 has no such follow-on key: it only ever starts Pause/Break's own
 * fixed six-byte sequence (E1 1D 45 E1 9D C5), which this driver has no
 * use for and no partial-byte meaning to give, so the whole sequence
 * (the five bytes after the 0xe1 itself) is swallowed instead of being
 * forwarded piecemeal as unrelated keys.
 */
static int ps2_e1_bytes_left;

void x86_64_ps2_keyboard_irq(void)
{
    UBYTE sc = x86_64_inb(PS2_DATA);

    if (sc == 0xe0)
        return;      /* drop the prefix; the next byte stands on its own */
    if (sc == 0xe1) {
        ps2_e1_bytes_left = 5;
        return;
    }
    if (ps2_e1_bytes_left) {
        ps2_e1_bytes_left--;
        return;
    }

    kbd_int(sc);
}

/*
 * PS/2 mouse packets arrive one byte per IRQ12 firing, three bytes per
 * logical packet:
 *   byte 0: bit0 left button, bit1 right button, bit2 middle button
 *           (ignored -- see below), bit3 always 1 (sync marker), bit4 X
 *           sign, bit5 Y sign, bit6/7 X/Y overflow (ignored: this port's
 *           own IKBD-format mouse_packet[] can only hold an SBYTE delta
 *           anyway, no wider than what overflow would have meant).
 *   byte 1: X movement magnitude.
 *   byte 2: Y movement magnitude.
 *
 * bios/ikbd.h's IKBD-format packet (see call_mousevec()'s own callers in
 * ikbd.c for the reference encoding) differs in three ways this
 * function accounts for: the button bits are swapped (IKBD: bit0 right,
 * bit1 left), a fixed 0xf8 base value OR'ed into byte 0 instead of PS/2's
 * bit3 sync marker (the same 0xf8/bit0/bit1 convention bios/amiga.c's
 * amiga_mouse_vbl() and bios/virtio_input.c's
 * virtio_input_send_mouse_delta() already use verbatim -- this port
 * follows their precedent rather than introducing a shared constant for
 * what every existing IKBD-mouse producer already just inlines), and
 * positive Y means *down* the screen -- the opposite of PS/2's own
 * "positive Y = away from the user" convention -- so the Y delta is
 * negated. Only the base (2-button, no wheel) button set is handled: the
 * middle button (bit2) is silently ignored, matching this driver's own
 * scope (ps2.h) of not implementing the IntelliMouse wheel extension.
 *
 * Deliberately not the same bios/ikbd.c-owned mouse_packet[] the
 * keyboard's own arrow-key mouse emulation (handle_mouse_mode()) uses:
 * that array's [0] byte doubles as emulation-mode-active state, so
 * writing through it here could make a real PS/2 packet get
 * misinterpreted as "entering emulation mode" or vice versa. A private
 * packet buffer avoids that cross-talk entirely; call_mousevec() takes
 * a plain pointer; it never needs to be *this specific* array.
 *
 * State carried between successive calls (this function is never
 * reentered -- device IRQs stay masked at the PIC while one is being
 * handled, see idt.c's own interrupt-gate comment): which of the three
 * packet bytes comes next, and the first two bytes already received.
 */
static int mouse_byte_index;
static UBYTE mouse_byte0;
static UBYTE mouse_byte1;

void x86_64_ps2_mouse_irq(void)
{
    UBYTE byte = x86_64_inb(PS2_DATA);

    if (mouse_byte_index == 0 && !(byte & 0x08)) {
        /* Lost sync (or garbage before the mouse's own reporting was
         * actually enabled) -- the real first byte of every packet always
         * has bit3 set. Discard and keep waiting for a byte that does,
         * rather than building a packet from a shifted stream. */
        return;
    }

    switch (mouse_byte_index) {
    case 0:
        mouse_byte0 = byte;
        mouse_byte_index = 1;
        break;

    case 1:
        mouse_byte1 = byte;
        mouse_byte_index = 2;
        break;

    case 2:
        {
            SBYTE packet[3];
            WORD dx = mouse_byte1;
            WORD dy = byte;

            if (mouse_byte0 & 0x10)
                dx -= 256;
            if (mouse_byte0 & 0x20)
                dy -= 256;

            /* Clamp to what an SBYTE (this port's own packet[], matching
             * every other IKBD producer's field width) can hold -- only
             * reachable at implausibly high report rates/DPI, but
             * firmware-reported hardware state is never trusted blindly
             * on this arch (see gop.c's own posture). */
            if (dx > 127) dx = 127;
            if (dx < -128) dx = -128;
            if (dy > 127) dy = 127;
            if (dy < -128) dy = -128;

            /* -dy overflows SBYTE's range when dy is -128 (the negative
             * bound clamped above): -(-128) is 128, one past SBYTE_MAX,
             * which would wrap back to -128 through the cast below and
             * reverse this one edge-case motion instead of saturating
             * it. Re-clamp the negated value the same way dx/dy were
             * clamped coming in. */
            {
                WORD negated_dy = -dy;
                if (negated_dy > 127)
                    negated_dy = 127;

                packet[0] = (SBYTE)(0xf8
                                     | ((mouse_byte0 & 0x01) ? 0x02 : 0)   /* left  -> IKBD bit1 */
                                     | ((mouse_byte0 & 0x02) ? 0x01 : 0)); /* right -> IKBD bit0 */
                packet[1] = (SBYTE)dx;
                packet[2] = (SBYTE)negated_dy;  /* PS/2 Y+ is up; IKBD Y+ is down */
            }

            call_mousevec(packet);
        }
        mouse_byte_index = 0;
        break;
    }
}
