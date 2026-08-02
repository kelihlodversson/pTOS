# Raspberry Pi 4/400 USB xHCI/VL805 Design

## Context

Issue #37 tracks USB support for Raspberry Pi 4 and 400. These boards do not use the DWC2 controller that pTOS currently drives for Raspberry Pi 1/2/3. Their external USB ports are exposed by the VL805 PCIe-to-xHCI controller behind the BCM2711 PCIe root complex.

The current tree has a generic USB layer in `usb/` plus one host-controller driver, `usb/ucd_dwc2.c`. `usb/Kconfig` deliberately excludes `CONF_WITH_USB` on `TARGET_RPI4`, and `usb/usb.c` only initializes DWC2 when `TARGET_RPI4` is not set.

Local QEMU validation is not available for the real Pi 4 USB path. The local QEMU checkout at `/home/freyr/qemu`, commit `9e69071fc292cd735aff3ae1440b6c16e3b61900`, disables the `brcm,bcm2711-pcie` devicetree node in `hw/arm/raspi4b.c` and wires only the shared DWC2 OTG controller in `hw/arm/bcm2838.c`. Therefore an xHCI/VL805 driver must be validated on real Raspberry Pi 4/400 hardware unless QEMU gains BCM2711 PCIe plus VL805/xHCI support.

## Goal

Add a buildable Raspberry Pi 4 USB foundation without changing Raspberry Pi 1/2/3 DWC2 behavior. The first implementation should provide clean Kconfig/build integration, Pi 4-specific PCIe/VL805 discovery scaffolding, and an xHCI host-controller driver shell that fails safely until hardware bring-up is complete.

## Non-Goals

- Do not attempt to emulate VL805 in QEMU as part of this issue.
- Do not add a generic PCI subsystem beyond the Pi 4 needs.
- Do not replace the existing USB enumeration, hub, or HID mouse layers.
- Do not enable unvalidated Pi 4 USB behavior by pretending DWC2 covers the external ports.

## Approach

### Recommended Path: Buildable Foundation First

Add small, reviewable layers in dependency order:

1. Update `usb/Kconfig` so `CONF_WITH_USB` can be enabled for Raspberry Pi 4 when an xHCI controller option is selected.
2. Add `CONF_WITH_USB_XHCI` and wire it into `usb/build.mk` as `ucd_xhci.o`.
3. Replace `#ifndef TARGET_RPI4` in `usb/usb.c` with configuration-driven built-in UCD initialization: DWC2 behind `CONF_WITH_USB_DWC2`, xHCI behind `CONF_WITH_USB_XHCI`.
4. Add a Pi 4-specific PCIe/VL805 discovery layer under the Raspberry Pi machine code, exposed through a narrow interface used by the xHCI driver.
5. Add an xHCI UCD driver skeleton that registers through the existing `struct ucdif` API and returns a clear failure if no usable VL805 controller is discovered.

This keeps the generic USB layer stable while making the Pi 4 path compile and fail predictably until real hardware testing can drive the register-level details.

## Components

### Kconfig and Build Wiring

`CONF_WITH_USB` should depend on `MACHINE_RPI` and at least one supported host-controller option. `CONF_WITH_USB_DWC2` remains the default for Raspberry Pi 1/2/3. `CONF_WITH_USB_XHCI` becomes the Raspberry Pi 4 host-controller option.

`usb/build.mk` adds `obj-$(CONF_WITH_USB_XHCI) += ucd_xhci.o`.

### Built-In Host Controller Initialization

`usb/usb.c` should include built-in host-controller declarations based on config symbols rather than target macros:

- `dwc2_init()` when `CONF_WITH_USB_DWC2` is enabled.
- `xhci_init()` when `CONF_WITH_USB_XHCI` is enabled.

The initialization order remains simple: set up the generic USB API, initialize built-in host controllers, then initialize USB HID mouse support.

### Pi 4 PCIe/VL805 Discovery

