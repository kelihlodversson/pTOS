# Raspberry Pi 4 PCIe Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Raspberry Pi 4 BCM2711 PCIe backend for the generic pTOS PCI layer so the shared PCI core can discover the downstream VL805 controller.

**Architecture:** Keep BCM2711 host-bridge details in `bios/machine/raspi/raspi_pci.c` behind the existing `pci_backend_t` callbacks. Add the minimum host-neutral bridge-bus enumeration to `bios/pci_core.c` because the VL805 is behind the Raspberry Pi 4 PCIe root complex/root port. Defer PCI interrupt hook-up to issue #63.

**Tech Stack:** C90 with GNU extensions, Kconfig, Kbuild-style `build.mk`, freestanding BIOS code, `portab.h` fixed-width types, `KINFO(())`/`KDEBUG(())`, Raspberry Pi mailbox/property interface, BCM2711 PCIe registers modeled after Linux `pcie-brcmstb`.

## Global Constraints

- Work in `/home/freyr/pTOS.2/.worktrees/feature-57-add-raspberry-pi-4-pcie-backend` on branch `feature/57-add-raspberry-pi-4-pcie-backend`.
- Issue branch and draft PR already exist: PR #62 fixes issue #57.
- Follow C90 style: declarations at the top of blocks; use `/* */` comments in new C code.
- Use `portab.h` types (`WORD`, `LONG`, `UBYTE`, `UWORD`, `ULONG`, `BOOL`) rather than host C integer assumptions.
- Keep all Raspberry Pi 4 PCIe register details in `bios/machine/raspi/raspi_pci.c` or `bios/machine/raspi/raspi_pci.h`.
- Do not implement PCI interrupt routing; `hook_interrupt()` and `unhook_interrupt()` return `PCI_FUNC_NOT_SUPPORTED` for #57.
- Do not add xHCI/VL805 driver logic; issue #37 consumes generic PCI later.
- QEMU raspi4b is not a functional validation target because current QEMU disables `brcm,bcm2711-pcie` and does not emulate VL805.
- The current Raspberry Pi MMU uses 32-bit short descriptors. The BCM2711 outbound PCI memory window at CPU physical `0x600000000` is not directly mappable by the existing MMU code. This PR must enumerate safely; MMIO resource translation must return `PCI_BAD_RESOURCE` unless a 32-bit-accessible mapping is proven during implementation.

---

## File Structure

- Modify `bios/Kconfig`: allow `CONF_WITH_PCI` for `TARGET_RPI4`; add hidden `CONF_WITH_PCI_RPI4_BRCMSTB` selected only for Raspberry Pi 4 PCIe.
- Modify `bios/build.mk`: add `raspi_pci.o` when `CONF_WITH_PCI_RPI4_BRCMSTB` is set.
- Modify `configs/rpi4_defconfig`: enable `CONF_WITH_PCI=y` if the Kconfig default does not save it automatically.
- Modify `include/pci.h`: add PCI bridge config register constants used by shared bridge enumeration.
- Modify `bios/pci_core.c`: add minimal bus/bridge enumeration while preserving virt-arm behavior.
- Create `bios/machine/raspi/raspi_pci.h`: declare `const pci_backend_t *raspi_pci_backend(void);` for the Raspberry Pi backend.
- Create `bios/machine/raspi/raspi_pci.c`: implement BCM2711 backend init, config access, address translation, and unsupported interrupt hooks.
- Modify `docs/superpowers/specs/2026-08-03-rpi4-pcie-backend-design.md`: record any implementation-time constraint discovered by the plan, especially high MMIO access behavior.

---

### Task 1: Wire Raspberry Pi 4 PCI Backend Selection

**Files:**
- Modify: `bios/Kconfig`
- Modify: `bios/build.mk`
- Modify: `configs/rpi4_defconfig` if needed by `make savedefconfig`
- Create: `bios/machine/raspi/raspi_pci.h`
- Create: `bios/machine/raspi/raspi_pci.c`

**Interfaces:**
- Consumes: `pci_backend_t` from `bios/pci_backend.h`.
- Produces: `const pci_backend_t *raspi_pci_backend(void);` for `pci_backend_get()`.

- [ ] **Step 1: Update Kconfig for Raspberry Pi 4 PCI**

Edit `bios/Kconfig` in the `PCI bus support` menu so `CONF_WITH_PCI` can be selected for `TARGET_RPI4` as well as `MACHINE_VIRT_ARM`:

```diff
 config CONF_WITH_PCI
 	bool "PCI bus support"
-	depends on MACHINE_VIRT_ARM
+	depends on MACHINE_VIRT_ARM || TARGET_RPI4
 	default y if MACHINE_VIRT_ARM
+	default y if TARGET_RPI4
```

Add the hidden Raspberry Pi 4 backend selector after `CONF_WITH_PCI_VIRT_ECAM`:

