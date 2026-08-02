# Raspberry Pi 4 USB xHCI/VL805 Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first Raspberry Pi 4/400 USB foundation by enabling a configured xHCI host-controller path that compiles and fails safely until real hardware validation is available.

**Architecture:** Keep the existing generic USB stack unchanged and add a second host-controller driver beside DWC2. Put Raspberry Pi 4 VL805/PCIe resource discovery behind a small machine-specific BIOS helper so generic USB code depends only on configuration symbols and a resource-returning interface.

**Tech Stack:** C90 with GNU extensions, Kconfig, Kbuild-style `build.mk`, freestanding pTOS BIOS/USB code, `KINFO(())`/`KDEBUG(())` tracing, existing `struct ucdif` USB controller-driver API.

## Global Constraints

- Preserve Raspberry Pi 1/2/3 DWC2 behavior.
- Raspberry Pi 4/400 external USB uses VL805 behind BCM2711 PCIe, not DWC2.
- Local QEMU `raspi4b` cannot validate the real Pi 4 USB path because it disables `brcm,bcm2711-pcie` and wires only DWC2.
- Do not add a generic PCI subsystem beyond the Pi 4 resource helper.
- Do not replace the existing USB enumeration, hub, or HID mouse layers.
- Keep machine-specific hardware details behind Raspberry Pi-specific BIOS files.
- C code must remain C90-compatible: declarations at the top of blocks, `/* */` comments, pTOS fixed-width types where relevant.
- Do not touch the existing untracked `package-lock.json`.

---

## File Structure

- Modify `usb/Kconfig`: make `CONF_WITH_USB` available on Raspberry Pi 4 when an xHCI controller exists; add `CONF_WITH_USB_XHCI`; keep DWC2 defaulted only for Raspberry Pi 1/2/3.
- Modify `usb/build.mk`: compile `ucd_xhci.o` only when `CONF_WITH_USB_XHCI` is set.
- Modify `usb/usb.c`: use `CONF_WITH_USB_DWC2` and `CONF_WITH_USB_XHCI` for built-in host-controller declarations and initialization.
- Create `bios/machine/raspi/raspi_vl805.h`: define `raspi_vl805_resources_t` and declare `raspi_vl805_get_resources()`.
- Create `bios/machine/raspi/raspi_vl805.c`: provide the first Pi 4 VL805 resource helper; initially returns failure with a trace instead of pretending hardware is usable.
- Modify `bios/build.mk`: compile `raspi_vl805.o` only for `CONF_WITH_USB_XHCI`.
- Create `usb/ucd_xhci.h`: declare `xhci_init()`.
- Create `usb/ucd_xhci.c`: add a `struct ucdif`-based xHCI UCD skeleton that registers, probes VL805 resources, and fails safely for unsupported transfers.

---

### Task 1: Wire Raspberry Pi 4 USB Configuration

**Files:**
- Modify: `usb/Kconfig:12-28`
- Modify: `usb/build.mk:8-10`
- Modify: `usb/usb.c:46-78`

**Interfaces:**
- Consumes: existing Kconfig symbols `MACHINE_RPI`, `TARGET_RPI4`, `CONF_WITH_USB_DWC2`.
- Produces: new Kconfig symbol `CONF_WITH_USB_XHCI`; new function declaration/use `void xhci_init(void)` guarded by `CONF_WITH_USB_XHCI`.

- [ ] **Step 1: Update USB Kconfig dependencies**

Replace the USB menu body in `usb/Kconfig` with this content:

```kconfig
menu "USB support"

config CONF_WITH_USB
	bool "USB stack"
	depends on MACHINE_RPI
	default y
	help
	  A minimal USB stack, derived from the one in FreeMiNT and U-Boot.
	  It currently provides mice using the HID boot report protocol.
	  A host controller driver is required, so the stack is only
	  available on machines that have one.

config CONF_WITH_USB_DWC2
	bool
	default y if CONF_WITH_USB && MACHINE_RPI && !TARGET_RPI4
	help
	  The Synopsys DesignWare USB 2.0 OTG host controller, used on the
	  Raspberry Pi 1, 2 and 3.

config CONF_WITH_USB_XHCI
	bool
	default y if CONF_WITH_USB && TARGET_RPI4
	help
	  The xHCI host controller used for the Raspberry Pi 4/400 external
	  USB ports through the VL805 PCIe bridge.

endmenu
```

