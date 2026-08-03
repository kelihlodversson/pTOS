# Generic PCI BIOS Layer Design

## Context

Issue #56 adds a generic PCI bus layer for pTOS using the Atari PCI BIOS and device driver specification as the functional model. The goal is not binary compatibility with Atari PCI BIOS programs. pTOS should expose native C interfaces that preserve the useful service semantics: discovery, lookup, configuration-space access, BAR/resource reporting, device ownership, interrupt hooks, and address-translation helpers.

This work is a prerequisite for narrower follow-up work:

- Issue #57 adds the Raspberry Pi 4 BCM2711 PCIe backend to the generic PCI layer.
- Issue #37 then uses the generic PCI layer to find the VL805 xHCI controller and consume its BAR resources.
- Issue #59 later audits inherited PCI-related code and moves PCI consumers onto this shared API.

## Goals

- Add a configurable generic PCI subsystem that is disabled for targets without PCI support.
- Provide a native pTOS PCI API modeled on useful Atari PCI BIOS service semantics.
- Represent PCI devices with opaque positive 32-bit handles.
- Enumerate PCI buses/devices/functions and cache discovered metadata.
- Support lookup by vendor/device ID, including vendor ID `0xffff` for iterating all devices.
- Support lookup by class code with Atari PCI BIOS-style mask bits.
- Provide byte, word, and long configuration-space read/write helpers.
- Provide BAR-backed memory and I/O resource descriptors.
- Provide memory and I/O read/write helpers through resource descriptors.
- Provide interrupt hook/unhook entry points that return a documented unsupported error when routing is not implemented.
- Provide card-used state helpers.
- Provide PMMU/address-translation helpers with identity behavior on non-MMU or already identity-mapped targets.
- Add QEMU ARM `virt` as the first real backend using its generic ECAM PCIe host bridge.

## Non-Goals

- Do not implement the Atari PCI BIOS 680x0 register calling convention.
- Do not install or expose the Atari `_PCI` cookie.
- Do not implement the Atari PCI BIOS function-table ABI.
- Do not make pTOS drivers depend on original Atari binary compatibility.
- Do not add the Raspberry Pi 4 backend in issue #56; that belongs to issue #57.
- Do not refactor inherited PCI consumers in issue #56 except where needed to avoid duplicate new abstractions; the full audit belongs to issue #59.

## Architecture

The PCI subsystem has three layers:

1. **Driver-facing API:** Shared headers and C functions used by drivers and platform-independent code. This layer owns handles, error codes, lookup semantics, resource descriptors, and card-used state.
2. **Generic core:** Enumeration and metadata caching. It walks buses/devices/functions through backend config-space callbacks, assigns opaque handles, decodes BARs, and implements API semantics that are independent of a host bridge.
3. **Backend:** Machine-specific host bridge access. The first backend is QEMU ARM `virt`, using the board's generic ECAM host bridge and fixed PCI resource windows.

Shared PCI code must not know Raspberry Pi 4 or QEMU internals beyond backend-provided window descriptors and config-space access functions. Machine-specific backend files may contain raw host bridge addresses, ECAM layout, and interrupt-routing limitations.

## Configuration And Build

Add a `CONF_WITH_PCI` Kconfig option under BIOS or a new PCI-related menu. It should default to enabled only where a backend exists. For the first implementation, that means `MACHINE_VIRT_ARM`.

Add a backend selection symbol such as `CONF_WITH_PCI_VIRT_ECAM`, defaulting to `y` when `CONF_WITH_PCI && MACHINE_VIRT_ARM`.

Build shared PCI core objects only when `CONF_WITH_PCI` is set. Build the QEMU virt backend only when `CONF_WITH_PCI_VIRT_ECAM` is set. Existing non-PCI targets must not compile PCI core or backend code unless explicitly enabled later.

## API Shape

The public API should live in `include/pci.h` or another shared include path used by BIOS and drivers. The implementation should prefer pTOS fixed-width types from `portab.h`.

Use an opaque positive `ULONG` handle type:

```c
typedef ULONG PCI_HANDLE;

#define PCI_HANDLE_NONE 0UL
```

The first pass should expose native functions equivalent to these service groups:

- `pci_init()` scans and caches devices.
- `pci_find_device(UWORD vendor, UWORD device, UWORD index, PCI_HANDLE *handle)` finds by vendor/device. `vendor == 0xffff` iterates all discovered devices.
- `pci_find_classcode(ULONG classcode, ULONG mask, UWORD index, PCI_HANDLE *handle)` finds by class code using a separate mask argument.
- `pci_read_config_byte/word/long()` and `pci_write_config_byte/word/long()` access configuration space through a handle.
- `pci_get_resource(PCI_HANDLE handle, UWORD bar, pci_resource_t *resource)` returns decoded BAR resources.
- `pci_read_io_*`, `pci_write_io_*`, `pci_read_mem_*`, and `pci_write_mem_*` access resource-backed regions where implemented.
- `pci_hook_interrupt()` and `pci_unhook_interrupt()` exist but may return `PCI_FUNC_NOT_SUPPORTED` until routing is implemented.
- `pci_get_card_used()` and `pci_set_card_used()` expose ownership state.
- `pci_bus_to_phys()` and `pci_phys_to_bus()` return identity mappings for the first backend unless a backend supplies translation.

Exact function names can be adjusted to match project conventions during planning, but the groups above are required.

The original Atari PCI BIOS packs class-code ignore bits into the high byte of the class-code register argument. pTOS intentionally uses a separate `mask` parameter in the native C API so callers pass the 24-bit class code and the comparison mask independently. The pTOS mask constants preserve the Atari semantics: `PCI_CLASS_MASK_BASE` ignores base class, `PCI_CLASS_MASK_SUBCLASS` ignores subclass, and `PCI_CLASS_MASK_PROGIF` ignores programming interface.