Add a Raspberry Pi-specific helper that owns BCM2711 PCIe/VL805 knowledge and keeps it out of generic USB code. Its first interface should be small:

- Initialize or probe the Pi 4 PCIe root complex.
- Locate the VL805 xHCI function.
- Return the xHCI MMIO base, size, and IRQ if available.
- Return failure without touching generic USB state when not available.

This helper belongs behind Raspberry Pi machine configuration. It should not introduce `#ifdef TARGET_RPI4` into generic USB code.

### xHCI UCD Driver

Add `usb/ucd_xhci.c` and a matching private header if needed. The driver should follow the existing `ucd_dwc2.c` contract:

- Define a `struct ucdif` with `open`, `close`, and `ioctl` handlers.
- Implement `LOWLEVEL_INIT`, `LOWLEVEL_STOP`, `SUBMIT_CONTROL_MSG`, `SUBMIT_BULK_MSG`, and `SUBMIT_INT_MSG` entry points.
- Initially, `LOWLEVEL_INIT` may only discover the controller and fail with a useful trace if PCIe/VL805 is unavailable.
- Root hub, transfer rings, event rings, device contexts, and endpoint context handling can be added incrementally after hardware validation starts.

The UCD API boundary lets the existing enumeration and HID device drivers remain unchanged.

## Data Flow

At boot, `bios/bios.c` calls `usb_init()` when `CONF_WITH_USB` is enabled. `usb_init()` initializes generic USB state and registers built-in UCD drivers. On Raspberry Pi 4, `xhci_init()` registers the xHCI UCD. During `ucd_register()`, the UCD opens and runs `LOWLEVEL_INIT`. The xHCI UCD asks the Pi 4 PCIe/VL805 helper for controller resources, maps its registers through existing uncached/MMIO access patterns, and either starts xHCI operation or returns a clean error.

Once the xHCI controller is operational, the existing USB core allocates a root hub device and calls `usb_new_device()`. Later work fills in xHCI control, interrupt, and bulk transfer handling so the existing hub and HID mouse stack can enumerate devices.

## Error Handling

- If PCIe/VL805 is missing, uninitialized, or unsupported, `LOWLEVEL_INIT` returns failure and logs a concise `KINFO` message.
- If xHCI reset or halt/start transitions time out, return failure rather than continuing with partial state.
- If DMA ring allocation or alignment requirements cannot be met, return failure before registering devices.
- Avoid panics for absent hardware because QEMU raspi4b and non-Pi4 builds may expose different USB hardware.

## Testing

Automated validation can cover only build and non-regression behavior:

- `make rpi2_defconfig && make` verifies DWC2 behavior still builds.
- `make rpi4_defconfig && make` verifies Pi 4 USB wiring compiles.
- Any available m68k smoke build should still compile because USB remains Raspberry Pi-scoped.

Hardware validation is required for functional completion:

- Boot on Raspberry Pi 4 or 400 with firmware that initializes VL805.
- Confirm PCIe/VL805 discovery logs the expected controller.
- Confirm xHCI controller reset/start succeeds.
- Confirm root hub enumeration.
- Confirm HID mouse enumeration and reports through the existing USB mouse path.

## Risks

- VL805 firmware and PCIe initialization requirements may differ by bootloader/firmware version.
- DMA coherency may require stricter uncached allocation or cache maintenance than the current DWC2 path.
- xHCI is substantially more complex than DWC2; the first useful milestone should be a cleanly buildable scaffold, not full device support.
- QEMU cannot validate real Pi 4 USB, so hardware logs will be needed before claiming functional support.

## Open Questions

- Which Raspberry Pi 4/400 firmware and bootloader version should be treated as the first supported baseline?
- Is a real Pi 4/400 available for iterative testing during implementation?
- Should keyboard HID support be added alongside mouse once xHCI interrupt transfers work, or should issue #37 remain focused on matching the current mouse-only USB feature set?
