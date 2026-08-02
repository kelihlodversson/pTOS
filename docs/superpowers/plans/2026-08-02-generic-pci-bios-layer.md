# Generic PCI BIOS Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a native pTOS PCI layer modeled on Atari PCI BIOS service semantics, with QEMU ARM `virt` ECAM as the first buildable and testable backend.

**Architecture:** Split PCI into a driver-facing API in `include/pci.h`, a shared BIOS core that owns handles/enumeration/resources, and machine backends that provide config-space and address-translation callbacks. The first backend is QEMU ARM `virt`; Raspberry Pi 4 PCIe support remains a follow-up backend in issue #57.

**Tech Stack:** C90 with GNU extensions, Kconfig, Kbuild-style `build.mk`, freestanding BIOS code, `portab.h` fixed-width types, `KINFO(())`/`KDEBUG(())` tracing, QEMU ARM `virt` generic ECAM PCIe host bridge.

## Global Constraints

- Add a configurable generic PCI subsystem that is disabled for targets without PCI support.
- Provide a native pTOS PCI API modeled on useful Atari PCI BIOS service semantics.
- Represent PCI devices with opaque positive 32-bit handles.
- Support lookup by vendor/device ID, including vendor ID `0xffff` for iterating all devices.
- Support lookup by class code with Atari PCI BIOS-style mask bits.
- Provide byte, word, and long configuration-space read/write helpers.
- Provide BAR-backed memory and I/O resource descriptors.
- Provide memory and I/O read/write helpers through resource descriptors.
- Provide interrupt hook/unhook entry points that return a documented unsupported error when routing is not implemented.
- Provide card-used state helpers.
- Provide PMMU/address-translation helpers with identity behavior on non-MMU or already identity-mapped targets.
- Add QEMU ARM `virt` as the first real backend using its generic ECAM PCIe host bridge.
- Do not implement the Atari PCI BIOS 680x0 register calling convention.
- Do not install or expose the Atari `_PCI` cookie.
- Do not implement the Atari PCI BIOS function-table ABI.
- Do not add the Raspberry Pi 4 backend in issue #56; that belongs to issue #57.
- Do not refactor inherited PCI consumers in issue #56 except where needed to avoid duplicate new abstractions; the full audit belongs to issue #59.
- C code must remain C90-compatible: declarations at the top of blocks, `/* */` comments, pTOS fixed-width types where relevant.
- Do not touch the existing untracked `package-lock.json`.

---

## File Structure

- Create `include/pci.h`: public native pTOS PCI API, error codes, handles, resource descriptors, and PCI register constants needed by drivers.
- Create `bios/pci_backend.h`: internal backend interface used by the shared PCI core; not included by drivers.
- Create `bios/pci_core.c`: shared PCI implementation for init, enumeration, lookup, config access, BAR decoding, resources, memory/I/O access helpers, card-used state, interrupts, and address translation.
- Create `bios/machine/virt-arm/virt_pci.c`: QEMU ARM `virt` ECAM backend with fixed ECAM/MMIO/PIO windows.
- Create `bios/machine/virt-arm/virt_pci.h`: QEMU ARM `virt` backend declaration for the shared core.
- Modify `bios/machine/virt-arm/virt_memmap.h`: add PCI ECAM/MMIO/PIO constants from QEMU `hw/arm/virt.c`.
- Modify `bios/Kconfig`: add `CONF_WITH_PCI` and `CONF_WITH_PCI_VIRT_ECAM`.
- Modify `bios/build.mk`: build shared PCI and virt backend objects only when selected.
- Modify `bios/bios.c`: call `pci_init()` during BIOS initialization when `CONF_WITH_PCI` is enabled.
- Modify `configs/virt-arm_defconfig` only if config generation does not preserve the intended default.
- Modify `docs/superpowers/specs/2026-08-02-generic-pci-bios-design.md` only if implementation reveals a correction to the approved design.

---

### Task 1: Public API, Configuration, And Init Hook

**Files:**
- Create: `include/pci.h`
- Create: `bios/pci_core.c` temporary build stub
- Create: `bios/machine/virt-arm/virt_pci.c` temporary build stub
- Modify: `bios/Kconfig`
- Modify: `bios/build.mk`
- Modify: `bios/bios.c`