```kconfig
config CONF_WITH_PCI_RPI4_BRCMSTB
	bool
	depends on CONF_WITH_PCI && TARGET_RPI4
	default y
	help
	  Raspberry Pi 4 BCM2711 PCIe host bridge backend.
```

- [ ] **Step 2: Wire the backend object into the BIOS build**

Edit `bios/build.mk` near the existing PCI object lines:

```make
obj-$(CONF_WITH_PCI) += pci_core.o
obj-$(CONF_WITH_PCI_VIRT_ECAM) += virt_pci.o
obj-$(CONF_WITH_PCI_RPI4_BRCMSTB) += raspi_pci.o
```

- [ ] **Step 3: Create the backend header**

Create `bios/machine/raspi/raspi_pci.h`:

```c
/*
 * raspi_pci.h - Raspberry Pi 4 PCIe backend
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef RASPI_PCI_H
#define RASPI_PCI_H

#ifdef MACHINE_RPI

#include "pci_backend.h"

const pci_backend_t *raspi_pci_backend(void);

#endif /* MACHINE_RPI */

#endif /* RASPI_PCI_H */
```

- [ ] **Step 4: Create a compiling backend stub**

Create `bios/machine/raspi/raspi_pci.c` with safe unsupported behavior. This stub must not touch hardware yet.

```c
/*
 * raspi_pci.c - Raspberry Pi 4 BCM2711 PCIe backend
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#if !defined(MACHINE_RPI) || !defined(TARGET_RPI4)
#error This file must only be compiled for Raspberry Pi 4 targets
#endif

#include "portab.h"
#include "pci.h"
#include "pci_backend.h"
#include "raspi_pci.h"

static LONG raspi_pci_init(void)
{
    return PCI_FUNC_NOT_SUPPORTED;
}

static LONG raspi_pci_get_windows(pci_backend_windows_t *windows)
{
    if (windows == 0)
        return PCI_GENERAL_ERROR;
    windows->ecam_base = 0UL;
    windows->ecam_size = 0UL;
    windows->mmio_base = 0UL;
    windows->mmio_size = 0UL;
    windows->pio_base = 0UL;
    windows->pio_size = 0UL;
    return PCI_SUCCESSFUL;
}

static LONG raspi_pci_read_config(UBYTE bus, UBYTE dev, UBYTE func, UWORD reg, UWORD size, ULONG *value)
{
    (void)bus;
    (void)dev;
    (void)func;
    (void)reg;
    (void)size;
    if (value == 0)
        return PCI_GENERAL_ERROR;
    *value = 0xffffffffUL;
    return PCI_FUNC_NOT_SUPPORTED;
}

static LONG raspi_pci_write_config(UBYTE bus, UBYTE dev, UBYTE func, UWORD reg, UWORD size, ULONG value)
{
    (void)bus;
    (void)dev;
    (void)func;
    (void)reg;
    (void)size;
    (void)value;
    return PCI_FUNC_NOT_SUPPORTED;
}

static LONG raspi_pci_bus_to_phys(ULONG bus_address, BOOL io, ULONG *phys_address)
{
    (void)bus_address;
    (void)io;
    if (phys_address == 0)
        return PCI_GENERAL_ERROR;
    return PCI_BAD_RESOURCE;
}

static LONG raspi_pci_phys_to_bus(ULONG phys_address, BOOL io, ULONG *bus_address)
{
    (void)phys_address;
    (void)io;
    if (bus_address == 0)
        return PCI_GENERAL_ERROR;
    return PCI_BAD_RESOURCE;
}

static LONG raspi_pci_hook_interrupt(PCI_HANDLE handle, UBYTE line, pci_interrupt_handler_t handler, void *param)
{
    (void)handle;
    (void)line;
    (void)handler;
    (void)param;
    return PCI_FUNC_NOT_SUPPORTED;
}

static LONG raspi_pci_unhook_interrupt(PCI_HANDLE handle, UBYTE line)
{
    (void)handle;
    (void)line;
    return PCI_FUNC_NOT_SUPPORTED;
}

static pci_backend_t raspi_pci_backend_ops = {
    raspi_pci_init,
    raspi_pci_get_windows,
    raspi_pci_read_config,
    raspi_pci_write_config,
    raspi_pci_bus_to_phys,
    raspi_pci_phys_to_bus,
    raspi_pci_hook_interrupt,
    raspi_pci_unhook_interrupt
};

const pci_backend_t *raspi_pci_backend(void)
{
    return &raspi_pci_backend_ops;
}

const pci_backend_t *pci_backend_get(void)
{
    return raspi_pci_backend();
}
```

- [ ] **Step 5: Build Raspberry Pi 4 with the stub**

Run:

```bash
make rpi4_defconfig && make obj/raspi_pci.o obj/pci_core.o
```

