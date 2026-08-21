# BCM2711 Pi 4/Pi 400 Boot Design

Issue: #242

## Goal

Make the `rpi4_defconfig` image boot through MMU setup, initialize its
framebuffer, and run the normal 200 Hz system tick on Raspberry Pi 4 and Pi
400 hardware.  Raspberry Pi 1, 2, and 3 behaviour must not change.

## Scope

The work has three ordered parts:

1. Make pre-C startup safe on BCM2711.
2. Make the ARMv7 MMU handoff preserve the page tables it just populated.
3. Add a BCM2711 GICv2 and generic-timer backend for the normal pTOS tick.

USB, PCIe, Pixel Valve VBL support, and framebuffer-mode changes are outside
this work.  The existing Pi 4 PCIe and xHCI code remains untouched.

## Early Startup

`bios/machine/raspi/startup.S` is shared by Pi 2, Pi 3, and Pi 4, before C
code has initialized `raspi_board`.  It must therefore select the ARM-local
secondary-core mailbox address at assembly time:

- Pi 2 and Pi 3: `0x4000008c`.
- Pi 4 and Pi 400 (BCM2711): `0xff80008c`.

The core 1-3 mailbox offsets remain `+0x10`, `+0x20`, and `+0x30`.
The boot core writes `_start_secondary` to these registers before it enters
the rest of the single-core OS startup.  A data synchronization barrier after
the writes makes their visibility explicit before continuing.

The assembly will emit minimal serial progress markers at the earliest points
where the selected UART is usable: entry, immediately before the MMU call,
and after return.  They are diagnostic-only and must not require the C BSS or
exception vectors.  They give a Pi 4/Pi 400 owner a way to place a future
failure without relying on the framebuffer.

## MMU Handoff

`init_mmu()` needs two distinct cache operations:

- Before constructing the table, invalidate stale firmware cache state.
- After constructing the table and before loading TTBR/enabling SCTLR.M,
  clean the table writes to the point of coherency, then invalidate the TLB
  and execute DSB/ISB barriers.

The existing `clean_data_cache()` name currently performs an invalidate.  The
implementation will not reuse that ambiguous helper for both stages.  It will
introduce or use explicit operations whose names describe their action, so the
page-table handoff cannot discard dirty descriptors on a cache-enabled
firmware entry state.

## BCM2711 Interrupts

BCM2711 routes the ARM generic physical timer through its GICv2, unlike the
Pi 2/Pi 3 local-interrupt path.  Add Pi 4-only GIC setup and dispatch:

- Distributor base: `0xff841000`.
- CPU-interface base: `0xff842000`.
- The non-secure physical generic timer is PPI 30.

Initialize the distributor and CPU interface, set the priority mask, and
disable inherited enabled interrupts.  A Pi 4 `connect_irq()` operation stores
handlers by GIC interrupt ID, assigns an ordinary priority, targets SPIs at
CPU 0, and enables the interrupt.  The ARM IRQ vector continues calling the
Pi-specific dispatcher; that dispatcher reads IAR, ignores spurious ID 1023,
calls the registered handler, and writes the original IAR value to EOIR.

The Pi 4 timer setup reads `CNTFRQ`, computes a 200 Hz period, programs
`CNTP_CVAL`, enables `CNTP_CTL`, and uses an ISB before enabling interrupts.
Each tick polls serial input when configured, calls `vector_5ms()` after it
has been installed, then schedules the next compare value.  This is the same
hardware protocol as `virt-arm`, but the bases, handler storage, and Pi timer
initialization order stay Raspberry Pi-specific.

Pi 1-3 retain their legacy interrupt controller and local-timer setup without
calling GIC code.

## Reuse Boundary

`bios/machine/virt-arm/virt_pic.c` and `virt_timer.c` are the reference for
GICv2 protocol and ARM generic-timer programming.  The initial implementation
keeps Pi and virt drivers separate to avoid a cross-machine abstraction while
hardware bring-up is incomplete.  This issue will not extract an ARM GICv2
helper: that refactoring is deferred until both backends have passed their
respective runtime validation.

## Verification

Build with `gmake rpi4_defconfig && gmake` and keep the Pi 1-3 configurations
buildable.  QEMU cannot validate BCM2711 PCIe or the physical Pi 4 interrupt
path, so real Pi 4/Pi 400 validation is required:

1. The early serial markers reach the post-MMU checkpoint.
2. The firmware rainbow screen is replaced by the pTOS framebuffer.
3. The serial console remains responsive for at least one minute, showing
   that the 200 Hz tick continues.
4. GEM reaches the desktop and accepts timer-driven input without freezing.

The final PR records firmware version, board model, display connection, and
serial output from this test.
