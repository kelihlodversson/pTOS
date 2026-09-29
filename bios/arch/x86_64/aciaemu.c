/*
 * aciaemu.c - keyboard/mouse vector table stand-in (x86-64)
 *
 * bios/arch/arm/aciaemu.c emulates the Atari ACIA/IKBD interrupt-driven
 * keyboard and MIDI hardware entirely in software, since ARM has neither
 * -- but ARM does have a real keyboard input path feeding it (USB HID,
 * usb/udd_keyboard.c). x86-64 has no keyboard driver of any kind yet (no
 * PS/2, no USB), so there is nothing to feed midivec()/kbdvec()-style
 * interrupt handlers here: this only provides the storage and sane dummy
 * defaults that bios/bios.c's biosmain() (init_acia_vecs(), called
 * unconditionally), bios/ikbd.c, bios/midi.c, bios/xbios.c and
 * vdi/vdi_mouse.c all depend on existing, whether or not anything ever
 * actually drives a real key or mouse event through them.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "biosdefs.h"
#include "iorec.h"
#include "vectors.h"

static UBYTE ikbdibufbuf[0x100];
static UBYTE midiibufbuf[0x80];

volatile IOREC ikbdiorec, midiiorec;

void (*mousexvec)(WORD scancode);

struct kbdvecs kbdvecs;

static void _dummy_p(UBYTE *unused) { UNUSED(unused); }
static void _dummy_c(UBYTE unused) { UNUSED(unused); }
static void _dummy_w(WORD unused) { UNUSED(unused); }

void init_acia_vecs(void)
{
    /* mousexvec must never be NULL: mouse drivers call it from interrupt
     * context for the extra buttons -- see bios/arch/arm/aciaemu.c's own
     * comment, which applies here just as much even with no such driver
     * wired up yet. */
    mousexvec = _dummy_w;

    kbdvecs.midivec = (PFVOID)_dummy_c;
    kbdvecs.vkbderr = (PFVOID)_dummy_c;
    kbdvecs.vmiderr = (PFVOID)_dummy_c;
    kbdvecs.statvec = (PFVOID)_dummy_p;
    kbdvecs.mousevec = (PFVOID)_dummy_p;

    ikbdiorec.buf = ikbdibufbuf;
    ikbdiorec.size = 0x100;
    ikbdiorec.head = 0;
    ikbdiorec.tail = 0;
    ikbdiorec.low = 0x40;
    ikbdiorec.high = 0xC0;

    midiiorec.buf = midiibufbuf;
    midiiorec.size = 0x80;
    midiiorec.head = 0;
    midiiorec.tail = 0;
    midiiorec.low = 0x20;
    midiiorec.high = 0x60;
}
