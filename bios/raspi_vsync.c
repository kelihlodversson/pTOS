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
 * historically used by RISC OS on the BCM2835/36/37, and matched here
 * against the same SMI control/status bits the firmware-KMS driver
 * uses to recognise its own vblank event.  When a validated
 * firmware-vblank SMI is actually arriving, this drives int_vbl() from
 * it instead of from the every-4th-200Hz-tick fake in int_timerc()
 * (bios/arch/arm/vectors.c), so VBL timing tracks the real display
 * refresh instead of a fixed 50 Hz.
 *
 * This support is compiled in by default on Pi 1-3 (see
 * CONF_WITH_RASPI_VSYNC_IRQ in bios/Kconfig), but the firmware behaviour
 * it looks for is opt-in, unofficial config.txt behaviour the user adds
 * themselves (see doc/readme-raspi.md).  Nothing about VBL timing
 * changes until a real, validated firmware-vblank SMI is actually
 * observed: with no config.txt (the default) or firmware that doesn't
 * raise it, pTOS keeps faking VBL at 50 Hz exactly as before -- this is
 * a runtime-detected capability, not a boot-time assumption.  Whether
 * it never arrives at all, or it stops arriving after having worked,
 * VBL falls back to (and stays on) the existing timer-driven 50 Hz,
 * driven by raspi_vbl_fallback() below; it re-locks onto real vsync
 * automatically as soon as validated interrupts resume.
 *
 * Not used on Pi 4 (TARGET_RPI4): its interrupts aren't routed through
 * the legacy interrupt controller raspi_connect_irq() drives, and its
 * real vsync source is the Pixel Valve, tracked separately (#206).
 *
 * The SMI control/status register and its firmware-vblank status bits
 * are matched against the historical firmware-KMS behaviour below, but
 * whether current Raspberry Pi firmware still raises ARM_IRQ_SMI at
 * all for fake_vsync_isr=1 has not yet been verified on real Pi 1/2/3
 * hardware.
 */

#include "config.h"
#ifndef MACHINE_RPI
#error This file must only be compiled for raspberry PI targets
#endif

#if CONF_WITH_RASPI_VSYNC_IRQ

#include "portab.h"
#include "raspi_io.h"
#include "raspi_int.h"
#include "raspi_vsync.h"
#include "tosvars.h"
#include "vectors.h"

/*
 * SMI (Secondary Memory Interface) control/status register.  Firmware
 * builds that implement fake_vsync_isr raise ARM_IRQ_SMI with one or
 * more of its DONE/TX/RX interrupt status bits (9-11) set, the same
 * bits the firmware-KMS vblank driver checks; any other state of this
 * register is not a firmware-vblank event.  Writing 0 acknowledges the
 * firmware vblank the same way firmware-KMS does; on real SMI traffic
 * that would also reset/disable other aspects of the interface (the
 * historical source of FKMS's conflicts with other SMI users), but
 * pTOS has no other SMI user to conflict with.
 */
#define SMI_BASE                (ARM_IO_BASE + 0x600000)
#define SMI_CS                  (*(volatile ULONG*)(SMI_BASE + 0x00))
#define SMI_CS_VSYNC_IRQ_MASK   ((1 << 9) | (1 << 10) | (1 << 11))

/*
 * Number of 200 Hz ticks without a fresh, validated SMI-vsync
 * interrupt before reverting to the timer-driven fallback VBL.  Real
 * vsync is expected roughly every 4 ticks (50 Hz) or every 3-4 ticks
 * (60 Hz); a few missed periods is enough margin to not flap on a
 * single dropped interrupt while still falling back quickly if the
 * source is gone.
 */
#define VSYNC_MISS_TICKS (4 * 3)

static volatile ULONG last_vsync_hz200;
static volatile BOOL vsync_healthy;

/*
 * ARM_IRQ_SMI handler, connected via raspi_connect_irq().
 * raspi_connect_irq()/raspi_int_handler() only dispatch to this
 * handler -- they never touch the SMI peripheral itself -- so this is
 * the only place that validates and acknowledges the interrupt source.
 * A dispatched SMI interrupt whose status doesn't carry any of the
 * firmware-vblank bits is not treated as VBL, and is left unacknowledged
 * rather than guessed at.  Only once the vblank bits are seen does this
 * mark real vsync healthy and drive VBL directly; int_timerc()'s
 * every-4th-tick fake (routed through raspi_vbl_fallback() below) then
 * stays quiet as long as validated interrupts keep arriving on time.
 */
static void raspi_vsync_isr(void)
{
    ULONG status = SMI_CS;

    if (!(status & SMI_CS_VSYNC_IRQ_MASK))
        return;

    SMI_CS = 0;         /* acknowledge, the same way firmware-KMS does */

    last_vsync_hz200 = (ULONG)hz_200;
    vsync_healthy = TRUE;
    int_vbl();
}

/*
 * Installed as timer_vbl_hook (bios/arch/arm/vectors.c); called from
 * int_timerc() on every 4th 200 Hz tick, in place of int_vbl().  Acts
 * as a standing watchdog, not just an initial fallback: it fires VBL
 * itself whenever validated real vsync hasn't been seen recently, and
 * re-locks onto real vsync as soon as raspi_vsync_isr() validates
 * another one.  hz_200 is a signed counter that free-runs and wraps;
 * the elapsed-ticks check is done in unsigned arithmetic so it stays
 * correct across that wrap.
 */
static void raspi_vbl_fallback(void)
{
    if (!vsync_healthy || ((ULONG)hz_200 - last_vsync_hz200) > VSYNC_MISS_TICKS)
    {
        vsync_healthy = FALSE;
        int_vbl();
    }
}

/*
 * Called once at startup, with the existing timer-driven VBL already
 * active: installs the fallback hook and enables the SMI IRQ, but
 * changes nothing about VBL timing until (unless) a validated
 * firmware-vblank SMI interrupt actually arrives.  No boot-time
 * detection window or timeout: real vsync is picked up opportunistically
 * whenever the first one shows up, however long that takes.
 */
void raspi_vsync_init(void)
{
    last_vsync_hz200 = (ULONG)hz_200;
    vsync_healthy = FALSE;
    timer_vbl_hook = raspi_vbl_fallback;
    raspi_connect_irq(ARM_IRQ_SMI, raspi_vsync_isr);
}

#endif /* CONF_WITH_RASPI_VSYNC_IRQ */