Expected: compile succeeds. `pci_init()` will return `PCI_FUNC_NOT_SUPPORTED` at runtime until Task 4 implements initialization.

- [ ] **Step 6: Ensure non-RPi PCI backend still builds**

Run:

```bash
make virt-arm_defconfig && make obj/virt_pci.o obj/pci_core.o
```

Expected: compile succeeds and does not compile `obj/raspi_pci.o`.

- [ ] **Step 7: Commit Task 1**

Run:

```bash
git add bios/Kconfig bios/build.mk bios/machine/raspi/raspi_pci.h bios/machine/raspi/raspi_pci.c configs/rpi4_defconfig
git commit -m "Wire Raspberry Pi 4 PCIe backend"
```

---

### Task 2: Add Minimal Generic Bridge Bus Enumeration

**Files:**
- Modify: `include/pci.h`
- Modify: `bios/pci_core.c`

**Interfaces:**
- Consumes: existing backend config callbacks.
- Produces: shared PCI scanning that can recurse from a PCI-to-PCI bridge on bus 0 to a downstream bus.

- [ ] **Step 1: Add bridge config register constants**

Edit `include/pci.h` after `PCI_CONFIG_HEADER_TYPE`:

```c
#define PCI_CONFIG_PRIMARY_BUS   0x18U
#define PCI_CONFIG_SECONDARY_BUS 0x19U
#define PCI_CONFIG_SUBORDINATE_BUS 0x1aU
```

- [ ] **Step 2: Add scan bookkeeping to `bios/pci_core.c`**

Add constants and globals near the existing PCI scan constants:

```c
#define PCI_MAX_BUSES 8
#define PCI_CLASS_BRIDGE_PCI 0x060400UL
```

Add static state near `pci_device_count`:

```c
static BOOL pci_bus_scanned[PCI_MAX_BUSES];
static UBYTE pci_next_bus;
```

Add prototypes near the existing scan prototypes:

```c
static pci_device_t *pci_add_function(UBYTE bus, UBYTE dev, UBYTE func);
static void pci_scan_bridge(pci_device_t *device);
static BOOL pci_is_pci_bridge(const pci_device_t *device);
```

Change the existing `static void pci_add_function(...)` prototype to the pointer-returning form above.

- [ ] **Step 3: Make bus scanning idempotent**

Replace `pci_scan_bus()` with:

```c
static void pci_scan_bus(UBYTE bus)
{
    UBYTE dev;

    if (bus >= PCI_MAX_BUSES)
        return;
    if (pci_bus_scanned[bus])
        return;

    pci_bus_scanned[bus] = TRUE;
    for (dev = 0; dev < PCI_DEVICES_PER_BUS; dev++)
        pci_scan_device(bus, dev);
}
```

- [ ] **Step 4: Make `pci_add_function()` return the cached device**

Change `pci_add_function()` to return `pci_device_t *`. On every early failure after selecting a slot, return `0`. At the end, increment `pci_device_count` and return `device`:

```c
    pci_device_count++;
    return device;
```

When the table is full, return `0` after logging.

- [ ] **Step 5: Scan bridge secondary buses from `pci_scan_device()`**

In `pci_scan_device()`, replace the function-add calls with pointer capture:

```c
pci_device_t *device;
```

For function 0:

```c
device = pci_add_function(bus, dev, 0);
if (device != 0)
    pci_scan_bridge(device);
```

For multifunction devices:

```c
device = pci_add_function(bus, dev, func);
if (device != 0)
    pci_scan_bridge(device);
```

- [ ] **Step 6: Add the bridge helpers**

Add after `pci_scan_device()`:

```c
static BOOL pci_is_pci_bridge(const pci_device_t *device)
{
    if (device == 0)
        return FALSE;
    if ((device->header_type & PCI_HEADER_TYPE_MASK) != PCI_HEADER_TYPE_BRIDGE)
        return FALSE;
    return (device->classcode & PCI_CLASS_CODE_MASK) == PCI_CLASS_BRIDGE_PCI;
}

static void pci_scan_bridge(pci_device_t *device)
{
    ULONG value;
    UBYTE secondary;
    UBYTE subordinate;

    if (!pci_is_pci_bridge(device))
        return;

    if (pci_read_config_raw(device, PCI_CONFIG_SECONDARY_BUS, 1, &value) != PCI_SUCCESSFUL)
        return;
    secondary = (UBYTE)value;

    if (pci_read_config_raw(device, PCI_CONFIG_SUBORDINATE_BUS, 1, &value) != PCI_SUCCESSFUL)
        return;
    subordinate = (UBYTE)value;

    if ((secondary == 0U) || (subordinate < secondary)) {
        if (pci_next_bus >= PCI_MAX_BUSES)
            return;
        secondary = pci_next_bus++;
        subordinate = secondary;
        pci_write_config_raw(device, PCI_CONFIG_PRIMARY_BUS, 1, (ULONG)device->bus);
        pci_write_config_raw(device, PCI_CONFIG_SECONDARY_BUS, 1, (ULONG)secondary);
        pci_write_config_raw(device, PCI_CONFIG_SUBORDINATE_BUS, 1, (ULONG)subordinate);
    }

    pci_scan_bus(secondary);
}
```