**Interfaces:**
- Consumes: `portab.h`, `config.h`, existing BIOS initialization flow in `bios/bios.c`.
- Produces: public `PCI_HANDLE`, `pci_resource_t`, `pci_mem_t`, PCI status codes, API function prototypes, and a `CONF_WITH_PCI`-guarded `pci_init()` call.

- [ ] **Step 1: Create the public PCI header**

Create `include/pci.h` with this exact public interface:

```c
/*
 * pci.h - native pTOS PCI access layer
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef PCI_H
#define PCI_H

#include "portab.h"

typedef ULONG PCI_HANDLE;

#define PCI_HANDLE_NONE              0UL

#define PCI_SUCCESSFUL               0L
#define PCI_FUNC_NOT_SUPPORTED      -2L
#define PCI_BAD_VENDOR_ID           -3L
#define PCI_DEVICE_NOT_FOUND        -4L
#define PCI_BAD_REGISTER_NUMBER     -5L
#define PCI_SET_FAILED              -6L
#define PCI_BUFFER_TOO_SMALL        -7L
#define PCI_GENERAL_ERROR           -8L
#define PCI_BAD_HANDLE              -9L
#define PCI_BAD_RESOURCE           -10L

#define PCI_ANY_VENDOR          0xffffU

#define PCI_CLASS_MASK_PROGIF   0x01000000UL
#define PCI_CLASS_MASK_SUBCLASS 0x02000000UL
#define PCI_CLASS_MASK_BASE     0x04000000UL
#define PCI_CLASS_CODE_MASK     0x00ffffffUL

#define PCI_RESOURCE_IO         0x4000U
#define PCI_RESOURCE_LAST       0x8000U
#define PCI_RESOURCE_8BIT       0x0100U
#define PCI_RESOURCE_16BIT      0x0200U
#define PCI_RESOURCE_32BIT      0x0400U
#define PCI_RESOURCE_ORDER_MOTOROLA 0x0000U
#define PCI_RESOURCE_ORDER_INTEL    0x000fU

#define PCI_CONFIG_VENDOR_ID    0x00U
#define PCI_CONFIG_DEVICE_ID    0x02U
#define PCI_CONFIG_COMMAND      0x04U
#define PCI_CONFIG_STATUS       0x06U
#define PCI_CONFIG_PROGIF       0x09U
#define PCI_CONFIG_SUBCLASS     0x0aU
#define PCI_CONFIG_BASE_CLASS   0x0bU
#define PCI_CONFIG_HEADER_TYPE  0x0eU
#define PCI_CONFIG_BAR0         0x10U
#define PCI_CONFIG_INTERRUPT_LINE 0x3cU
#define PCI_CONFIG_INTERRUPT_PIN  0x3dU

#define PCI_MAX_BARS            6

typedef struct {
    UWORD next;
    UWORD flags;
    ULONG start;
    ULONG length;
    ULONG offset;
    ULONG dmaoffset;
} pci_resource_t;

typedef struct {
    ULONG address;
    ULONG length;
} pci_mem_t;

typedef void (*pci_interrupt_handler_t)(void *param);
typedef LONG (*pci_card_callback_t)(LONG function);

LONG pci_init(void);
LONG pci_find_device(UWORD vendor, UWORD device, UWORD index, PCI_HANDLE *handle);
LONG pci_find_classcode(ULONG classcode, UWORD index, PCI_HANDLE *handle);
LONG pci_read_config_byte(PCI_HANDLE handle, UWORD reg, UBYTE *value);
LONG pci_read_config_word(PCI_HANDLE handle, UWORD reg, UWORD *value);
LONG pci_read_config_long(PCI_HANDLE handle, UWORD reg, ULONG *value);
LONG pci_write_config_byte(PCI_HANDLE handle, UWORD reg, UBYTE value);
LONG pci_write_config_word(PCI_HANDLE handle, UWORD reg, UWORD value);
LONG pci_write_config_long(PCI_HANDLE handle, UWORD reg, ULONG value);
LONG pci_get_resource(PCI_HANDLE handle, UWORD bar, pci_resource_t *resource);
LONG pci_read_mem_byte(PCI_HANDLE handle, ULONG address, UBYTE *value);
LONG pci_read_mem_word(PCI_HANDLE handle, ULONG address, UWORD *value);
LONG pci_read_mem_long(PCI_HANDLE handle, ULONG address, ULONG *value);
LONG pci_write_mem_byte(PCI_HANDLE handle, ULONG address, UBYTE value);
LONG pci_write_mem_word(PCI_HANDLE handle, ULONG address, UWORD value);
LONG pci_write_mem_long(PCI_HANDLE handle, ULONG address, ULONG value);
LONG pci_read_io_byte(PCI_HANDLE handle, ULONG address, UBYTE *value);
LONG pci_read_io_word(PCI_HANDLE handle, ULONG address, UWORD *value);
LONG pci_read_io_long(PCI_HANDLE handle, ULONG address, ULONG *value);
LONG pci_write_io_byte(PCI_HANDLE handle, ULONG address, UBYTE value);
LONG pci_write_io_word(PCI_HANDLE handle, ULONG address, UWORD value);
LONG pci_write_io_long(PCI_HANDLE handle, ULONG address, ULONG value);
LONG pci_hook_interrupt(PCI_HANDLE handle, pci_interrupt_handler_t handler, void *param);
LONG pci_unhook_interrupt(PCI_HANDLE handle);
LONG pci_get_card_used(PCI_HANDLE handle, pci_card_callback_t *callback);
LONG pci_set_card_used(PCI_HANDLE handle, pci_card_callback_t callback, LONG state);
LONG pci_get_pagesize(ULONG *pagesize);
LONG pci_virt_to_bus(PCI_HANDLE handle, ULONG address, pci_mem_t *mem);
LONG pci_bus_to_virt(PCI_HANDLE handle, ULONG address, pci_mem_t *mem);
LONG pci_virt_to_phys(ULONG address, pci_mem_t *mem);
LONG pci_phys_to_virt(ULONG address, pci_mem_t *mem);

#endif /* PCI_H */
```

