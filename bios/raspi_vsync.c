/*
 * raspi_vsync.c - real vsync-driven VBL for the Raspberry Pi (Pi 1-3)
 *
 * Copyright (C) 2013-2026 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * Some Raspberry Pi GPU firmware builds support a legacy, otherwise
 * undocumented config.txt option, fake_vsync_isr=1, that raises
 * ARM_IRQ_SMI synchronized to the real display refresh -- a facility
 * historically used by RISC OS on the BCM2835/36/37.  When it is
 * actually firing, this drives int_vbl() from it instead of from the
 * every-4th-200Hz-tick fake in int_timerc() (bios/arch/arm/vectors.c),
 * so VBL timing tracks the real display refresh instead of a fixed
 * 50 Hz.
 *
 * This is opt-in, unofficial firmware behaviour the user adds to
 * config.txt themselves (see doc/readme-raspi.md); pTOS never depends
 * on it, and boots exactly as before without it.  Whether it never
 * arrives at all -- no config.txt (the default), older or different
 * firmware -- or it stops arriving after having worked, VBL falls
 * back to (and stays on) the existing timer-driven 50 Hz, driven by
 * raspi_vbl_fallback() below.
 *
 * Not used on Pi 4 (TARGET_RPI4): its interrupts aren't routed through
 * the legacy interrupt controller raspi_connect_irq() drives, and its
 * real vsync source is the Pixel Valve, tracked separately (#206).
 *
 * The exact SMI acknowledge sequence current firmware needs -- if any
 * -- is unverified; this has not yet been tested against real Pi 1/2/3
 * hardware, which is why CONF_WITH_RASPI_VSYNC_IRQ defaults to n.
 */

#include "config.h"
#ifndef MACHINE_RPI
#error This file must only be compiled for raspberry PI targets
#endif

#if CONF_WITH_RASPI_VSYNC_IRQ

#include "portab.h"
#include "raspi_int.h"
#include "raspi_vsync.h"
#include "tosvars.h"
#include "vectors.h"

/*
 * Number of 200 Hz ticks without a fresh SMI-vsync interrupt before
 * reverting to the timer-driven fallback VBL.  Real vsync is expected
 * roughly every 4 ticks (50 Hz) or every 3-4 ticks (60 Hz); a few
 * missed periods is enough margin to not flap on a single dropped
 * interrupt while still falling back quickly if the source is gone.
 */
#define VSYNC_MISS_TICKS (4 * 3)

static volatile LONG last_vsync_hz200;
static volatile BOOL vsync_healthy;

/*
 * ARM_IRQ_SMI handler, connected via raspi_connect_irq().  Marks real
 * vsync as healthy and drives VBL directly; int_timerc()'s every-4th-
 * tick fake (routed through raspi_vbl_fallback() below) then stays
 * quiet as long as this keeps arriving on time.
 */
static void raspi_vsync_isr(void)
{
    last_vsync_hz200 = hz_200;
    vsync_healthy = TRUE;
    int_vbl();
}

/*
 * Installed as timer_vbl_hook (bios/arch/arm/vectors.c); called from
 * int_timerc() on every 4th 200 Hz tick, in place of int_vbl().  Acts
 * as a standing watchdog, not just an initial fallback: it fires VBL
 * itself whenever real vsync hasn't been seen recently, and re-locks
 * onto real vsync as soon as raspi_vsync_isr() runs again.
 */
static void raspi_vbl_fallback(void)
{
    if (!vsync_healthy || (hz_200 - last_vsync_hz200) > VSYNC_MISS_TICKS)
    {
        vsync_healthy = FALSE;
        int_vbl();
    }
}

/*
 * Called once at startup, with the existing timer-driven VBL already
 * active: installs the fallback hook and enables the SMI IRQ, but
 * changes nothing about VBL timing until (unless) a real SMI-vsync
 * interrupt actually arrives.
 */
void raspi_vsync_init(void)
{
    last_vsync_hz200 = hz_200;
    vsync_healthy = FALSE;
    timer_vbl_hook = raspi_vbl_fallback;
    raspi_connect_irq(ARM_IRQ_SMI, raspi_vsync_isr);
}

#endif /* CONF_WITH_RASPI_VSYNC_IRQ */