- [ ] **Step 7: Initialize bridge scan state in `pci_init()`**

In `pci_init()`, after clearing `pci_devices`, add:

```c
memset(pci_bus_scanned, 0, sizeof(pci_bus_scanned));
pci_next_bus = 1U;
```

- [ ] **Step 8: Build and smoke-test virt-arm**

Run:

```bash
make virt-arm_defconfig && make
```

Expected: build succeeds.

If `qemu-system-arm` is available, run:

```bash
timeout 8 qemu-system-arm -M virt,highmem=off -cpu cortex-a7 -m 128 -kernel virt-arm.elf -d guest_errors -display none -serial stdio -device virtio-net-pci
```

Expected: output includes `pci: 3 device(s) found` and no `pci: self-check` failures.

- [ ] **Step 9: Build Raspberry Pi 4**

Run:

```bash
make rpi4_defconfig && make obj/pci_core.o obj/raspi_pci.o
```

Expected: compile succeeds.

- [ ] **Step 10: Commit Task 2**

Run:

```bash
git add include/pci.h bios/pci_core.c
git commit -m "Add minimal PCI bridge bus scanning"
```

---

### Task 3: Implement Raspberry Pi 4 Backend Config Access And Window Translation

**Files:**
- Modify: `bios/machine/raspi/raspi_pci.c`

**Interfaces:**
- Consumes: `pci_backend_t` callbacks from Task 1 and generic bridge scan from Task 2.
- Produces: config-space callbacks that can read root-complex and downstream device config space once initialization marks the link up.

- [ ] **Step 1: Add includes and constants**

Add to `raspi_pci.c`:

```c
#include "endian.h"
#include "kprint.h"
#include "raspi_io.h"
#include "raspi_mbox.h"
```

Add constants near the top of the file:

```c
#define RASPI_PCIE_REG_BASE             0xfd500000UL
#define RASPI_PCIE_REG_SIZE             0x00009310UL

#define RASPI_PCIE_MMIO_BUS_BASE        0xf8000000UL
#define RASPI_PCIE_MMIO_SIZE            0x04000000UL

#define RASPI_PCIE_DMA_BUS_BASE         0x00000000UL
#define RASPI_PCIE_DMA_SIZE             0xc0000000UL

#define PCIE_RC_CFG_PRIV1_ID_VAL3       0x043cUL
#define PCIE_RC_CFG_PRIV1_ID_VAL3_CLASS_CODE_MASK 0x00ffffffUL
#define PCIE_MISC_PCIE_STATUS           0x4068UL
#define PCIE_MISC_PCIE_STATUS_PCIE_DL_ACTIVE_MASK 0x20UL
#define PCIE_MISC_PCIE_STATUS_PCIE_PHYLINKUP_MASK 0x10UL

#define PCIE_EXT_CFG_INDEX              0x9000UL
#define PCIE_EXT_CFG_DATA               0x8000UL

#define PCI_ECAM_REG(reg)               ((reg) & 0xfffU)
#define PCI_ECAM_OFFSET(bus, dev, func, reg) \
    (((ULONG)(bus) << 20) | ((ULONG)(dev) << 15) | ((ULONG)(func) << 12) | PCI_ECAM_REG(reg))
```

Add link state:

```c
static BOOL raspi_pci_link_ready;
```

- [ ] **Step 2: Add register access helpers**

Add:

```c
static volatile UBYTE *raspi_pci_reg_ptr(ULONG offset)
{
    return (volatile UBYTE *)(RASPI_PCIE_REG_BASE + offset);
}

static ULONG raspi_pci_readl(ULONG offset)
{
    return le2cpu32(*(volatile ULONG *)raspi_pci_reg_ptr(offset));
}

static void raspi_pci_writel(ULONG offset, ULONG value)
{
    *(volatile ULONG *)raspi_pci_reg_ptr(offset) = cpu2le32(value);
}

static BOOL raspi_pci_link_up(void)
{
    ULONG status;

    status = raspi_pci_readl(PCIE_MISC_PCIE_STATUS);
    return ((status & PCIE_MISC_PCIE_STATUS_PCIE_DL_ACTIVE_MASK) != 0UL) &&
           ((status & PCIE_MISC_PCIE_STATUS_PCIE_PHYLINKUP_MASK) != 0UL);
}
```

- [ ] **Step 3: Replace `get_windows()` with the fixed Pi 4 windows**

Use:

```c
static LONG raspi_pci_get_windows(pci_backend_windows_t *windows)
{
    if (windows == 0)
        return PCI_GENERAL_ERROR;

    windows->ecam_base = 0UL;
    windows->ecam_size = 0UL;
    windows->mmio_base = RASPI_PCIE_MMIO_BUS_BASE;
    windows->mmio_size = RASPI_PCIE_MMIO_SIZE;
    windows->pio_base = 0UL;
    windows->pio_size = 0UL;
    return PCI_SUCCESSFUL;
}
```

- [ ] **Step 4: Add config pointer selection**

Add:

```c
static volatile UBYTE *raspi_pci_config_ptr(UBYTE bus, UBYTE dev, UBYTE func, UWORD reg)
{
    ULONG index;

    if (bus == 0U) {
        if ((dev != 0U) || (func != 0U))
            return 0;
        return raspi_pci_reg_ptr(PCI_ECAM_REG(reg));
    }

    if (!raspi_pci_link_ready)
        return 0;

    index = PCI_ECAM_OFFSET(bus, dev, func, 0U);
    raspi_pci_writel(PCIE_EXT_CFG_INDEX, index);
    return raspi_pci_reg_ptr(PCIE_EXT_CFG_DATA + PCI_ECAM_REG(reg));
}
```

- [ ] **Step 5: Replace config read/write stubs**

Implement reads:

```c
static LONG raspi_pci_read_config(UBYTE bus, UBYTE dev, UBYTE func, UWORD reg, UWORD size, ULONG *value)
{
    volatile UBYTE *ptr;

    if (value == 0)
        return PCI_GENERAL_ERROR;
    if (((size == 2U) && ((reg & 1U) != 0U)) ||
        ((size == 4U) && ((reg & 3U) != 0U)) ||
        ((size != 1U) && (size != 2U) && (size != 4U)))
        return PCI_BAD_REGISTER_NUMBER;

    ptr = raspi_pci_config_ptr(bus, dev, func, reg);
    if (ptr == 0) {
        *value = 0xffffffffUL;
        return PCI_SUCCESSFUL;
    }

    if (size == 1U)
        *value = (ULONG)*ptr;
    else if (size == 2U)
        *value = (ULONG)le2cpu16(*(volatile UWORD *)ptr);
    else
        *value = le2cpu32(*(volatile ULONG *)ptr);
    return PCI_SUCCESSFUL;
}
```

Implement writes:

```c
static LONG raspi_pci_write_config(UBYTE bus, UBYTE dev, UBYTE func, UWORD reg, UWORD size, ULONG value)
{
    volatile UBYTE *ptr;

    if (((size == 2U) && ((reg & 1U) != 0U)) ||
        ((size == 4U) && ((reg & 3U) != 0U)) ||
        ((size != 1U) && (size != 2U) && (size != 4U)))
        return PCI_BAD_REGISTER_NUMBER;

    ptr = raspi_pci_config_ptr(bus, dev, func, reg);
    if (ptr == 0)
        return PCI_SUCCESSFUL;

    if (size == 1U)
        *ptr = (UBYTE)value;
    else if (size == 2U)
        *(volatile UWORD *)ptr = cpu2le16((UWORD)value);
    else
        *(volatile ULONG *)ptr = cpu2le32(value);
    return PCI_SUCCESSFUL;
}
```

- [ ] **Step 6: Implement safe translation behavior**

Replace `bus_to_phys()` and `phys_to_bus()` with conservative behavior:

```c
static LONG raspi_pci_bus_to_phys(ULONG bus_address, BOOL io, ULONG *phys_address)
{
    if (phys_address == 0)
        return PCI_GENERAL_ERROR;
    if (io)
        return PCI_BAD_RESOURCE;
    if ((bus_address < RASPI_PCIE_MMIO_BUS_BASE) ||
        (bus_address >= RASPI_PCIE_MMIO_BUS_BASE + RASPI_PCIE_MMIO_SIZE))
        return PCI_BAD_RESOURCE;

    return PCI_BAD_RESOURCE;
}

static LONG raspi_pci_phys_to_bus(ULONG phys_address, BOOL io, ULONG *bus_address)
{
    (void)phys_address;
    if (bus_address == 0)
        return PCI_GENERAL_ERROR;
    if (io)
        return PCI_BAD_RESOURCE;
    return PCI_BAD_RESOURCE;
}
```

This intentionally prevents the generic PCI core from exposing unusable BAR pointers until high MMIO mapping support exists.

- [ ] **Step 7: Keep init unsupported until Task 4**

Leave `raspi_pci_init()` returning `PCI_FUNC_NOT_SUPPORTED` in this task. That makes config helpers compile without touching hardware during boot.

- [ ] **Step 8: Build both PCI targets**

Run:

```bash
make rpi4_defconfig && make obj/raspi_pci.o obj/pci_core.o
make virt-arm_defconfig && make obj/virt_pci.o obj/pci_core.o
```

Expected: both commands succeed.

- [ ] **Step 9: Commit Task 3**