- [ ] **Step 2: Add PCI Kconfig options**

In `bios/Kconfig`, add this menu after the storage devices menu and before `menu "Serial ports and console"`:

```kconfig
menu "PCI bus support"

config CONF_WITH_PCI
	bool "PCI bus support"
	depends on MACHINE_VIRT_ARM
	default y if MACHINE_VIRT_ARM
	help
	  Native pTOS PCI layer modeled on useful Atari PCI BIOS service
	  semantics. This provides discovery, configuration-space access,
	  resource reporting and card ownership through pTOS C APIs, not the
	  Atari _PCI cookie or function-table ABI.

config CONF_WITH_PCI_VIRT_ECAM
	bool
	depends on CONF_WITH_PCI && MACHINE_VIRT_ARM
	default y
	help
	  QEMU ARM virt generic ECAM PCIe backend.

endmenu
```

- [ ] **Step 3: Wire initial PCI objects into the BIOS build**

In `bios/build.mk`, add these lines after the `MACHINE_VIRT_M68K` object line and before `CONF_WITH_VIRTIO_BLK`:

```make
obj-$(CONF_WITH_PCI) += pci_core.o
obj-$(CONF_WITH_PCI_VIRT_ECAM) += virt_pci.o
```

- [ ] **Step 4: Add the BIOS init hook**

In `bios/bios.c`, add the header include after the existing `#include "memory.h"` line:

```c
#if CONF_WITH_PCI
#include "pci.h"
#endif
```

Then add this initialization block after `fill_cookie_jar();` and before the console/Line-A initialization block:

```c
#if CONF_WITH_PCI
    KDEBUG(("pci_init()\n"));
    pci_init();
#endif
```

- [ ] **Step 5: Create temporary compile stubs only for this task**

Create `bios/pci_core.c` with this temporary implementation so Task 1 builds before Task 2 replaces it:

```c
/*
 * pci_core.c - native pTOS PCI access layer
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#if CONF_WITH_PCI

#include "pci.h"

LONG pci_init(void)
{
    return PCI_SUCCESSFUL;
}

#endif /* CONF_WITH_PCI */
```

Create `bios/machine/virt-arm/virt_pci.c` with this temporary implementation so Task 1 builds before Task 3 replaces it:

```c
/*
 * virt_pci.c - QEMU ARM virt PCI backend
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#ifndef MACHINE_VIRT_ARM
#error This file must only be compiled for the QEMU ARM virt target
#endif
```

- [ ] **Step 6: Verify config selection and builds**

Run:

```bash
make virt-arm_defconfig
grep '^CONF_WITH_PCI' .config
make obj/pci_core.o obj/virt_pci.o
make rpi2_defconfig
grep '^CONF_WITH_PCI' .config || true
make
```

