# Raspberry Pi 4 PCIe Backend Design

## Context

Issue #57 adds the Raspberry Pi 4 PCIe backend for the generic pTOS PCI layer introduced by issue #56. The backend should make the BCM2711 PCIe root complex usable by the shared PCI core so later USB work can discover the Pi 4/400 VL805 xHCI controller through generic PCI APIs.

QEMU's current Raspberry Pi 4 model is not a functional validation target for this work: it disables the `brcm,bcm2711-pcie` device-tree node and does not emulate the host controller or VL805. The implementation must therefore be based on the BCM2711 hardware behavior documented by the Linux `pcie-brcmstb` driver and Raspberry Pi device-tree data, with real-hardware validation left as the deciding test.

## Goals

- Enable `CONF_WITH_PCI` for `TARGET_RPI4` only among Raspberry Pi targets.
- Add a Raspberry Pi 4 BCM2711 PCIe backend behind the existing `pci_backend_t` interface.
- Initialize the BCM2711 PCIe root complex enough for the shared PCI core to enumerate the root complex and the downstream VL805 device.
- Provide config-space read/write callbacks using the Broadcom index/data register mechanism, not generic ECAM.
- Describe the BCM2711 outbound memory window in the generic PCI resource model.
- Provide bus/physical address translation for the supported PCI memory window.
- Return documented PCI errors for unsupported features instead of undefined behavior.

## Non-Goals

- Do not implement PCI interrupt routing in this PR. That is tracked by issue #63.
- Do not implement MSI or MSI-X.
- Do not add xHCI/VL805 driver logic; issue #37 consumes this backend later.
- Do not add QEMU-specific fallbacks for Pi 4 PCIe because QEMU does not emulate this hardware path.
- Do not change the generic PCI core beyond the minimal bridge-bus enumeration needed for devices behind the Raspberry Pi 4 root complex.

## Architecture

The backend should live under the Raspberry Pi machine directory:

```c
bios/machine/raspi/raspi_pci.c
bios/machine/raspi/raspi_pci.h
```

`bios/Kconfig` should allow `CONF_WITH_PCI` when `TARGET_RPI4` is selected and add a hidden backend selector such as `CONF_WITH_PCI_RPI4_BRCMSTB`. `bios/build.mk` should include `raspi_pci.o` only for that backend selector.

`pci_backend_get()` for the Raspberry Pi backend should return the BCM2711 backend when `CONF_WITH_PCI_RPI4_BRCMSTB` is selected. The existing QEMU ARM virt backend remains unchanged and selected only by `CONF_WITH_PCI_VIRT_ECAM`.

All BCM2711 register offsets, bit masks, link-training sequence details, and address-window constants belong in the Raspberry Pi backend file or a Raspberry Pi PCI header. Shared PCI code should continue to see only the generic backend callbacks.

The generic PCI core from issue #56 currently scans bus 0 only. The Raspberry Pi 4 controller presents a PCIe root complex/root port, with VL805 behind the downstream bus. Issue #57 may therefore add minimal shared bridge enumeration: detect PCI-to-PCI bridge class/header type, assign or read secondary/subordinate bus numbers, and scan the secondary bus. This should be kept host-bridge-neutral and should not grow into a full PCI bus allocator beyond what the Pi 4 topology needs.

## Hardware Model

The backend should use the Linux BCM2711 device-tree map as the fixed hardware description for `TARGET_RPI4`:

- PCIe controller registers: bus address `0x7d500000`, CPU physical address `0xfd500000`, size `0x9310`.
- Outbound PCI memory range: PCI bus address `0xf8000000`, CPU physical address `0x600000000`, size `0x04000000`.
- Inbound DMA range: PCI bus address `0x00000000`, CPU physical address `0x00000000`, size `0xc0000000`.

pTOS is currently a 32-bit ARM image. The outbound CPU physical address is above 4 GiB, so the design must either rely on an already-established MMU mapping that makes the window accessible at a 32-bit virtual address, or explicitly add one before the PCI resource helpers are usable. If the current MMU code cannot map this range yet, the backend may still support config-space enumeration while memory resource access must return `PCI_BAD_RESOURCE` until the mapping is implemented.