Run:

```bash
git add bios/machine/raspi/raspi_pci.c
git commit -m "Add Raspberry Pi PCIe config access helpers"
```

---

### Task 4: Implement BCM2711 PCIe Initialization

**Files:**
- Modify: `bios/machine/raspi/raspi_pci.c`
- Modify: `docs/superpowers/specs/2026-08-03-rpi4-pcie-backend-design.md` if implementation confirms a narrower behavior than the spec permits

**Interfaces:**
- Consumes: config access helpers from Task 3.
- Produces: `raspi_pci_init()` that powers/configures the host bridge and sets `raspi_pci_link_ready`.

- [ ] **Step 1: Add remaining BCM2711 setup constants**

Add constants near the existing register definitions:

```c
#define PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1 0x0188UL
#define PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1_ENDIAN_MODE_BAR2_MASK 0x0000000cUL
#define PCIE_RC_CFG_VENDOR_SPECIFIC_REG1_LITTLE_ENDIAN 0x00000000UL

#define PCIE_MISC_MISC_CTRL             0x4008UL
#define PCIE_MISC_MISC_CTRL_PCIE_RCB_64B_MODE_MASK 0x00000080UL
#define PCIE_MISC_MISC_CTRL_PCIE_RCB_MPS_MODE_MASK 0x00000400UL
#define PCIE_MISC_MISC_CTRL_SCB_ACCESS_EN_MASK 0x00001000UL
#define PCIE_MISC_MISC_CTRL_CFG_READ_UR_MODE_MASK 0x00002000UL
#define PCIE_MISC_MISC_CTRL_MAX_BURST_SIZE_MASK 0x00300000UL
#define PCIE_MISC_MISC_CTRL_SCB0_SIZE_MASK 0xf8000000UL

#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LO 0x400cUL
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_HI 0x4010UL
#define PCIE_MISC_RC_BAR1_CONFIG_LO     0x402cUL
#define PCIE_MISC_PCIE_CTRL             0x4064UL
#define PCIE_MISC_PCIE_CTRL_PCIE_PERSTB_MASK 0x00000004UL
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT 0x4070UL
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT_LIMIT_MASK 0xfff00000UL
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT_BASE_MASK 0x0000fff0UL
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI 0x4080UL
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI 0x4084UL
#define PCIE_MISC_HARD_PCIE_HARD_DEBUG  0x4204UL
#define PCIE_MISC_HARD_PCIE_HARD_DEBUG_SERDES_IDDQ_MASK 0x08000000UL
#define PCIE_RGR1_SW_INIT_1             0x9210UL
#define PCIE_RGR1_SW_INIT_1_PERST_MASK  0x00000001UL
#define RGR1_SW_INIT_1_INIT_GENERIC_MASK 0x00000002UL

#define RASPI_PCIE_OUTBOUND_CPU_BASE_LO 0x00000000UL
#define RASPI_PCIE_OUTBOUND_CPU_BASE_HI 0x00000006UL
#define RASPI_PCIE_INBOUND_SIZE         0x80000000UL
#define RASPI_PCIE_INBOUND_SIZE_CODE    16U
#define RASPI_PCIE_LINK_WAIT_LOOPS      20U
```

Use `0x80000000` for the first inbound window size because it is a power-of-two safe lower-RAM viewport and avoids claiming DMA coverage above 2 GiB in this first backend. The spec's 3 GiB hardware limit remains documented; the first implementation does not add bounce buffering or split inbound windows.

- [ ] **Step 2: Add bitfield helper functions**

Add:

```c
static ULONG raspi_pci_field_shift(ULONG mask)
{
    ULONG shift;

    shift = 0UL;
    while (((mask >> shift) & 1UL) == 0UL)
        shift++;
    return shift;
}

static ULONG raspi_pci_replace_bits(ULONG original, ULONG value, ULONG mask)
{
    ULONG shift;

    shift = raspi_pci_field_shift(mask);
    original &= ~mask;
    original |= (value << shift) & mask;
    return original;
}

static void raspi_pci_update_bits(ULONG offset, ULONG mask, ULONG value)
{
    ULONG reg;

    reg = raspi_pci_readl(offset);
    reg &= ~mask;
    reg |= value & mask;
    raspi_pci_writel(offset, reg);
}
```

- [ ] **Step 3: Add reset helpers**

Add:

```c
static void raspi_pci_set_bridge_reset(BOOL asserted)
{
    ULONG value;

    value = asserted ? RGR1_SW_INIT_1_INIT_GENERIC_MASK : 0UL;
    raspi_pci_update_bits(PCIE_RGR1_SW_INIT_1, RGR1_SW_INIT_1_INIT_GENERIC_MASK, value);
}

static void raspi_pci_set_perst(BOOL asserted)
{
    ULONG value;

    value = asserted ? PCIE_RGR1_SW_INIT_1_PERST_MASK : 0UL;
    raspi_pci_update_bits(PCIE_RGR1_SW_INIT_1, PCIE_RGR1_SW_INIT_1_PERST_MASK, value);
}
```