Expected: `virt-arm_defconfig` selects `CONF_WITH_PCI=y` and `CONF_WITH_PCI_VIRT_ECAM=y`; `obj/pci_core.o` and `obj/virt_pci.o` build; `rpi2_defconfig` does not enable PCI; the Raspberry Pi 2 build still succeeds.

- [ ] **Step 7: Commit API and config wiring**

Run:

```bash
git add include/pci.h bios/Kconfig bios/build.mk bios/bios.c bios/pci_core.c bios/machine/virt-arm/virt_pci.c
git commit -m "Add PCI API and configuration wiring"
```

---

### Task 2: Generic PCI Core Enumeration And Lookup

**Files:**
- Create: `bios/pci_backend.h`
- Modify: `bios/pci_core.c`

**Interfaces:**
- Consumes: public API from Task 1.
- Produces: backend interface `pci_backend_t`, device enumeration, opaque handle table, lookup by vendor/device, lookup by class code, and config-space read/write helpers.

- [ ] **Step 1: Create the internal backend header**

Create `bios/pci_backend.h` with:

```c
/*
 * pci_backend.h - internal PCI host bridge backend interface
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef PCI_BACKEND_H
#define PCI_BACKEND_H

#include "portab.h"
#include "pci.h"

typedef struct {
    ULONG ecam_base;
    ULONG ecam_size;
    ULONG mmio_base;
    ULONG mmio_size;
    ULONG pio_base;
    ULONG pio_size;
} pci_backend_windows_t;

typedef struct {
    LONG (*init)(void);
    LONG (*get_windows)(pci_backend_windows_t *windows);
    LONG (*read_config)(UBYTE bus, UBYTE dev, UBYTE func, UWORD reg, UWORD size, ULONG *value);
    LONG (*write_config)(UBYTE bus, UBYTE dev, UBYTE func, UWORD reg, UWORD size, ULONG value);
    LONG (*bus_to_phys)(ULONG bus_address, BOOL io, ULONG *phys_address);
    LONG (*phys_to_bus)(ULONG phys_address, BOOL io, ULONG *bus_address);
    LONG (*hook_interrupt)(PCI_HANDLE handle, UBYTE line, pci_interrupt_handler_t handler, void *param);
    LONG (*unhook_interrupt)(PCI_HANDLE handle, UBYTE line);
} pci_backend_t;

const pci_backend_t *pci_backend_get(void);

#endif /* PCI_BACKEND_H */
```

- [ ] **Step 2: Replace the temporary core with enumeration state**

Replace `bios/pci_core.c` with a complete generic core built around these constants and structs:

```c
#define PCI_MAX_DEVICES 64
#define PCI_DEVICES_PER_BUS 32
#define PCI_FUNCTIONS_PER_DEVICE 8
#define PCI_HEADER_MULTIFUNCTION 0x80U

typedef struct {
    PCI_HANDLE handle;
    UBYTE bus;
    UBYTE dev;
    UBYTE func;
    UWORD vendor;
    UWORD device;
    ULONG classcode;
    UBYTE header_type;
    UBYTE interrupt_line;
    UBYTE interrupt_pin;
    pci_resource_t resources[PCI_MAX_BARS];
    pci_card_callback_t callback;
    LONG used;
} pci_device_t;
```

The file must include:

```c
#include "config.h"

#if CONF_WITH_PCI

#include "pci.h"
#include "pci_backend.h"
#include "kprint.h"

/* implementation */

#endif /* CONF_WITH_PCI */
```

- [ ] **Step 3: Implement handle validation and config access helpers**

Add internal helpers with these exact signatures:

```c
static pci_device_t *pci_device_from_handle(PCI_HANDLE handle);
static LONG pci_check_reg(UWORD reg, UWORD size);
static LONG pci_read_config_raw(pci_device_t *device, UWORD reg, UWORD size, ULONG *value);
static LONG pci_write_config_raw(pci_device_t *device, UWORD reg, UWORD size, ULONG value);
```

Requirements:

- `PCI_HANDLE_NONE` is invalid.
- A valid handle is `device_index + 1`.
- `pci_check_reg()` returns `PCI_BAD_REGISTER_NUMBER` when `reg >= 256`, `reg + size > 256`, word accesses are not 2-byte aligned, or long accesses are not 4-byte aligned.
- Config read/write helpers call backend callbacks only after validating the handle and register.

- [ ] **Step 4: Implement device scanning**

Implement:

```c
static void pci_scan_bus(UBYTE bus);
static void pci_scan_device(UBYTE bus, UBYTE dev);
static void pci_add_function(UBYTE bus, UBYTE dev, UBYTE func);
```

Requirements:

- Scan bus `0` only in this first implementation.
- For each device, read function 0 vendor ID at offset `PCI_CONFIG_VENDOR_ID`.
- Skip absent functions when vendor ID is `PCI_ANY_VENDOR`.
- Read header type from `PCI_CONFIG_HEADER_TYPE`.
- Scan functions 1 through 7 only when function 0 has `PCI_HEADER_MULTIFUNCTION` set.
- Stop adding devices when `PCI_MAX_DEVICES` is reached and log `KINFO(("pci: device table full\n"));` once for the first dropped function.
- Cache vendor ID, device ID, base/subclass/progif as one `ULONG classcode = (base << 16) | (subclass << 8) | progif`, header type, interrupt line, and interrupt pin.
- Leave BAR/resource decoding to Task 4 by zeroing `resources`.

- [ ] **Step 5: Implement public init and lookup functions**

Implement these public functions in `bios/pci_core.c`:

```c
LONG pci_init(void);
LONG pci_find_device(UWORD vendor, UWORD device, UWORD index, PCI_HANDLE *handle);
LONG pci_find_classcode(ULONG classcode, UWORD index, PCI_HANDLE *handle);
```

Requirements:

- `pci_init()` clears the device table, calls `pci_backend_get()`, calls `backend->init()`, scans bus 0, logs `KINFO(("pci: %u device(s) found\n", pci_device_count));`, and returns `PCI_SUCCESSFUL` when backend init succeeds even if no devices are found.
- `pci_find_device()` must not return `PCI_BAD_VENDOR_ID` for `vendor == PCI_ANY_VENDOR`; ignore `device` in that case to match the Atari special case.
- `pci_find_device()` returns `PCI_DEVICE_NOT_FOUND` and sets `*handle = PCI_HANDLE_NONE` when no match exists.
- `pci_find_classcode()` applies mask bits from the high byte of `classcode`: bit 26 ignores base class, bit 25 ignores subclass, bit 24 ignores programming interface.
- Both lookup functions treat a null output pointer as `PCI_GENERAL_ERROR`.

- [ ] **Step 6: Implement public config read/write functions**

Implement all config-space functions declared in `include/pci.h`. Byte reads write an `UBYTE`, word reads write a `UWORD`, and long reads write a `ULONG`. Byte/word writes pass the value in the low bits of `ULONG` to the backend.

- [ ] **Step 7: Compile-check the generic core**

Run:

```bash
make virt-arm_defconfig
make obj/pci_core.o
```

Expected: `obj/pci_core.o` builds without C90 declaration-order errors.

- [ ] **Step 8: Commit generic enumeration and lookup**

Run:

```bash
git add bios/pci_backend.h bios/pci_core.c
git commit -m "Add generic PCI enumeration and lookup"
```

---

### Task 3: QEMU ARM Virt ECAM Backend

**Files:**
- Create: `bios/machine/virt-arm/virt_pci.h`
- Modify: `bios/machine/virt-arm/virt_memmap.h`
- Modify: `bios/machine/virt-arm/virt_pci.c`

**Interfaces:**
- Consumes: `pci_backend_t` from Task 2.
- Produces: `const pci_backend_t *virt_pci_backend(void);` and `pci_backend_get()` for `CONF_WITH_PCI_VIRT_ECAM` builds.

- [ ] **Step 1: Add QEMU virt PCI memory map constants**

Add these constants to `bios/machine/virt-arm/virt_memmap.h` after `VIRT_UART0_BASE`:

```c
#define VIRT_PCIE_MMIO_BASE     0x10000000UL
#define VIRT_PCIE_MMIO_SIZE     0x2eff0000UL
#define VIRT_PCIE_PIO_BASE      0x3eff0000UL
#define VIRT_PCIE_PIO_SIZE      0x00010000UL
#define VIRT_PCIE_ECAM_BASE     0x3f000000UL
#define VIRT_PCIE_ECAM_SIZE     0x01000000UL
```

- [ ] **Step 2: Create the backend header**

Create `bios/machine/virt-arm/virt_pci.h` with:

```c
/*
 * virt_pci.h - QEMU ARM virt PCI backend
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VIRT_PCI_H
#define VIRT_PCI_H

#ifdef MACHINE_VIRT_ARM

#include "pci_backend.h"

const pci_backend_t *virt_pci_backend(void);

#endif /* MACHINE_VIRT_ARM */

#endif /* VIRT_PCI_H */
```

