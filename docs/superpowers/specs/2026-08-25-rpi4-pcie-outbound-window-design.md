# RPi4 PCIe Outbound/Inbound Window Address Translation

## Context

Issue #276 tracks a defect in `bios/machine/raspi/raspi_pci.c`:
`raspi_pci_bus_to_phys()` unconditionally returns `PCI_BACKEND_UNMAPPABLE`
for every address in the valid MMIO window, which makes
`pci_decode_bar()` (`bios/pci_core.c`) leave every BAR's resource
unpopulated, which makes `pci_get_resource()` always fail. This is not a
regression: the original #57 design doc
(`docs/superpowers/specs/2026-08-03-rpi4-pcie-backend-design.md`)
explicitly recorded the root cause and deliberately deferred it —
"memory resource access must return `PCI_BAD_RESOURCE` until the mapping
is implemented" — because the outbound window's CPU-side physical base is
programmed at `0x6_00000000`, above the 4 GiB boundary a 32-bit ARM
address can express. `raspi_pci_phys_to_bus()` (the reverse-direction
function, used for translating a CPU-side DMA buffer address into the PCI
bus address a device would use to reach it) is also an unconditional
stub, discarding its input entirely.

This surfaced as a concrete blocker for #270 (RPi4 xHCI bring-up):
`raspi_vl805_get_resources()` fails at its `pci_get_resource()` call
before any of that work's bring-up code can run on real hardware.

Confirmed by exploration before this design: the entire 4 GiB 32-bit
address space is already flat-identity-mapped as device memory by
`init_mmu()` (`bios/machine/raspi/memory.c`) — a single ARMv6
short-descriptor L1 section table covering every 1 MB section from `0` to
`0xFFFFFFFF`. This port has no LPAE (2-level page table) infrastructure
at all. Relocating the outbound window's CPU-side base to any address
below 4 GiB therefore requires no new MMU code — the target section is
already present with the right attributes.