- [ ] **Step 4: Add outbound window helper**

Add:

```c
static void raspi_pci_set_outbound_window(void)
{
    ULONG cpu_mb;
    ULONG limit_mb;
    ULONG reg;

    raspi_pci_writel(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LO, RASPI_PCIE_MMIO_BUS_BASE);
    raspi_pci_writel(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_HI, 0UL);

    cpu_mb = RASPI_PCIE_OUTBOUND_CPU_BASE_LO >> 20;
    limit_mb = (RASPI_PCIE_OUTBOUND_CPU_BASE_LO + RASPI_PCIE_MMIO_SIZE - 1UL) >> 20;

    reg = raspi_pci_readl(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT);
    reg = raspi_pci_replace_bits(reg, cpu_mb, PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT_BASE_MASK);
    reg = raspi_pci_replace_bits(reg, limit_mb, PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT_LIMIT_MASK);
    raspi_pci_writel(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT, reg);

    raspi_pci_writel(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI, RASPI_PCIE_OUTBOUND_CPU_BASE_HI);
    raspi_pci_writel(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI, RASPI_PCIE_OUTBOUND_CPU_BASE_HI);
}
```

- [ ] **Step 5: Add inbound window helper**

Add:

```c
static void raspi_pci_set_inbound_window(void)
{
    ULONG reg;

    reg = RASPI_PCIE_DMA_BUS_BASE;
    reg |= RASPI_PCIE_INBOUND_SIZE_CODE;
    raspi_pci_writel(PCIE_MISC_RC_BAR1_CONFIG_LO + 8UL, reg);
    raspi_pci_writel(PCIE_MISC_RC_BAR1_CONFIG_LO + 12UL, 0UL);
}
```

- [ ] **Step 6: Add class-code setup helper**

Add:

```c
static void raspi_pci_set_root_bridge_class(void)
{
    ULONG reg;

    reg = raspi_pci_readl(PCIE_RC_CFG_PRIV1_ID_VAL3);
    reg &= ~PCIE_RC_CFG_PRIV1_ID_VAL3_CLASS_CODE_MASK;
    reg |= 0x00060400UL;
    raspi_pci_writel(PCIE_RC_CFG_PRIV1_ID_VAL3, reg);
}
```

- [ ] **Step 7: Replace `raspi_pci_init()` with hardware initialization**

Use:

```c
static LONG raspi_pci_init(void)
{
    prop_tag_2u32_t power_state;
    ULONG reg;
    UWORD i;

    raspi_pci_link_ready = FALSE;

    power_state.value1 = DEVICE_ID_USB_HCD;
    power_state.value2 = POWER_STATE_ON | POWER_STATE_WAIT;
    if (!raspi_prop_get_tag(PROPTAG_SET_POWER_STATE, &power_state, sizeof power_state, sizeof(ULONG) * 2))
        KDEBUG(("pci: RPi4 USB power-state request failed\n"));

    raspi_pci_set_bridge_reset(TRUE);
    raspi_pci_set_perst(TRUE);
    raspi_delay_us(200UL);

    raspi_pci_set_bridge_reset(FALSE);

    reg = raspi_pci_readl(PCIE_MISC_HARD_PCIE_HARD_DEBUG);
    reg &= ~PCIE_MISC_HARD_PCIE_HARD_DEBUG_SERDES_IDDQ_MASK;
    raspi_pci_writel(PCIE_MISC_HARD_PCIE_HARD_DEBUG, reg);
    raspi_delay_us(200UL);

    reg = raspi_pci_readl(PCIE_MISC_MISC_CTRL);
    reg = raspi_pci_replace_bits(reg, 1UL, PCIE_MISC_MISC_CTRL_SCB_ACCESS_EN_MASK);
    reg = raspi_pci_replace_bits(reg, 1UL, PCIE_MISC_MISC_CTRL_CFG_READ_UR_MODE_MASK);
    reg = raspi_pci_replace_bits(reg, 0UL, PCIE_MISC_MISC_CTRL_MAX_BURST_SIZE_MASK);
    reg = raspi_pci_replace_bits(reg, 1UL, PCIE_MISC_MISC_CTRL_PCIE_RCB_MPS_MODE_MASK);
    reg = raspi_pci_replace_bits(reg, 1UL, PCIE_MISC_MISC_CTRL_PCIE_RCB_64B_MODE_MASK);
    reg = raspi_pci_replace_bits(reg, RASPI_PCIE_INBOUND_SIZE_CODE, PCIE_MISC_MISC_CTRL_SCB0_SIZE_MASK);
    raspi_pci_writel(PCIE_MISC_MISC_CTRL, reg);

    raspi_pci_set_inbound_window();
    raspi_pci_set_outbound_window();
    raspi_pci_set_root_bridge_class();

    reg = raspi_pci_readl(PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1);
    reg &= ~PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1_ENDIAN_MODE_BAR2_MASK;
    reg |= PCIE_RC_CFG_VENDOR_SPECIFIC_REG1_LITTLE_ENDIAN;
    raspi_pci_writel(PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1, reg);

    raspi_pci_set_perst(FALSE);
    for (i = 0; i < RASPI_PCIE_LINK_WAIT_LOOPS; i++) {
        raspi_delay_us(5000UL);
        if (raspi_pci_link_up()) {
            raspi_pci_link_ready = TRUE;
            KINFO(("pci: RPi4 PCIe link up\n"));
            return PCI_SUCCESSFUL;
        }
    }

    KINFO(("pci: RPi4 PCIe link down\n"));
    return PCI_DEVICE_NOT_FOUND;
}
```