- [ ] **Step 3: Implement ECAM address calculation and backend callbacks**

Replace the temporary `bios/machine/virt-arm/virt_pci.c` with an implementation that:

- Includes `config.h`, `portab.h`, `endian.h`, `pci.h`, `pci_backend.h`, `virt_memmap.h`, and `virt_pci.h`.
- Defines `static LONG virt_pci_init(void)` returning `PCI_SUCCESSFUL`.
- Defines `static LONG virt_pci_get_windows(pci_backend_windows_t *windows)` filling the six constants from Step 1.
- Defines `static volatile UBYTE *virt_pci_ecam_ptr(UBYTE bus, UBYTE dev, UBYTE func, UWORD reg)` using `VIRT_PCIE_ECAM_BASE + ((ULONG)bus << 20) + ((ULONG)dev << 15) + ((ULONG)func << 12) + reg`.
- Defines read/write callbacks for size `1`, `2`, and `4` only.
- Uses `le2cpu16()`, `le2cpu32()`, `cpu2le16()`, and `cpu2le32()` for word/long config accesses.
- Defines `virt_pci_bus_to_phys()` and `virt_pci_phys_to_bus()` for low MMIO and PIO windows.
- Defines hook/unhook callbacks that return `PCI_FUNC_NOT_SUPPORTED`.
- Exposes a static `pci_backend_t virt_pci_backend_ops` and `const pci_backend_t *virt_pci_backend(void)`.

Use this backend selection function at the end of the file:

```c
const pci_backend_t *pci_backend_get(void)
{
    return virt_pci_backend();
}
```

- [ ] **Step 4: Compile-check backend config access**

Run:

```bash
make virt-arm_defconfig
make obj/virt_pci.o obj/pci_core.o
```

Expected: both objects build and link-visible `pci_backend_get()` is defined exactly once.

- [ ] **Step 5: Commit QEMU virt backend**

Run:

```bash
git add bios/machine/virt-arm/virt_pci.h bios/machine/virt-arm/virt_memmap.h bios/machine/virt-arm/virt_pci.c
git commit -m "Add QEMU virt PCI ECAM backend"
```

---

### Task 4: BAR Resources, Memory/I/O Access, Ownership, And Translation

**Files:**
- Modify: `bios/pci_core.c`

**Interfaces:**
- Consumes: backend config and translation callbacks from Task 3.
- Produces: decoded `pci_resource_t` descriptors, `pci_get_resource()`, memory/I/O access helpers, interrupt unsupported routing, card-used state, and PMMU/address-translation helpers.

- [ ] **Step 1: Add BAR decoding helpers**

Add these constants and helpers to `bios/pci_core.c`:

```c
#define PCI_BAR_IO              0x00000001UL
#define PCI_BAR_MEM_TYPE_MASK   0x00000006UL
#define PCI_BAR_MEM_TYPE_64     0x00000004UL
#define PCI_BAR_MEM_PREFETCH    0x00000008UL
#define PCI_BAR_IO_MASK         0xfffffffcUL
#define PCI_BAR_MEM_MASK        0xfffffff0UL
```

Add internal functions:

```c
static void pci_decode_bars(pci_device_t *device);
static void pci_decode_bar(pci_device_t *device, UWORD bar);
static ULONG pci_bar_size(ULONG mask, BOOL io);
```

Requirements:

- During `pci_add_function()`, call `pci_decode_bars()` after interrupt fields are read.
- For each BAR, save the original value, write `0xffffffffUL`, read the mask, restore the original value, and skip BARs where original or mask is zero.
- I/O BAR bus address is `original & PCI_BAR_IO_MASK` and size uses `mask & PCI_BAR_IO_MASK`.
- Memory BAR bus address is `original & PCI_BAR_MEM_MASK` and size uses `mask & PCI_BAR_MEM_MASK`.
- Size is computed as `(~masked_size) + 1` after applying the proper mask.
- For 64-bit memory BARs, mark the low BAR resource as present with the low 32-bit address and skip the next BAR. The first implementation supports only low 32-bit CPU addresses.
- Set `PCI_RESOURCE_IO` for I/O resources.
- Set `PCI_RESOURCE_8BIT | PCI_RESOURCE_16BIT | PCI_RESOURCE_32BIT | PCI_RESOURCE_ORDER_INTEL` for all implemented resources.
- Set `PCI_RESOURCE_LAST` on the last populated descriptor for a device. If no BARs exist, every resource remains zero.
- Fill `offset` as `phys_address - bus_address` using backend `bus_to_phys()`.
- Fill `dmaoffset` as `0` for the first backend.

