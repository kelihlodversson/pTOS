# Raspberry Pi 4/400 xHCI Controller Bring-Up

## Context

Issue #270 tracks implementing the xHCI controller driver for Raspberry Pi
4/400 USB. `usb/ucd_xhci.c` (from the #37/#56/#57 foundation work) is
currently a 110-line registration shell: `xhci_lowlevel_init()` discovers
the VL805 controller's MMIO BAR and IRQ through
`raspi_vl805_get_resources()` (`bios/machine/raspi/raspi_vl805.c`, built on
the generic PCI layer from #56/#57) and then unconditionally returns
`EOPNOTSUPP`. All three `SUBMIT_*_MSG` ioctls do the same. No register
access, no TRB rings, no device contexts exist anywhere in the tree.

This design covers only the first of three planned stages, agreed with the
project owner:

1. **Bring-up** (this design): reset the controller, initialize its core
   data structures, start it running, and read back root hub port status.
2. Control transfers (`SUBMIT_CONTROL_MSG`) for device enumeration.
3. Async interrupt transfers (`SUBMIT_ASYNC_INT_MSG`/`CANCEL_ASYNC_INT_MSG`,
   the contract HID drivers actually use per the 2026-08-12 async-transfers
   design) for working keyboard/mouse input, which is where #63's
   `pci_hook_interrupt()` becomes a real dependency.

Splitting the work this way mirrors how #56 → #57 → #63 shipped as
separate reviewed PRs rather than one large change, and lets stage 1 be
verified on real Raspberry Pi 4/400 hardware (the project owner has access)
before any transfer logic is built on top of it.

QEMU cannot validate any of this: the upstream `raspi4b` QEMU machine
`fdt_nop_node()`s out the `brcm,bcm2711-pcie` devicetree node entirely (per
the prior #57 design doc), so there is no PCIe, VL805, or xHCI model to
test against in an emulator. Automated testing is limited to compilation;
functional verification requires real hardware.

## Goal

`xhci_lowlevel_init()` resets the VL805 xHCI controller, allocates and
programs its Device Context Base Address Array (DCBAA), Command Ring, and
Event Ring (with its Event Ring Segment Table), starts the controller
running, and reports root hub port count and per-port status
(connect/enable/speed) via `KINFO` tracing. `SUBMIT_CONTROL_MSG`,
`SUBMIT_BULK_MSG`, and `SUBMIT_INT_MSG` remain `EOPNOTSUPP` — unchanged
from the current stub — since no transfer logic is added in this stage.

## Non-Goals

- No transfer logic of any kind (control, bulk, or interrupt) — that is
  stages 2 and 3.
- No interrupt handling. The Event Ring is serviced by direct polling in
  this stage; the interrupter stays disabled (`IMAN`/`IMOD` left at their
  post-reset zero/default state), matching how U-Boot's xHCI driver — a
  real, hardware-validated, fully-polled implementation — operates
  throughout its entire lifetime, not just at bring-up. `pci_hook_interrupt()`
  (#63) is not used by this stage.
- No device slot enable, address device, or configure endpoint commands —
  those exist only to serve stage 2 (enumeration) and are out of scope here.
- No scratchpad support beyond what `HCSPARAMS2`'s Max Scratchpad Buffers
  field requires; if VL805 reports 0, no scratchpad buffers are allocated.
- No changes to `raspi_vl805_get_resources()`'s return type. Stage 3 will
  need to add the `PCI_HANDLE` to `raspi_vl805_resources_t` so
  `pci_hook_interrupt()` can be called; that is out of scope until
  interrupts are actually wired up.

## Approach

Mirror the verified structure of a real, public xHCI reference driver
(U-Boot's `drivers/usb/host/xhci*.c`) rather than working from the xHCI
specification alone, the same way the BCM2711 PCIe INTx routing (#63) was
verified against Linux's actual `bcm2711.dtsi` and `irq-gic.c` instead of
from memory. The sequence below (reset → configure → allocate rings →
start) is the exact call order confirmed in U-Boot's
`xhci_register()`/`xhci_lowlevel_init()`/`xhci_mem_init()`.

DMA-capable memory follows the convention `usb/ucd_dwc2.c` already
establishes in this tree: static, cacheline-aligned buffers via
`usb/usb_io.h`'s `DEFINE_ALIGN_BUFFER` macro, with `flush_data_cache()`/
`invalidate_data_cache()` (`include/biosext.h`) around every
controller-visible write/read. `bios/biosmem.c`'s `balloc_stram()` is not
used — it is a boot-time carve-out for BIOS-early allocation and DWC2
doesn't use it either; static arrays sized at compile time match how this
driver family already works.

## Components

### Register layout (`usb/machine/raspi/xhci_hw.h`, new file)

Plain `portab.h`-typed structs/offsets, no bitfields (matching
`raspi_pci.c`'s register-access style: explicit offset macros plus
`volatile ULONG *`/`UWORD`/`UBYTE` reads and writes):

- **Capability registers** (read-only, at the VL805 BAR base):
  `CAPLENGTH` (byte 0) / `HCIVERSION` (bytes 2-3), `HCSPARAMS1` (0x04),
  `HCSPARAMS2` (0x08), `HCSPARAMS3` (0x0c), `HCCPARAMS1` (0x10),
  `DBOFF` (0x14), `RTSOFF` (0x18).
- **Operational registers** at `base + CAPLENGTH`: `USBCMD` (0x00),
  `USBSTS` (0x04), `PAGESIZE` (0x08), `DNCTRL` (0x14), `CRCR` (0x18,
  64-bit), `DCBAAP` (0x30, 64-bit), `CONFIG` (0x38); per-port register
  sets (`PORTSC`/`PORTPMSC`/`PORTLI`, 16 bytes each) starting at `+0x400`.
- **Runtime registers** at `base + RTSOFF`: `MFINDEX` (0x00), Interrupter
  Register Set 0 at `+0x20`: `IMAN` (0x00), `IMOD` (0x04), `ERSTSZ` (0x08),
  `ERSTBA` (0x10, 64-bit), `ERDP` (0x18, 64-bit).
- **Doorbell array** at `base + DBOFF` (not written by this stage — no
  commands are ever queued yet).

### TRB representation

A 16-byte TRB is 4 `ULONG` fields (`param_lo`, `param_hi`, `status`,
`control`). `control` bit 0 is the Cycle bit; bits 10-15 are the TRB
Type field. Only two TRB shapes are needed in this stage: a generic
zeroed TRB (for the Command Ring's unused slots) and a Link TRB (segment
pointer in `param_lo`/`param_hi`, Cycle bit set, Type = Link = 6) to close
the Command Ring into a loop — the Event Ring does not use a Link TRB,
since HC event-ring segment traversal is driven by the ERST, not by
in-ring links.

### Bring-up sequence (`xhci_lowlevel_init()`)

1. **Reset**: if `USBCMD.RUN` is set, clear it and wait for `USBSTS.HALT`
   to become set; set `USBCMD.RESET`, wait for it to self-clear; wait for
   `USBSTS.CNR` (Controller Not Ready) to clear. Each wait is a bounded
   spin with a timeout — 16 ms for halt-set/halt-clear waits, 250 ms for
   the reset/CNR waits (the exact `XHCI_MAX_HALT_USEC`/`XHCI_MAX_RESET_USEC`
   values from the reference); a timeout is a hard failure, not a retry.
2. Read `HCSPARAMS1`'s Max Device Slots field; write it into
   `CONFIG.MaxSlotsEn`.
3. **DCBAA**: a static, 64-byte-aligned array of `(MaxSlots + 1)` 64-bit
   entries, zeroed. Write its physical address to `DCBAAP`.
4. **Command Ring**: one static, 64-byte-aligned segment of TRBs (64
   entries, matching the reference's `TRBS_PER_SEGMENT`), the last entry a
   Link TRB pointing back to entry 0 with the Cycle bit set. Write `CRCR`
   = segment address | initial cycle state (1).
5. **Event Ring + ERST**: one static segment of TRBs (same size), one
   static one-entry ERST (`{segment address, segment TRB count}`). Write
   `ERDP` = segment address, then `ERSTSZ` = 1, then `ERSTBA` = ERST
   address — this order matters: the spec requires `ERDP`/`ERSTSZ` to be
   valid before `ERSTBA` is written, since writing `ERSTBA` arms the ring.
6. **Scratchpad**: read `HCSPARAMS2`'s Max Scratchpad Buffers field
   (5+5 split bits, per the spec and confirmed in the reference's
   `HCS_MAX_SCRATCHPAD` macro). If nonzero, allocate that many
   page-sized (4 KiB) static buffers plus a static pointer array, write
   each buffer's address into the array, and set `DCBAA[0]` to the
   array's address. If zero, `DCBAA[0]` stays zero.
7. Zero `DNCTRL` (prevents spurious Device Notification Events).
8. **Start**: set `USBCMD.RUN`; wait for `USBSTS.HALT` to clear (bounded
   spin, timeout is a hard failure).
9. Read `HCSPARAMS1`'s Max Ports field; for each port, read `PORTSC` and
   `KINFO`-trace connect/enable/speed. This is the stage's pass signal.

The interrupter is left disabled throughout — no write to `IMAN` beyond
what reset already zeroed.

### `xhci_lowlevel_init()` orchestration

Replaces the current unconditional `EOPNOTSUPP` body. On any step failure
(handshake timeout, resource discovery failure — the existing
`raspi_vl805_get_resources()` check is unchanged), return failure with a
`KINFO` trace, leaving `xhci_priv`'s state such that a later `LOWLEVEL_STOP`
is still safe to call. `SUBMIT_*_MSG` handling in `xhci_ioctl()` is
untouched.

## Data Flow

Unchanged from the existing design doc's data flow up through
`raspi_vl805_get_resources()`. From there: `xhci_lowlevel_init()` maps the
capability/operational/runtime/doorbell register regions from the MMIO
base, runs the sequence above, and returns success once the controller is
running and port status has been read. `ucd_register()` then proceeds to
allocate the root hub `usb_device` and call `usb_new_device()` exactly as
it does for DWC2 today — but since `SUBMIT_CONTROL_MSG` is still
`EOPNOTSUPP`, root hub enumeration itself will fail past this point until
stage 2 lands. That failure is expected and out of scope for this stage;
it is not a regression, since the stub returns `EOPNOTSUPP` even earlier
today.

## Error Handling

- Any register handshake (reset, CNR, halt-set, halt-clear) that does not
  complete within its timeout returns failure and stops bring-up at that
  step; no later step runs with unconfirmed hardware state.
- `raspi_vl805_get_resources()` failure (already handled by the existing
  stub) continues to short-circuit before any register access.
- No panics — absent or misbehaving hardware must fail cleanly, matching
  the existing design doc's policy, since non-RPi4 builds and any future
  QEMU work may exercise this code path against nothing at all.

## Testing

Automated (all that's available without hardware):

- `make rpi4_defconfig && make` — the new bring-up code compiles and links.
- `make rpi2_defconfig && make` and `make virt-arm_defconfig && make` —
  regression: DWC2/virt-arm builds are unaffected (this stage touches only
  `usb/ucd_xhci.c` and the new `usb/machine/raspi/xhci_hw.h`).

Manual, on real Raspberry Pi 4/400 hardware (required — QEMU has no VL805
model):

- Boot log shows VL805 discovery (already works today) followed by
  reset/start succeeding with no timeout `KINFO` traces.
- `KINFO` port trace shows the expected port count for VL805 and, with a
  device plugged into a port, that port's `PORTSC` shows `PORT_CONNECT`
  set with a plausible speed field (full/low/high/super).
- No crash or hang with nothing plugged into any port.

## Risks

- xHCI is complex enough that a subtly wrong register offset or TRB field
  could silently produce a controller that "starts" (`USBSTS.HALT` clears)
  but is not actually functional — the offsets and sequence above were
  cross-checked against U-Boot's real, shipping implementation rather than
  written from the specification alone, but only real-hardware testing in
  this stage's manual checklist can confirm correctness.
- VL805 firmware/PCIe initialization timing may vary by bootloader
  version, as the predecessor design doc already noted; if reset or start
  handshakes intermittently time out, the timeout budget may need
  adjustment based on real-hardware logs.
- Scratchpad buffer handling is easy to get wrong by omission (assuming
  the count is always zero); this design implements it rather than
  skipping it, but it is the least-exercised part of the sequence in
  minimal/embedded xHCI drivers and deserves particular attention during
  hardware testing.

## Open Questions

None — the two open questions from the predecessor design doc (firmware
baseline, hardware availability) are resolved: the project owner has real
Raspberry Pi 4/400 hardware for iterative testing.