- [ ] **Step 8: Build Raspberry Pi 4 objects**

Run:

```bash
make rpi4_defconfig && make obj/raspi_pci.o obj/pci_core.o
```

Expected: compile succeeds.

- [ ] **Step 9: Build full Raspberry Pi 4 image**

Run:

```bash
make rpi4_defconfig && make
```

Expected: build succeeds and produces `kernel7l.img`.

- [ ] **Step 10: Commit Task 4**

Run:

```bash
git add bios/machine/raspi/raspi_pci.c docs/superpowers/specs/2026-08-03-rpi4-pcie-backend-design.md
git commit -m "Initialize Raspberry Pi 4 PCIe host bridge"
```

---

### Task 5: Final Validation And PR Documentation

**Files:**
- Modify: `docs/superpowers/specs/2026-08-03-rpi4-pcie-backend-design.md` if validation changes documented behavior
- Modify: PR #62 description using `gh pr edit`

**Interfaces:**
- Consumes: completed backend and bridge enumeration.
- Produces: verified branch with documented hardware limitations.

- [ ] **Step 1: Run full target builds**

Run:

```bash
make rpi4_defconfig && make
make virt-arm_defconfig && make
```

Expected: both builds succeed.

- [ ] **Step 2: Run virt-arm PCI smoke test**

Run:

```bash
timeout 8 qemu-system-arm -M virt,highmem=off -cpu cortex-a7 -m 128 -kernel virt-arm.elf -d guest_errors -display none -serial stdio -device virtio-net-pci
```

Expected: output includes `pci: 3 device(s) found` and does not include `pci: self-check` failures.

- [ ] **Step 3: Run whitespace check**

Run:

```bash
git diff --check
```

Expected: no output.

- [ ] **Step 4: Record hardware test expectations**

If real Raspberry Pi 4/400 hardware is not available, add this note to the PR description:

```markdown
Hardware validation still needed on Raspberry Pi 4/400:

- Boot `kernel7l.img`.
- Confirm `pci: RPi4 PCIe link up` appears.
- Confirm generic PCI enumeration sees the VL805 controller.
- Confirm BAR resources are absent with `PCI_BAD_RESOURCE` until high PCI MMIO mapping support exists, or report the usable BAR address if a platform mapping is later added.
```

- [ ] **Step 5: Update PR description**

Run:

```bash
gh pr edit 62 --body "$(cat <<'EOF'
Fixes #57.

## Summary

- Adds Raspberry Pi 4 BCM2711 PCIe backend wiring for the generic PCI layer.
- Adds minimal generic bridge-bus scanning so devices behind the Pi 4 root complex can be discovered.
- Initializes the Pi 4 PCIe host bridge enough for config-space enumeration; interrupt routing remains #63.

## Validation

- `make rpi4_defconfig && make`
- `make virt-arm_defconfig && make`
- `timeout 8 qemu-system-arm -M virt,highmem=off -cpu cortex-a7 -m 128 -kernel virt-arm.elf -d guest_errors -display none -serial stdio -device virtio-net-pci`
- `git diff --check`

## Hardware Note

QEMU raspi4b does not emulate the BCM2711 PCIe/VL805 path. Real Raspberry Pi 4/400 validation is still required for link training and VL805 enumeration.

## Follow-Up

- #63 tracks Raspberry Pi 4 PCI interrupt routing.
- #37 will consume this backend for VL805/xHCI discovery.
EOF
)"
```

- [ ] **Step 6: Commit validation documentation if files changed**

If Step 4 changed tracked files, run:

```bash
git add docs/superpowers/specs/2026-08-03-rpi4-pcie-backend-design.md
git commit -m "Document Raspberry Pi PCIe validation limits"
```

If no tracked files changed, do not create an empty commit.

- [ ] **Step 7: Push the branch**

Run:

```bash
git status --short
git push
```

Expected: worktree is clean except generated build outputs ignored by `.gitignore`, and the remote branch updates.