- [ ] **Step 2: Implement resource lookup**

Implement:

```c
LONG pci_get_resource(PCI_HANDLE handle, UWORD bar, pci_resource_t *resource);
```

Requirements:

- Return `PCI_BAD_HANDLE` for invalid handles.
- Return `PCI_BAD_RESOURCE` for `bar >= PCI_MAX_BARS`, null `resource`, or a BAR with `length == 0`.
- Copy the cached descriptor into `*resource` on success.

- [ ] **Step 3: Implement memory and I/O address validation**

Add helper:

```c
static LONG pci_find_resource_for_address(pci_device_t *device, BOOL io, ULONG address, UWORD size, pci_resource_t **resource);
```

Requirements:

- Match resources by `PCI_RESOURCE_IO` flag and `address >= start && address + size <= start + length`.
- Return `PCI_BAD_RESOURCE` when no matching resource covers the requested range.
- Avoid overflow by rejecting requests where `address + size < address`.

- [ ] **Step 4: Implement memory and I/O read/write helpers**

Implement all memory and I/O functions declared in `include/pci.h`.

Requirements:

- Validate handle and output pointer before reading.
- Use `pci_find_resource_for_address()`.
- Compute CPU address as `address + resource->offset`.
- Use volatile `UBYTE *`, `UWORD *`, and `ULONG *` casts for loads/stores.
- Use little-endian conversion for word/long values with `le2cpu16()`, `le2cpu32()`, `cpu2le16()`, and `cpu2le32()`.
- Return `PCI_SUCCESSFUL` on successful access.

- [ ] **Step 5: Implement interrupt and ownership helpers**

Implement:

```c
LONG pci_hook_interrupt(PCI_HANDLE handle, pci_interrupt_handler_t handler, void *param);
LONG pci_unhook_interrupt(PCI_HANDLE handle);
LONG pci_get_card_used(PCI_HANDLE handle, pci_card_callback_t *callback);
LONG pci_set_card_used(PCI_HANDLE handle, pci_card_callback_t callback, LONG state);
```

Requirements:

- Interrupt functions validate the handle and delegate to backend callbacks with the cached interrupt line. The QEMU virt backend returns `PCI_FUNC_NOT_SUPPORTED`.
- `pci_hook_interrupt()` returns `PCI_GENERAL_ERROR` if `handler == 0`.
- `pci_get_card_used()` returns the cached `used` state and stores the callback in `*callback` when the output pointer is not null.
- `pci_set_card_used()` accepts only states `0`, `1`, `2`, and `3`; other values return `PCI_GENERAL_ERROR`.
- State `2` stores the callback pointer; states `0`, `1`, and `3` clear the callback.

- [ ] **Step 6: Implement PMMU/address helpers**

Implement:

```c
LONG pci_get_pagesize(ULONG *pagesize);
LONG pci_virt_to_bus(PCI_HANDLE handle, ULONG address, pci_mem_t *mem);
LONG pci_bus_to_virt(PCI_HANDLE handle, ULONG address, pci_mem_t *mem);
LONG pci_virt_to_phys(ULONG address, pci_mem_t *mem);
LONG pci_phys_to_virt(ULONG address, pci_mem_t *mem);
```

Requirements:

- `pci_get_pagesize()` writes `0` and returns `PCI_SUCCESSFUL`.
- Handle-based functions validate the handle before using backend translation.
- For the first backend, translation is identity unless backend `phys_to_bus()` or `bus_to_phys()` maps the address through a PCI window.
- Fill `pci_mem_t.address` with the translated address and `pci_mem_t.length` with `0xffffffffUL - address` when translation succeeds.

- [ ] **Step 7: Compile-check resource implementation**

Run:

```bash
make virt-arm_defconfig
make obj/pci_core.o
```

Expected: `obj/pci_core.o` builds without C90 declaration-order errors or missing prototypes.

- [ ] **Step 8: Commit resource and service helpers**

Run:

```bash
git add bios/pci_core.c
git commit -m "Add PCI resources and service helpers"
```

---

### Task 5: Virt-ARM Validation Path And Final Polish