Also confirmed: the **inbound** window (PCIe bus → CPU physical, used for
device DMA into system RAM) is already correctly configured today —
`raspi_pci_set_inbound_window()` maps PCIe bus `0x0` for 2 GiB straight
onto CPU physical `0x0`, a 1:1 identity mapping that was never blocked by
the >4 GiB problem. Only the **outbound** direction (CPU → PCIe, used to
reach a device's BAR/register space) is broken. `raspi_pci_phys_to_bus()`
is the smaller half of this fix.

## Goal

`raspi_pci_bus_to_phys()` and `raspi_pci_phys_to_bus()` both return real,
correct translations for addresses inside their respective windows, and
return a clean, distinguishable error for anything outside them. VL805
resource mapping (`raspi_vl805_get_resources()`'s `pci_get_resource()`
call, part of #270) succeeds on real hardware instead of failing with
"PCI BAR0 is not usable yet".

## Non-Goals

- No LPAE / 2-level page table support. The chosen fix reaches every
  address this port needs without it; adding LPAE from scratch for this
  alone would be substantially more invasive than the problem requires.
- No change to the outbound window's *size* (`RASPI_PCIE_MMIO_SIZE`,
  64 MiB) or its PCIe-bus-side target (`RASPI_PCIE_MMIO_BUS_BASE`,
  `0xf8000000`) — only its CPU-side physical base moves.
- No change to the inbound window's configuration
  (`raspi_pci_set_inbound_window()`) — it is already correct.
- No change to PCI config-space access (enumeration, `pci_find_classcode`,
  INTx routing from #63) — those use the separate, always-32-bit-reachable
  ECAM register base (`RASPI_PCIE_REG_BASE`, `0xfd500000`) and are
  unaffected by this issue either way.

## Approach

Relocate the outbound window's CPU-side base to a fixed 32-bit physical
address chosen to sit safely between the fixed BCM2711 PCIe register
block and any plausible amount of detected RAM, verified (not merely
assumed) safe at boot via a dedicated check against the actual detected
RAM size — matching this project's established policy of failing cleanly
rather than proceeding on an unverified assumption, and its established
practice this session of adding a runtime guard wherever a static
constant's correctness depends on something only known at boot.

A fixed constant was chosen over computing the placement dynamically from
detected RAM size: every other address constant in `raspi_pci.c` is a
compile-time `#define`, and introducing a new runtime-computed-constant
pattern for this one case would cost more in consistency than the
robustness it would add — the boot-time check already gives the same
safety guarantee a dynamic placement would, without introducing a new
convention. This was discussed and confirmed with the project owner
before writing this doc.

## Components

### Outbound window CPU-side base

`RASPI_PCIE_REG_BASE` (`0xfd500000`) is the nearest fixed, always-present
obstacle above any candidate placement — it is RPi4-specific and
independent of the board-detected peripheral base (`ARM_IO_BASE`, which
is itself always higher still). Chosen placement:

```
RASPI_PCIE_OUTBOUND_CPU_BASE = 0xf8000000UL
```

This is 1 MB-aligned (required — the outbound window's base/limit fields
are 1 MB-granular per the existing `raspi_pci_set_outbound_window()`
logic) and, at `RASPI_PCIE_MMIO_SIZE` (64 MiB, unchanged) wide, spans
`0xf8000000`–`0xfbffffff`, 21 MiB clear of `RASPI_PCIE_REG_BASE` and 32
MiB clear of the RPi4 peripheral aperture (`0xfe000000`) — more margin
than the minimum needed, adopted during final review as cheap insurance
against any undocumented fixed SoC decode between the top of RAM and
`RASPI_PCIE_REG_BASE`.
This CPU base is numerically identical to `RASPI_PCIE_MMIO_BUS_BASE` by
coincidence, not by requirement.

This replaces `RASPI_PCIE_OUTBOUND_CPU_BASE_LO` (`0`) /
`RASPI_PCIE_OUTBOUND_CPU_BASE_HI` (`0x6`) with a single 32-bit constant;
`raspi_pci_set_outbound_window()`'s register-programming logic
(`PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT`,
`..._BASE_HI`/`..._LIMIT_HI`) is otherwise structurally unchanged — only
the values fed into it change, with the HI fields now `0`.

### Boot-time safety check

Before enabling the outbound window, verify that detected RAM does not
reach into the chosen window: `raspi_top_of_ram <= RASPI_PCIE_OUTBOUND_CPU_BASE`.
`raspi_top_of_ram` is a new `ULONG` global set by `raspi_vcmem_init()`
(`bios/machine/raspi/memory.c`) from the firmware's actual reported
ARM-visible memory (`PROPTAG_GET_ARM_MEMORY`), declared `extern` in
`bios/machine/raspi/raspi_memory.h`.

**This must check `raspi_top_of_ram`, not the pre-existing `phystop`
(`include/tosvars.h`) — an error caught during Copilot's automated PR
review, after the human-reviewed final whole-branch review had already
approved a version of this design that used `phystop`.** `phystop` is
not the top of RAM: `raspi_vcmem_init()` sets it to
`(raspi_top_of_ram - 1 MB)` rounded down to an MB boundary, marking the
*start* of the topmost reserved megabyte where it then places the live
MMU page table (`raspi_page_table0` — the same memory the ARM TTBR
registers point at) and the cache-coherent DMA buffer. A check against
`phystop` alone can be satisfied (`phystop <= RASPI_PCIE_OUTBOUND_CPU_BASE`)
while up to two megabytes of real, live RAM above it — including that
page table — still overlaps the chosen window, silently aliasing
actively-read MMU translation data behind PCIe MMIO. Checking against
`raspi_top_of_ram` (the true upper bound of detected RAM, reservation
included) avoids this entirely.

If the check fails: skip *only* `raspi_pci_set_outbound_window()` and
mark BAR/MMIO resource access unavailable (`raspi_pci_bus_to_phys()`
returns `PCI_BACKEND_UNMAPPABLE` for every address, exactly as it does
unconditionally today) — do not abort PCI init as a whole. Config-space
enumeration, INTx interrupt routing (#63), and the already-correct
inbound window are all independent of the outbound window and must keep
working. Log a clear `KINFO` message explaining why MMIO resource access
is unavailable, so this reads as a diagnosable, expected fallback rather
than a silent or confusing failure.

### `raspi_pci_bus_to_phys()`

Existing range check (`bus_address` inside
`[RASPI_PCIE_MMIO_BUS_BASE, RASPI_PCIE_MMIO_BUS_BASE + RASPI_PCIE_MMIO_SIZE)`)
is unchanged. Replace the unconditional `PCI_BACKEND_UNMAPPABLE` with:

```c
*phys_address = RASPI_PCIE_OUTBOUND_CPU_BASE + (bus_address - RASPI_PCIE_MMIO_BUS_BASE);
return PCI_SUCCESSFUL;
```

If the boot-time check failed and the outbound window was never enabled,
this function must still report failure rather than returning a physical
address that doesn't actually route anywhere — tracked via a static
`BOOL raspi_pci_outbound_window_enabled` set once by the boot-time check
(see the Boot-time safety check subsection above) and read here, rather
than re-deriving the check inside this function on every call.

### `raspi_pci_phys_to_bus()`

Currently discards its input entirely (`(void)phys_address;`) and always
returns `PCI_BAD_RESOURCE`. The inbound window it should be translating
through is already correct and already 1:1 (`RASPI_PCIE_DMA_BUS_BASE = 0`,
`RASPI_PCIE_INBOUND_SIZE = 0x80000000` — 2 GiB), so the real
implementation is a straight range check plus identity passthrough:

```c
/* RASPI_PCIE_DMA_BUS_BASE is 0, so every unsigned phys_address is
 * already >= it -- a lower-bound check would be dead code, which
 * -Wtype-limits correctly flags. Only the upper bound can fail. */
if (phys_address >= RASPI_PCIE_DMA_BUS_BASE + RASPI_PCIE_INBOUND_SIZE)
    return PCI_BAD_RESOURCE;

*bus_address = phys_address;
return PCI_SUCCESSFUL;
```

This has no dependency on the outbound-window fix or its boot-time check
— it works unconditionally, since the inbound window was never broken.

## Data Flow

Unchanged from the #57/#63 design docs except at the two points above.
`pci_decode_bar()` (`bios/pci_core.c`) calls `bus_to_phys()` during BAR
resource decoding, exactly as before — it now receives
`PCI_SUCCESSFUL`/a real physical address instead of always failing, so
`device->resources[bar]` gets populated and `pci_get_resource()` starts
succeeding for the first time. `pci_self_check()`
(`bios/pci_core.c:543`) already exercises `phys_to_bus()` indirectly
through `pci_virt_to_bus()` (`bios/pci_core.c:945`) at boot, so this is
not, strictly, an unexercised code path — but no *production* DMA
consumer calls it yet (a future DMA-buffer-address consumer, likely
part of xHCI's later transfer stages in #270, will be the first one) —
implementing it now is still in scope per #276, since it's the other
stub half of the same address-translation surface, and it costs nothing
to keep correct now rather than leave broken for a future issue to
rediscover.

## Error Handling

- The boot-time RAM-vs-window check is a hard gate: on failure, the
  outbound window is not enabled, and `bus_to_phys()` fails cleanly and
  consistently for every call rather than intermittently or by chance.
- `bus_to_phys()`/`phys_to_bus()`'s existing input validation (null
  output pointer, I/O-space rejection) is unchanged. In `bus_to_phys()`
  specifically, the new `raspi_pci_outbound_window_enabled` check runs
  *before* the pre-existing bus-address range check, not after — a
  disabled window returns `PCI_BACKEND_UNMAPPABLE` immediately, even for
  a `bus_address` that would otherwise be out of range (which would
  return `PCI_BAD_RESOURCE`). The two failure codes are already
  distinguishable by a caller that cares, so this ordering is
  intentional, not an oversight, but it does mean "still applies before
  any new logic runs" (as an earlier draft of this bullet claimed) is
  not quite accurate for this function.
- No panics — matching every prior design doc in this area, absent or
  misconfigured hardware (or, here, an unexpectedly large RAM
  configuration) must fail cleanly, not crash.

## Testing

Automated:

- `make rpi4_defconfig && make` compiles.
- `make rpi2_defconfig && make` and `make virt-arm_defconfig && make`
  (regression) — `raspi_pci.c` only builds for `TARGET_RPI4`, so this is
  a compilation-only regression check, same reasoning as #270's plan.

Manual, on real Raspberry Pi 4/400 hardware (required — QEMU has no
PCIe/VL805 model, same constraint as #270):

- Boot log shows the outbound window's boot-time check passing (no "MMIO
  resource access unavailable" fallback message) on the project owner's
  actual hardware/RAM configuration.
- `raspi_vl805_get_resources()` succeeds — VL805's BAR0 MMIO base/size are
  logged instead of "PCI BAR0 is not usable yet". This is the concrete,
  already-existing signal from #270's `xhci_lowlevel_init()` that this
  fix unblocks it: `xhci: MMIO 0x... size 0x... irq ...` should appear,
  followed by #270's own reset/start/port-trace sequence actually
  running for the first time.
- **This alone is not sufficient evidence the fix is correct** — per the
  Risks section above, `bus_to_phys()` could report success with a
  physical address that doesn't actually reach the device if the
  interconnect-routing assumption is wrong. The decisive check is that
  `xhci_lowlevel_init()`'s subsequent reset sequence actually succeeds
  (no `xhci: timed out waiting for ...` lines) and, ideally, that the
  capability register values it reads (`CAPLENGTH`, `HCIVERSION` via
  `HC_VERSION`) are neither all-zero nor all-ones — either of those would
  mean the CPU access at the translated physical address reached nothing
  real, not an actual xHCI controller.

## Risks

- **The most significant unverified assumption in this design**: it
  relies on the PCIe root complex's outbound ATU registers
  (`PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT`/`BASE_HI`/`LIMIT_HI`) being
  the actual mechanism that determines which CPU physical addresses the
  SoC interconnect routes to the root complex — not merely local
  bookkeeping inside an RC that can only ever be reached at its one true
  hardware placement (`0x6_00000000`). This matches how CPU-side outbound
  ATU windows work on essentially every PCIe root complex architecture
  (DesignWare, Broadcom iProc/STB, Xilinx, and others), which is why this
  design was chosen, but it is not independently confirmed against a
  BCM2711 datasheet or real hardware from this repository alone. If the
  assumption is wrong, the failure mode is silent: `bus_to_phys()` would
  report `PCI_SUCCESSFUL` with a physical address that doesn't actually
  reach the device (the SoC's fixed decode would route the CPU access
  somewhere else — most plausibly back into RAM or an unrelated
  peripheral — instead of to the root complex), which is a worse outcome
  than the honest `PCI_BACKEND_UNMAPPABLE` this design replaces. The
  Testing section's hardware checklist reads back an actual xHCI
  capability register specifically to catch this failure mode loudly
  rather than let it pass silently as a "successful" but wrong resource
  mapping.
- The chosen fixed address (`0xf8000000`) is a well-justified but
  ultimately unverified-until-boot guess about where real RPi4 hardware's
  detected RAM tops out; the boot-time `raspi_top_of_ram` check is the
  actual safety mechanism against RAM overlap specifically, not the
  address choice itself — if the check ever trips on real hardware, that
  is expected, correct, diagnosable behavior, not a bug to work around by
  moving the address further down without re-examining why.
- **A real bug, not just a documentation gap, was found and fixed during
  Copilot's automated PR review**: an earlier version of this design
  (already through the human-reviewed final whole-branch review) checked
  `phystop` instead of `raspi_top_of_ram`. `phystop` is not the top of
  RAM — `raspi_vcmem_init()` sets it to `(raspi_top_of_ram - 1 MB)`
  rounded down to an MB boundary, marking the *start* of the topmost
  reserved megabyte where it places the live MMU page table
  (`raspi_page_table0`, the same memory the ARM TTBR registers point at)
  and the cache-coherent DMA buffer. Checking `phystop` alone could pass
  while up to two megabytes of real RAM above it — including that live
  page table — still overlapped the chosen window, which would have
  silently aliased actively-read MMU translation data behind PCIe MMIO
  on any RPi4 whose detected RAM landed in that range. This is exactly
  the class of subtle, hardware-only-reproducible bug this project's
  review process (task review, final whole-branch review, and automated
  PR review as a further backstop) exists to catch before it reaches
  real hardware, and it demonstrates why: neither the task reviewer nor
  the final whole-branch reviewer caught it, because both reasoned about
  `phystop`'s *name* and its use elsewhere in the codebase
  (`IS_STRAM_POINTER` in `bios/machine.h`) rather than reading its actual
  derivation in `raspi_vcmem_init()`.
- **Two more real defects surfaced in the same Copilot review cycle**,
  both in the new `raspi_top_of_ram` code path: (1) `raspi_prop_get_tags()`'s
  return value was never checked, so a failed or timed-out firmware
  mailbox query would leave `init_tags` as uninitialized stack garbage
  and derive `raspi_top_of_ram` from it; (2) `arm_memory_base +
  arm_memory_size` (both 32-bit) could in principle overflow and wrap
  to a small value. Both are now handled by `panic()` (this file's
  existing convention for "unrecoverable, stop now") rather than a
  sentinel-and-continue approach — an earlier fix attempt used a
  sentinel value that correctly made the PCIe safety check fail closed,
  but that same sentinel still flowed into `phystop`/`init_mmu()`
  moments later, likely placing the live MMU page table outside real
  RAM and crashing far more confusingly than at the actual point of
  failure. Neither failure path is new: the unchecked mailbox call
  predates this branch entirely, and it took making `raspi_top_of_ram`
  load-bearing for a safety guarantee to surface it.
- This is the second RPi4 PCIe-adjacent design in this repository to
  need real-hardware validation with no emulator fallback (the first
  being #270 itself); both should ideally be validated in the same
  hardware session once both land, since #270's own pass signal is the
  most concrete evidence this fix actually works.

## Open Questions

None — the placement approach (fixed address + boot-time check, over
computing it dynamically) was discussed and confirmed with the project
owner before this doc was written.