- [ ] **Step 2: Wire the xHCI object into the USB build**

Edit `usb/build.mk` so the end of the file is:

```make
obj-y += usb.o ucd.o udd.o usb_api.o usb_hub.o udd_mouse.o

obj-$(CONF_WITH_USB_DWC2) += ucd_dwc2.o
obj-$(CONF_WITH_USB_XHCI) += ucd_xhci.o
```

- [ ] **Step 3: Make USB built-in UCD init configuration-driven**

In `usb/usb.c`, replace the built-in driver declarations near lines 46-50 with:

```c
/* Built-in drivers: */
#if CONF_WITH_USB_DWC2
extern void dwc2_init(void);
#endif
#if CONF_WITH_USB_XHCI
extern void xhci_init(void);
#endif
extern int usb_mouse_init(void);
```

Then replace the built-in driver initialization block near lines 74-78 with:

```c
    /* Initialize built-in drivers */
#if CONF_WITH_USB_DWC2
    dwc2_init();
#endif
#if CONF_WITH_USB_XHCI
    xhci_init();
#endif
    usb_mouse_init();
```

- [ ] **Step 4: Run generated config smoke checks**

Run:

```bash
make rpi2_defconfig
grep '^CONF_WITH_USB' .config
make rpi4_defconfig
grep '^CONF_WITH_USB' .config
```

Expected rpi2 output includes `CONF_WITH_USB=y` and `CONF_WITH_USB_DWC2=y`. Expected rpi4 output includes `CONF_WITH_USB=y` and `CONF_WITH_USB_XHCI=y`.

- [ ] **Step 5: Commit configuration wiring**

Run:

```bash
git add usb/Kconfig usb/build.mk usb/usb.c
git commit -m "Wire Raspberry Pi 4 USB xHCI configuration"
```

---

### Task 2: Add Raspberry Pi 4 VL805 Resource Helper

**Files:**
- Create: `bios/machine/raspi/raspi_vl805.h`
- Create: `bios/machine/raspi/raspi_vl805.c`
- Modify: `bios/build.mk`

**Interfaces:**
- Consumes: `config.h`, `portab.h`, `kprint.h`, Raspberry Pi target symbols.
- Produces: `typedef struct raspi_vl805_resources_t { ULONG mmio_base; ULONG mmio_size; UWORD irq; } raspi_vl805_resources_t;` and `BOOL raspi_vl805_get_resources(raspi_vl805_resources_t *resources);`.

- [ ] **Step 1: Create the public helper header**

Create `bios/machine/raspi/raspi_vl805.h` with:

```c
/*
 * raspi_vl805.h - Raspberry Pi 4 VL805 USB controller resources
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef RASPI_VL805_H
#define RASPI_VL805_H

#include "portab.h"

typedef struct {
    ULONG mmio_base;
    ULONG mmio_size;
    UWORD irq;
} raspi_vl805_resources_t;

BOOL raspi_vl805_get_resources(raspi_vl805_resources_t *resources);

#endif /* RASPI_VL805_H */
```

- [ ] **Step 2: Create the first resource-helper implementation**

Create `bios/machine/raspi/raspi_vl805.c` with:

```c
/*
 * raspi_vl805.c - Raspberry Pi 4 VL805 USB controller resources
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#ifndef TARGET_RPI4
#error This file must only be compiled for Raspberry Pi 4 targets
#endif

#include "kprint.h"
#include "raspi_vl805.h"

BOOL raspi_vl805_get_resources(raspi_vl805_resources_t *resources)
{
    if (resources != 0) {
        resources->mmio_base = 0;
        resources->mmio_size = 0;
        resources->irq = 0;
    }

    KINFO(("VL805/xHCI: BCM2711 PCIe discovery is not implemented yet\n"));
    return FALSE;
}
```

- [ ] **Step 3: Wire the helper into the BIOS build**

In `bios/build.mk`, add this object next to the other Raspberry Pi machine-specific object wiring:

```make
obj-$(CONF_WITH_USB_XHCI) += raspi_vl805.o
```