**Files:**
- Modify: `docs/superpowers/specs/2026-08-02-generic-pci-bios-design.md` if implementation details changed.
- Modify: `docs/superpowers/plans/2026-08-02-generic-pci-bios-layer.md` if execution decisions changed the plan.
- Modify: source files from Tasks 1-4 only for build or review fixes.

**Interfaces:**
- Consumes: completed Tasks 1-4.
- Produces: verified branch with documented limits and pushed PR #61 updates.

- [ ] **Step 1: Verify generated configurations**

Run:

```bash
make virt-arm_defconfig
grep '^CONF_WITH_PCI' .config
make rpi2_defconfig
grep '^CONF_WITH_PCI' .config || true
make rpi4_defconfig
grep '^CONF_WITH_PCI' .config || true
```

Expected: `virt-arm_defconfig` selects `CONF_WITH_PCI=y` and `CONF_WITH_PCI_VIRT_ECAM=y`; Raspberry Pi configs do not select PCI in issue #56.

- [ ] **Step 2: Run build checks**

Run:

```bash
make virt-arm_defconfig
make
make rpi2_defconfig
make
make rpi4_defconfig
make
```

Expected: all configured builds succeed if the required toolchains are installed. Record exact toolchain errors if a build cannot run locally.

- [ ] **Step 3: Run a QEMU virt-arm smoke boot with a PCI device**

Run the QEMU command appropriate for the produced `virt-arm` image. If the build output says an ELF image is ready, use that image path in this shape:

```bash
qemu-system-arm -M virt -cpu cortex-a7 -kernel emutos.elf -nographic -serial stdio -device virtio-net-pci
```

Expected: boot reaches BIOS startup far enough to print PCI initialization, and the log includes `pci: N device(s) found` with `N` greater than zero. If the command line needs this tree's documented virt-arm boot flags, adjust only the machine/image arguments and record the exact command in the report.

- [ ] **Step 4: Verify debug discoverability without adding permanent diagnostics**

Inspect boot output from Step 3. If `pci: N device(s) found` is the only PCI log, keep it. Do not add a permanent CLI command in issue #56 because `virt-arm_defconfig` disables CLI and this feature is a BIOS service, not a user command.

- [ ] **Step 5: Run readiness checks**

Run:

```bash
make gitready
git status --short
git diff --stat master...HEAD
```

Expected: readiness checks pass or report only pre-existing/toolchain-limited failures; `package-lock.json` remains untracked and unstaged.

- [ ] **Step 6: Update docs only for real deviations**

If implementation differs from `docs/superpowers/specs/2026-08-02-generic-pci-bios-design.md`, update the spec with the exact final behavior. If the human-approved Task 1/Task 4 sequencing changes the plan, update this plan with the exact final task shape. Do not create a documentation-only commit when the docs already match the implementation.

- [ ] **Step 7: Commit any final fixes or documentation changes**

When files changed during Task 5, run:

```bash
git add include/pci.h bios/Kconfig bios/build.mk bios/bios.c bios/pci_backend.h bios/pci_core.c bios/machine/virt-arm/virt_pci.h bios/machine/virt-arm/virt_pci.c bios/machine/virt-arm/virt_memmap.h docs/superpowers/specs/2026-08-02-generic-pci-bios-design.md docs/superpowers/plans/2026-08-02-generic-pci-bios-layer.md
git commit -m "Verify generic PCI BIOS layer"
```

If no files changed, do not create an empty commit.

- [ ] **Step 8: Push PR #61 branch**

Run:

```bash
git push
```

Expected: branch `feature/56-generic-pci-bus-support` updates draft PR #61.

---

## Self-Review

- Spec coverage: Task 1 covers public native API, Kconfig, build wiring, and BIOS init; Task 2 covers handles, enumeration, lookup, and config-space helpers; Task 3 covers QEMU ARM `virt` ECAM backend; Task 4 covers BAR resources, memory/I/O helpers, interrupt unsupported paths, ownership, and address translation; Task 5 covers build and QEMU validation.
- Placeholder scan: No placeholder markers or deferred implementation text remain. Raspberry Pi 4 and inherited PCI-code migration are explicitly excluded because issues #57 and #59 own them.
- Type consistency: `PCI_HANDLE`, `pci_resource_t`, `pci_mem_t`, `pci_backend_t`, and public function signatures are defined before later tasks consume them and use `portab.h` types consistently.