## Error Codes

Define documented PCI error/status values in the public header. They should include at least:

- `PCI_SUCCESSFUL` for success.
- `PCI_DEVICE_NOT_FOUND` when lookup misses.
- `PCI_BAD_HANDLE` for invalid handles.
- `PCI_BAD_REGISTER_NUMBER` for invalid configuration-space offsets or sizes.
- `PCI_BAD_RESOURCE` for absent or invalid BAR/resource requests.
- `PCI_FUNC_NOT_SUPPORTED` for optional services the backend cannot provide.

Use one signed return type consistently for PCI API functions. The plan should choose exact numeric values that are stable within pTOS and easy to test.

## Device Metadata

Each discovered device record should cache:

- Bus, device, and function numbers.
- Vendor ID and device ID.
- Class code, subclass, and programming interface.
- Header type and multifunction state.
- Decoded BAR resources.
- Interrupt pin and line if available.
- Card-used state.

Enumeration should skip absent functions where vendor ID reads as `0xffff`. It should scan function 0 first and scan functions 1-7 only when function 0 reports a multifunction header.

The first implementation may scan bus 0 only if the backend does not yet expose bridge traversal. If bridge traversal is omitted, document that limitation in the code and design notes. QEMU ARM `virt` validation for the first backend can use devices on bus 0.

## Resources And Access

The generic core should decode PCI BARs by writing all ones, reading back the size mask, restoring the original BAR, and recording implemented resources. It must distinguish I/O BARs from memory BARs and record at least:

- BAR number.
- Resource type: memory or I/O.
- Bus address.
- CPU physical address if translated by the backend.
- Size.
- Flags such as prefetchable and 64-bit memory.

The QEMU ARM `virt` backend should describe its low PCI windows from QEMU's
`virt,highmem=off` machine layout:

- ECAM base `0x3f000000`, size `0x01000000`.
- PCI MMIO window base `0x10000000`, size `0x2eff0000`.
- PCI PIO window base `0x3eff0000`, size `0x00010000`.

The backend should implement config-space access through ECAM. Resource translation should map QEMU's low MMIO and PIO windows according to the fixed virt memory map. High PCI windows are not required for the first implementation; validation must run QEMU with `-M virt,highmem=off` so ECAM is available at the low `0x3f000000` address implemented by this backend.

## Interrupts

The API must expose interrupt hook/unhook entry points because Atari PCI BIOS includes interrupt services and future PCI consumers need a stable contract. The QEMU ARM `virt` backend may initially return `PCI_FUNC_NOT_SUPPORTED` for interrupt hook/unhook if generic PCI IRQ routing is not wired yet.

If basic INTx routing is implemented in the first pass, it must stay in the backend and use the existing `virt_pic` interrupt facilities. Shared PCI code should only see backend callbacks.

## Initialization Flow

BIOS startup should initialize PCI only when `CONF_WITH_PCI` is enabled. The initialization should be safe when no devices are present: it returns success with an empty device list or a documented nonfatal status.

For QEMU ARM `virt`:

1. The backend reports ECAM, MMIO, and PIO windows.
2. The generic core scans bus/device/function config headers through backend config callbacks.
3. The core decodes BARs and stores resource descriptors.
4. Optional debug output reports discovered devices when existing debug logging is enabled.

## Testing And Validation

Build checks:

- `make rpi2_defconfig && make` must continue to build without PCI enabled.
- `make rpi4_defconfig && make` must continue to build without the generic PCI backend until issue #57 enables it for Pi 4.
- `make virt-arm_defconfig && make` must build with PCI enabled.

Runtime checks under QEMU ARM `virt`:

- Boot the `virt-arm` image under QEMU with `-M virt,highmem=off` and at least one PCI device attached.
- Confirm PCI initialization logs at least one discovered device.
- Confirm vendor/device lookup can find a known QEMU device.
- Confirm vendor `0xffff` iteration returns discovered devices in stable order.
- Confirm class-code lookup works with mask bits.
- Confirm invalid handles and invalid config offsets return documented errors.
- Confirm optional interrupt hook/unhook either work or return `PCI_FUNC_NOT_SUPPORTED`.

Unit-style tests are not established for BIOS code in this tree, so validation should be build checks plus QEMU runtime smoke tests and small diagnostic logging or CLI hooks only if they fit existing project patterns. The first PCI backend uses a boot-time self-check in the PCI core that reports failures through existing kernel logging and exercises exact lookup, wildcard lookup, class-code mask lookup, invalid handle/register errors, interrupt hook supported-or-unsupported status, and RAM identity DMA translation without adding a user-facing command.

## Follow-Up Integration

After #56 lands, issue #57 should add the BCM2711/Raspberry Pi 4 PCIe backend behind the same backend interface. Issue #37 should then replace the private `raspi_vl805_get_resources()` style helper with generic PCI lookup/resource calls for the VL805 xHCI controller.

Issue #59 should audit any inherited PCI-related code and ensure drivers and shared subsystems use the generic PCI API instead of private enumeration, config-space, resource, interrupt, or ownership paths.

## Risks

- The Atari PCI BIOS spec includes ABI details that pTOS intentionally does not implement; the implementation must avoid leaking those binary-compatibility assumptions into native APIs.
- ECAM and resource-window assumptions are QEMU ARM `virt` specific and must stay in the backend.
- Interrupt routing may require additional platform work; returning `PCI_FUNC_NOT_SUPPORTED` is acceptable for the first backend if documented and tested.
- BAR sizing changes device configuration registers temporarily; implementation must restore original BAR values before continuing.
- `int` size differs between m68k and ARM, so public API types must use `portab.h` fixed-width types.
