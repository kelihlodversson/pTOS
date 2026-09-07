# RPi4 LPAE PCIe Aperture Design

## Context

Issue #276 originally attempted to move BCM2711's CPU-to-PCIe outbound
window from its hardware address, `0x6_00000000`, to `0xf8000000` so an
ARM32 short-descriptor identity map could access VL805 MMIO. Real Pi 400
testing disproved that assumption. After BAR assignment and endpoint
Memory Space enablement, a capability read through the relocated aperture
returned `0xff`; it did not reach VL805.

The hardware window must remain at physical `0x6_00000000`. This overlaps
issue #76, which owns ARM32 LPAE and high physical-address MMIO support.
The two issues are cross-referenced. This document implements the smallest
RPi4-specific consumer of that shared direction without attempting #76's
separate virt-arm Device Tree, generic ioremap, or high-BAR work.

## Scope

Add the mandatory RPi4 LPAE page-table path that maps:

```text
virtual 0xf8000000..0xfbffffff -> physical 0x6_00000000..0x603ffffff
```

This is a 64 MiB Device mapping for the existing BCM2711 PCIe outbound
window. All kernel pointers, TOS userspace pointers, PCI bus BAR addresses,
and public PCI resource types remain 32-bit.

`raspi_pci_bus_to_phys()` is a historical name: its result is consumed as a
CPU-dereferenceable address by pci_core. In the LPAE configuration it returns
the low virtual aperture address, not the high physical address. This keeps
the current VL805/xHCI path unchanged while avoiding a false physical-pointer
conversion. The eventual #76 API cleanup must distinguish bus, physical, and
virtual addresses explicitly.

## Non-goals

- LPAE support for Pi 1-3 or virt-arm.
- Generic dynamic `ioremap()`/`iounmap()`.
- High PCI bus BAR, ECAM, DMA, or resource-type support.
- Per-process virtual memory.
- Replacing the short-descriptor path in existing configurations.
- Changing the 32-bit virtual layout of pTOS.

## Configuration

`CONF_WITH_ARM_LPAE` is selected automatically for `TARGET_RPI4` and selects
`CONF_WITH_ARM_PMMU`. It is incompatible with the current
short-descriptor-only `CONF_WITH_MMU_TEXT_PROTECT` implementation, which is
therefore unavailable on RPi4. The single RPi4 configuration always uses LPAE.

## Tables And Attributes

Use ARMv7-A long descriptors with a 4 KiB granule:

- one four-entry L1 table;
- four 512-entry L2 tables, one per 1 GiB virtual region;
- 2 MiB L2 block descriptors for the normal low identity map and the high
  PCIe physical aperture.

The tables use the first 20 KiB of a two-megabyte-aligned, two-megabyte
top-of-RAM reservation. Normal RAM is Inner Shareable,
write-back/write-allocate. The reservation is Outer Shareable Normal
Non-Cacheable memory so the page tables and VideoCore mailbox buffer use RAM
transactions without entering the data cache. Everything outside detected
RAM is execute-never Device memory, except the special PCIe aperture, which
is also execute-never Device memory but points at the high physical range.

Set MAIR before enabling translation, use `TTBCR.EAE`, load the 64-bit TTBRs
with `mcrr`, invalidate the unified TLB, and use DSB/ISB around the transition.
Long descriptors do not use the short-descriptor DACR/domain mechanism.

## PCIe Setup

With LPAE enabled, program the outbound window's CPU base and limit back to
the BCM2711 hardware values (`0x6_00000000` through `0x603ffffff`). The
PCI bus side remains `0xf8000000`. The backend returns the mapped virtual
base `0xf8000000` to existing PCI consumers.

Without LPAE, retain the current behavior temporarily so existing builds
continue to compile and boot. The LPAE configuration is the only mode
claimed to make VL805 MMIO usable on real RPi4 hardware.

## Verification

1. Build default `rpi1`, `rpi2`, `rpi4`, and the LPAE RPi4 configuration.
2. Smoke boot the QEMU-supported Pi configurations; QEMU cannot model RPi4.
3. On Pi 400, verify the allocated VL805 BAR, a valid aligned `CAPLENGTH`,
   reset completion, and readable capability registers. No Data Abort may
   occur on an invalid MMIO read.
4. Run the existing regression image on Pi 400 after the xHCI probe.

The Pi 400 hardware test brought up PCIe, allocated and decoded the VL805
BAR, reset and ran the xHCI controller, detected five root-hub ports, and
reached the desktop. Intermittent xHCI Host System Errors during event-ring
setup proved that cache flushes of ordinary BSS were insufficient for VL805's
DMA. The Event Ring Segment Table and its 8 KiB event-ring allocation (the
4 KiB logical segment plus VL805's 4 KiB overfetch guard) now occupy distinct
slots in the non-cacheable coherent reservation. Eight consecutive cold boots
then reached `xhci: controller running` without an HSE, including a six-boot
series after removing temporary 1 ms setup delays. USB enumeration stops at
the expected unimplemented xHCI transfer submission.