## Initialization Flow

The initialization should follow the minimal root-complex setup used by the Linux `pcie-brcmstb` driver:

- Power on the relevant USB/PCIe hardware through the existing mailbox power-state mechanism when required.
- Assert bridge reset and, for BCM2711, PERST# before changing controller state.
- Deassert bridge reset, bring SerDes out of IDDQ, and program controller miscellaneous control fields for SCB access, unsupported-request behavior, RCB mode, and BCM2711 burst size.
- Program inbound window 2 for the lower RAM range, respecting the BCM2711 first-3GB limitation.
- Program outbound memory window 0 for the fixed 64 MiB PCI memory aperture.
- Set the root complex class code to PCI-to-PCI bridge (`0x060400`) so generic enumeration sees a proper host bridge.
- Deassert PERST#, wait for link training, and verify link-up before allowing downstream config-space access.
- Log useful status through `KINFO(())`/`KDEBUG(())`, including link-up or link-down.

If initialization fails before config space is safe to access, return a documented PCI error such as `PCI_GENERAL_ERROR` or `PCI_DEVICE_NOT_FOUND` from the backend init path. Config reads to absent devices should return `PCI_BAD_REGISTER_NUMBER` or `PCI_DEVICE_NOT_FOUND` only when the generic PCI core can handle that result safely; otherwise they should read as `0xffffffff` where appropriate so enumeration treats the slot as empty.

## Config-Space Access

Root complex config-space access for bus 0, device 0, function 0 should map directly to the controller's root-complex config registers.

Downstream device access should:

- Refuse access if the link is down.
- Program the Broadcom external config index register with the PCI bus/device/function/register offset.
- Access the external config data register plus the register's offset within the selected word or dword window.
- Support 8-bit, 16-bit, and 32-bit accesses using little-endian conversion, matching the generic PCI core's expectations.
- Reject invalid access sizes and unaligned word/long accesses with `PCI_BAD_REGISTER_NUMBER`.

## Address Translation And Resources

The backend should report one PCI memory window through `get_windows()`:

- `mmio_base = 0xf8000000`
- `mmio_size = 0x04000000`
- `pio_base = 0`
- `pio_size = 0`
- `ecam_base = 0`
- `ecam_size = 0`

The backend should translate PCI bus addresses in the outbound memory range to the CPU-accessible mapping. If the mapped CPU address is not representable or not established in the current pTOS MMU setup, translation should fail with `PCI_BAD_RESOURCE` rather than returning an unusable pointer.

I/O port resources are not expected on the Raspberry Pi 4 host bridge and should return `PCI_BAD_RESOURCE`.

RAM translation helpers should keep the generic identity behavior unless a concrete PCI DMA consumer needs a different mapping. The documented BCM2711 DMA limit is the lower 3 GiB; the design does not add bounce buffering.

## Interrupts

`hook_interrupt()` and `unhook_interrupt()` should return `PCI_FUNC_NOT_SUPPORTED` in issue #57. Interrupt routing is issue #63, covering INTx and any later MSI work.

## Testing

Build validation:

- `make rpi4_defconfig && make`
- `make virt-arm_defconfig && make`

Baseline status before design: `make rpi4_defconfig && make` completed successfully and produced `kernel7l.img`, with existing warnings outside #57.

Hardware validation, when available:

- Boot the image on Raspberry Pi 4 or 400.
- Confirm the log reports PCI initialization and a trained PCIe link.
- Confirm generic PCI enumeration sees the VL805 xHCI controller.
- Confirm BAR resource descriptors expose the VL805 memory BAR with a usable address, or explicitly report `PCI_BAD_RESOURCE` if the high outbound window mapping is not implemented yet.

QEMU validation is not required for Pi 4 PCIe because the emulated board disables the PCIe node and lacks the VL805 path.

## Follow-Up

- Issue #63 tracks Raspberry Pi 4 PCI interrupt routing.
- Issue #37 should consume this backend by finding the VL805 through `pci_find_device()` or `pci_find_classcode()` and using `pci_get_resource()` instead of private Raspberry Pi resource helpers.
