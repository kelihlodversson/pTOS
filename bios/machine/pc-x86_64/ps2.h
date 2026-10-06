/*
 * ps2.h - x86-64 legacy PS/2 keyboard/mouse controller (i8042)
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef PC_X86_64_PS2_H
#define PC_X86_64_PS2_H

/*
 * Resets and configures the i8042 controller: flushes any stale output
 * byte, enables IBM PC/XT scancode translation (Set 1) on the keyboard
 * port -- the same convention bios/ikbd.c's own KEY_* constants already
 * assume (KEY_ESCAPE=0x01, KEY_LSHIFT=0x2a, KEY_RETURN=0x1c, ... -- Set 1
 * values throughout, a direct legacy of the Atari ST's own 6301 keyboard
 * controller using a closely related scancode convention), so
 * x86_64_ps2_keyboard_irq() below needs no scancode translation of its
 * own -- enables both the keyboard and mouse ports, and enables the
 * mouse's own default (2-button, 3-byte-packet, no wheel) streaming
 * mode. Does not itself unmask or enable either device's IRQ -- that is
 * bios/machine/pc-x86_64/irq.c's job, once its own IDT gates and PIC
 * remap are in place.
 */
void x86_64_ps2_init(void);

/* Called from bios/machine/pc-x86_64/irq.c's IRQ1 dispatch: reads one
 * scancode byte from the controller and feeds it to bios/ikbd.c's
 * kbd_int(), the same machine-independent entry point every other IKBD-
 * equivalent input driver (USB HID included) uses. */
void x86_64_ps2_keyboard_irq(void);

/*
 * Called from bios/machine/pc-x86_64/irq.c's IRQ12 dispatch: reads one
 * byte of the mouse's own 3-byte relative-movement packet (assembled
 * across three separate IRQ12 firings -- one byte each) and, once a full
 * packet is in hand, translates it into bios/ikbd.c's own IKBD-format
 * mouse_packet[] (button bits swapped, Y sign flipped -- see this
 * function's own comment in ps2.c for the exact PS/2-to-IKBD mapping)
 * and calls call_mousevec(), the same machine-independent mouse-event
 * entry point the keyboard-emulated mouse (ikbd.c's own
 * handle_mouse_mode()) already uses.
 */
void x86_64_ps2_mouse_irq(void);

#endif /* PC_X86_64_PS2_H */