If the file already groups Raspberry Pi objects, place it in that group; otherwise place it near other conditional machine objects and preserve link-order-sensitive comments.

- [ ] **Step 4: Compile-check the helper target selection**

Run:

```bash
make rpi4_defconfig
make obj/raspi_vl805.o
```

Expected: `obj/raspi_vl805.o` builds or the build proceeds until a missing external toolchain is reported. If the target name is not accepted by the Makefile, run `make` and verify the compile command includes `raspi_vl805.c`.

- [ ] **Step 5: Commit the VL805 helper**

Run:

```bash
git add bios/machine/raspi/raspi_vl805.h bios/machine/raspi/raspi_vl805.c bios/build.mk
git commit -m "Add Raspberry Pi 4 VL805 resource stub"
```

---

### Task 3: Add xHCI UCD Skeleton

**Files:**
- Create: `usb/ucd_xhci.h`
- Create: `usb/ucd_xhci.c`

**Interfaces:**
- Consumes: `raspi_vl805_get_resources(raspi_vl805_resources_t *resources)` from Task 2; existing `ucd_register()`, `struct ucdif`, ioctl command constants from `usb_api.h`.
- Produces: `void xhci_init(void)` for Task 1's `usb_init()` call; a registered UCD named `xhci` that fails cleanly while hardware bring-up is incomplete.

- [ ] **Step 1: Create the xHCI header**

Create `usb/ucd_xhci.h` with:

```c
/*
 * ucd_xhci.h - xHCI USB host controller driver
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef UCD_XHCI_H
#define UCD_XHCI_H

void xhci_init(void);

#endif /* UCD_XHCI_H */
```

- [ ] **Step 2: Create the xHCI UCD skeleton**

Create `usb/ucd_xhci.c` with:

```c
/*
 * ucd_xhci.c - xHCI USB host controller driver
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "usb_global.h"
#include "usb.h"
#include "usb_api.h"
#include "raspi_vl805.h"
#include "ucd_xhci.h"

struct xhci_priv {
    raspi_vl805_resources_t resources;
    BOOL have_resources;
};

static long xhci_open(struct ucdif *u);
static long xhci_close(struct ucdif *u);
static long xhci_ioctl(struct ucdif *u, short cmd, long arg);
static long xhci_lowlevel_init(struct xhci_priv *priv);

static char xhci_lname[] = "xHCI USB driver\0";
static struct usb_device *root_hub_dev = NULL;
static struct xhci_priv xhci_local;
static struct ucdif xhci_uif =
{
    0,
    USB_API_VERSION,
    USB_CONTRLL,
    xhci_lname,
    "xhci",
    0,
    0,
    xhci_open,
    xhci_close,
    0,
    xhci_ioctl,
    0,
    (long *)&xhci_local
};

static long xhci_open(struct ucdif *u)
{
    (void)u;
    return E_OK;
}

static long xhci_close(struct ucdif *u)
{
    struct xhci_priv *priv;

    priv = (struct xhci_priv *)u->ucd_priv;
    priv->have_resources = FALSE;
    return E_OK;
}

static long xhci_lowlevel_init(struct xhci_priv *priv)
{
    priv->have_resources = raspi_vl805_get_resources(&priv->resources);
    if (!priv->have_resources) {
        KINFO(("xhci: VL805 controller not available\n"));
        return -1;
    }

    KINFO(("xhci: MMIO 0x%lx size 0x%lx irq %u\n",
           priv->resources.mmio_base,
           priv->resources.mmio_size,
           priv->resources.irq));
    KINFO(("xhci: controller bring-up is not implemented yet\n"));
    return -1;
}

static long xhci_ioctl(struct ucdif *u, short cmd, long arg)
{
    struct xhci_priv *priv;

    priv = (struct xhci_priv *)u->ucd_priv;

    switch (cmd) {
    case LOWLEVEL_INIT:
        return xhci_lowlevel_init(priv);
    case LOWLEVEL_STOP:
        priv->have_resources = FALSE;
        return E_OK;
    case SUBMIT_CONTROL_MSG:
    case SUBMIT_BULK_MSG:
    case SUBMIT_INT_MSG:
        (void)arg;
        KINFO(("xhci: transfer submission is not implemented yet\n"));
        return -1;
    default:
        (void)arg;
        return -1;
    }
}

void xhci_init(void)
{
    if (ucd_register(&xhci_uif, &root_hub_dev)) {
        KINFO(("xhci_init(): ucd register failed!\n"));
        return;
    }

    KDEBUG(("xhci_init(): ucd register succeeded!\n"));
}
```

- [ ] **Step 3: Verify include paths**

Run:

```bash
make rpi4_defconfig
make obj/ucd_xhci.o
```

Expected: `ucd_xhci.c` finds `raspi_vl805.h` through the existing `usb_copts` BIOS include paths, or the build reports only missing external toolchain problems.

- [ ] **Step 4: Commit the xHCI UCD skeleton**

Run:

```bash
git add usb/ucd_xhci.h usb/ucd_xhci.c
git commit -m "Add xHCI USB controller skeleton"
```

---

### Task 4: Build Verification and Documentation Polish

**Files:**
- Modify if needed: `docs/superpowers/specs/2026-08-02-rpi4-usb-xhci-design.md`
- Modify if needed: `docs/superpowers/plans/2026-08-02-rpi4-usb-xhci-foundation.md`

**Interfaces:**
- Consumes: completed Tasks 1-3.
- Produces: verified buildable foundation and documentation that reflects any implementation adjustments.

- [ ] **Step 1: Verify generated configs**

Run:

```bash
make rpi2_defconfig
grep '^CONF_WITH_USB' .config
make rpi4_defconfig
grep '^CONF_WITH_USB' .config
```

Expected rpi2 config selects DWC2. Expected rpi4 config selects xHCI.

- [ ] **Step 2: Run Raspberry Pi 2 build smoke test**

Run:

```bash
make rpi2_defconfig
make
```

Expected: build succeeds if the ARM toolchain is installed. If `arm-none-eabi-*` is missing, record that verification is blocked by the missing toolchain.

- [ ] **Step 3: Run Raspberry Pi 4 build smoke test**

Run:

```bash
make rpi4_defconfig
make
```

Expected: build succeeds if the ARM toolchain is installed. If `arm-none-eabi-*` is missing, record that verification is blocked by the missing toolchain.

- [ ] **Step 4: Run formatting/readiness check**

Run:

```bash
make gitready
```

Expected: no style errors. If the command requires unavailable toolchain pieces, record the exact failure.

- [ ] **Step 5: Inspect final diff**

Run:

```bash
git diff --stat master...HEAD
git diff master...HEAD -- usb/Kconfig usb/build.mk usb/usb.c bios/build.mk bios/machine/raspi/raspi_vl805.h bios/machine/raspi/raspi_vl805.c usb/ucd_xhci.h usb/ucd_xhci.c docs/superpowers/specs/2026-08-02-rpi4-usb-xhci-design.md docs/superpowers/plans/2026-08-02-rpi4-usb-xhci-foundation.md
```

Expected: only issue #37 design, plan, and USB/VL805 scaffold files are changed. The untracked `package-lock.json` is not staged.

- [ ] **Step 6: Commit verification notes if docs changed**

If verification required documentation updates, run:

```bash
git add docs/superpowers/specs/2026-08-02-rpi4-usb-xhci-design.md docs/superpowers/plans/2026-08-02-rpi4-usb-xhci-foundation.md
git commit -m "Document Raspberry Pi 4 USB verification limits"
```

- [ ] **Step 7: Push the branch**

Run:

```bash
git push
```

Expected: branch `feature/37-add-usb-support-for-rpi4-400` updates PR #55.

---

## Self-Review

- Spec coverage: Task 1 covers Kconfig/build and config-driven init; Task 2 covers Pi 4-specific VL805 discovery boundary; Task 3 covers xHCI UCD skeleton and safe failure; Task 4 covers QEMU/hardware validation limits through build and documentation checks.
- Placeholder scan: No placeholders are present; incomplete hardware bring-up is intentionally represented as explicit safe-failure behavior.
- Type consistency: `raspi_vl805_resources_t`, `raspi_vl805_get_resources()`, and `xhci_init()` signatures are defined before use and are consistent across tasks.
