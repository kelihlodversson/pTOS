# RPi4 PCIe Outbound/Inbound Window Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix `bios/machine/raspi/raspi_pci.c` so `raspi_pci_bus_to_phys()` and `raspi_pci_phys_to_bus()` return real, correct address translations instead of unconditionally failing, unblocking `pci_get_resource()`/BAR mapping on real RPi4 hardware (concretely, `raspi_vl805_get_resources()`, part of #270).

**Architecture:** Relocate the PCIe outbound window's CPU-side physical base from an unreachable >4GB address (`0x6_00000000`) to a fixed 32-bit address (`0xf9000000` as originally implemented by Task 1 below; relocated again to `0xf8000000` by a final-review fix commit for extra margin — see the design doc's Risks section), guarded by a boot-time check against detected RAM (`phystop`) rather than an unverified assumption. `raspi_pci_bus_to_phys()` becomes a real translation gated on that check having passed. `raspi_pci_phys_to_bus()` is a separate, independent fix — the inbound window it translates through (PCIe bus `0x0` → CPU physical `0x0`, 2 GiB) was already correct and 32-bit-reachable, so it just needs a real range check plus identity passthrough. No new files, no MMU changes (the whole 4GB space is already flat-identity-mapped).

**Tech Stack:** C90/GNU extensions (`-std=gnu90`), `portab.h` types, no libc, ARM cross-compiled freestanding OS.

**Spec:** `docs/superpowers/specs/2026-08-25-rpi4-pcie-outbound-window-design.md`

## Global Constraints

- C90 with GNU extensions (`-std=gnu90`); declarations at the top of each block.
- 4 spaces, never a hard tab. Run `make gitready` before every commit.
- Use `portab.h` types (`ULONG`, `BOOL`) — never bare `int`/`unsigned`/`long` in new code.
- Trace with `KINFO(())`, never a private printf.
- `make rpi4_defconfig && make` must succeed with no new warnings after every task. `make rpi2_defconfig && make` and `make virt-arm_defconfig && make` (regression) must also succeed — checked once, in the final task, since `raspi_pci.c` only compiles under `TARGET_RPI4` and no task touches any file those other configs build.
- No automated functional test exists for this code (QEMU has no RPi4 PCIe model). Each task's test cycle is build-clean plus `make gitready` clean; real hardware validation is a manual step handed off at the end, same as #270.

---

## File Structure

- **Modify `bios/machine/raspi/raspi_pci.c`** only. No new files. Two independent slices: the outbound-window relocation + `bus_to_phys()` (interdependent — `bus_to_phys()` is meaningless without the relocated window and its safety flag), and `phys_to_bus()` (independent of the other two — the inbound window it uses was never broken).

## Interfaces

- Task 1 produces: `RASPI_PCIE_OUTBOUND_CPU_BASE` (replaces the `_LO`/`_HI` pair), `raspi_pci_outbound_window_enabled` (static `BOOL`, readable by later code in the same file), an updated `raspi_pci_set_outbound_window()`, an updated `raspi_pci_init()`, and a real `raspi_pci_bus_to_phys()`.
- Task 2 consumes nothing from Task 1 — `raspi_pci_phys_to_bus()` only needs the existing `RASPI_PCIE_DMA_BUS_BASE`/`RASPI_PCIE_INBOUND_SIZE` constants, unchanged since before this plan.
- Already-existing, unchanged: `extern UBYTE *phystop;` (`include/tosvars.h`), `PCI_SUCCESSFUL`/`PCI_GENERAL_ERROR`/`PCI_BAD_RESOURCE`/`PCI_BACKEND_UNMAPPABLE` (`include/pci.h`), `raspi_pci_readl()`/`raspi_pci_writel()`/`raspi_pci_replace_bits()` (this file, unchanged).

---

### Task 1: Relocate the outbound window and implement `bus_to_phys()`

**Files:**
- Modify: `bios/machine/raspi/raspi_pci.c`

**Interfaces:**
- Consumes: `extern UBYTE *phystop;` (`include/tosvars.h` — not yet included in this file; add the include).
- Produces: `RASPI_PCIE_OUTBOUND_CPU_BASE` (macro), `raspi_pci_outbound_window_enabled` (static `BOOL`) — both consumed by this same task's `raspi_pci_bus_to_phys()`, and available to any later code in this file.

- [ ] **Step 1: Add the `tosvars.h` include and replace the outbound base constants**

Add `#include "tosvars.h"` to the includes at the top of `bios/machine/raspi/raspi_pci.c` (after the existing `#include "raspi_pci.h"` at line 22 — needed for `phystop`).

Replace (currently lines 66-67):

```c
#define RASPI_PCIE_OUTBOUND_CPU_BASE_LO 0x00000000UL
#define RASPI_PCIE_OUTBOUND_CPU_BASE_HI 0x00000006UL
```

with:

```c
/*
 * CPU-side physical base of the outbound (CPU -> PCIe) MMIO window.
 * The BCM2711 root port's real hardware placement for this window is
 * 0x6_00000000 -- above the 4 GiB boundary a 32-bit ARM address can
 * express. This port has no LPAE support, so the window is relocated
 * here to a fixed, 1 MB-aligned 32-bit address instead: 0xf9000000 to
 * 0xfcffffff (RASPI_PCIE_MMIO_SIZE, 64 MiB), ending 5 MiB clear of
 * RASPI_PCIE_REG_BASE (0xfd500000), the PCIe controller's own fixed
 * register block. The whole 4 GiB space is already flat-identity-mapped
 * by init_mmu() (bios/machine/raspi/memory.c), so this needs no new MMU
 * work -- but it must not overlap real RAM. raspi_pci_init() verifies
 * that against phystop before enabling this window; see
 * raspi_pci_outbound_window_enabled below.
 */
#define RASPI_PCIE_OUTBOUND_CPU_BASE    0xf9000000UL
```

- [ ] **Step 2: Add the `raspi_pci_outbound_window_enabled` flag**

In the static variable declarations (currently line 144, right after the `raspi_pci_intx_isr[]` array):

```c
static BOOL raspi_pci_link_ready;
```

add immediately after it:

```c
static BOOL raspi_pci_outbound_window_enabled;
```

- [ ] **Step 3: Update `raspi_pci_set_outbound_window()`**

Replace the function body (currently lines 216-235):

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

with:

```c
static void raspi_pci_set_outbound_window(void)
{
    ULONG cpu_mb;
    ULONG limit_mb;
    ULONG reg;

    raspi_pci_writel(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LO, RASPI_PCIE_MMIO_BUS_BASE);
    raspi_pci_writel(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_HI, 0UL);

    cpu_mb = RASPI_PCIE_OUTBOUND_CPU_BASE >> 20;
    limit_mb = (RASPI_PCIE_OUTBOUND_CPU_BASE + RASPI_PCIE_MMIO_SIZE - 1UL) >> 20;

    reg = raspi_pci_readl(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT);
    reg = raspi_pci_replace_bits(reg, cpu_mb, PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT_BASE_MASK);
    reg = raspi_pci_replace_bits(reg, limit_mb, PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT_LIMIT_MASK);
    raspi_pci_writel(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT, reg);

    raspi_pci_writel(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI, 0UL);
    raspi_pci_writel(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI, 0UL);
}
```

(Only the base-address source changed — `RASPI_PCIE_OUTBOUND_CPU_BASE_LO` → `RASPI_PCIE_OUTBOUND_CPU_BASE`, and the two `_HI` register writes now write the literal `0UL` instead of the removed `RASPI_PCIE_OUTBOUND_CPU_BASE_HI` constant, since the window's CPU-side base is now fully expressible in 32 bits.)

- [ ] **Step 4: Guard `raspi_pci_set_outbound_window()`'s call site with the boot-time check**

In `raspi_pci_init()`, replace (currently lines 331-333):

```c
    raspi_pci_set_inbound_window();
    raspi_pci_set_outbound_window();
    raspi_pci_set_root_bridge_class();
```

with:

```c
    raspi_pci_set_inbound_window();

    raspi_pci_outbound_window_enabled = ((ULONG)phystop <= RASPI_PCIE_OUTBOUND_CPU_BASE);
    if (raspi_pci_outbound_window_enabled) {
        raspi_pci_set_outbound_window();
    } else {
        KINFO(("pci: detected RAM reaches the PCIe outbound MMIO window "
               "(phystop=0x%lx >= 0x%lx); BAR/MMIO resource access will "
               "be unavailable\n",
               (ULONG)phystop, RASPI_PCIE_OUTBOUND_CPU_BASE));
    }

    raspi_pci_set_root_bridge_class();
```

Note: this only skips the outbound window. Config-space enumeration
(`raspi_pci_config_ptr()`, used by `raspi_pci_read_config()`/`raspi_pci_write_config()`),
INTx interrupt routing, and the inbound window are all unaffected by this
check and continue to work even if it fails — PCI init as a whole does
not abort here.

- [ ] **Step 5: Implement `raspi_pci_bus_to_phys()` for real**

Replace the function body (currently lines 415-426):

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

    return PCI_BACKEND_UNMAPPABLE;
}
```

with:

```c
static LONG raspi_pci_bus_to_phys(ULONG bus_address, BOOL io, ULONG *phys_address)
{
    if (phys_address == 0)
        return PCI_GENERAL_ERROR;
    if (io)
        return PCI_BAD_RESOURCE;
    if (!raspi_pci_outbound_window_enabled)
        return PCI_BACKEND_UNMAPPABLE;
    if ((bus_address < RASPI_PCIE_MMIO_BUS_BASE) ||
        (bus_address >= RASPI_PCIE_MMIO_BUS_BASE + RASPI_PCIE_MMIO_SIZE))
        return PCI_BAD_RESOURCE;

    *phys_address = RASPI_PCIE_OUTBOUND_CPU_BASE + (bus_address - RASPI_PCIE_MMIO_BUS_BASE);
    return PCI_SUCCESSFUL;
}
```

(The existing range check moves after the `raspi_pci_outbound_window_enabled`
check — matching the design doc's stated priority: a disabled window
always fails, regardless of whether the address itself would otherwise be
in range. The null-pointer and I/O checks stay first, unchanged.)

- [ ] **Step 6: Build**

Run: `gmake rpi4_defconfig && gmake -j4 2>&1 | grep -iE "raspi_pci|error"`
Expected: `obj/raspi_pci.o` compiles with no warnings and no errors.

- [ ] **Step 7: gitready**

Run: `gmake gitready`
Expected: `gitready checks passed.`

- [ ] **Step 8: Commit**

```bash
git add bios/machine/raspi/raspi_pci.c
git commit -m "bios/raspi: relocate PCIe outbound window into 32-bit space

The outbound (CPU -> PCIe) MMIO window's CPU-side base was programmed
at 0x6_00000000, above the 4 GiB boundary a 32-bit ARM address can
express, making raspi_pci_bus_to_phys() unconditionally unmappable.
Relocate it to a fixed 32-bit address (0xf9000000) that the existing
flat identity map already covers, guarded by a boot-time check against
detected RAM (phystop) rather than an unverified assumption -- on
failure, only the outbound window is skipped; config-space access,
INTx routing, and the already-correct inbound window are unaffected.
bus_to_phys() now returns a real translation when the window is
enabled.

Fixes #276."
```

---

### Task 2: Implement `phys_to_bus()` for real

**Files:**
- Modify: `bios/machine/raspi/raspi_pci.c`

**Interfaces:**
- Consumes: `RASPI_PCIE_DMA_BUS_BASE`, `RASPI_PCIE_INBOUND_SIZE` (both pre-existing, unchanged since before this plan).
- Produces: a real `raspi_pci_phys_to_bus()` — no other code in this file calls it yet (a future DMA-buffer-address consumer will be the first caller); this task's own build/regression checks are its only verification.

- [ ] **Step 1: Implement `raspi_pci_phys_to_bus()` for real**

Replace the function body (currently lines 428-436):

```c
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

with:

```c
static LONG raspi_pci_phys_to_bus(ULONG phys_address, BOOL io, ULONG *bus_address)
{
    if (bus_address == 0)
        return PCI_GENERAL_ERROR;
    if (io)
        return PCI_BAD_RESOURCE;
    if ((phys_address < RASPI_PCIE_DMA_BUS_BASE) ||
        (phys_address >= RASPI_PCIE_DMA_BUS_BASE + RASPI_PCIE_INBOUND_SIZE))
        return PCI_BAD_RESOURCE;

    *bus_address = phys_address;
    return PCI_SUCCESSFUL;
}
```

This is independent of Task 1 and of the outbound-window safety check:
the inbound window (`raspi_pci_set_inbound_window()`, unchanged by this
plan) maps PCIe bus `0x0` for `RASPI_PCIE_INBOUND_SIZE` (2 GiB) straight
onto CPU physical `0x0` — a 1:1 identity mapping already fully within
32-bit reach, so `*bus_address = phys_address` is the correct translation
for any `phys_address` inside that range.

- [ ] **Step 2: Build**

Run: `gmake rpi4_defconfig && gmake -j4 2>&1 | grep -iE "raspi_pci|error"`
Expected: `obj/raspi_pci.o` compiles with no warnings and no errors.

- [ ] **Step 3: Build all three configs (regression)**

Run: `gmake distclean; gmake rpi4_defconfig && gmake -j4 2>&1 | tail -10`
Expected: kernel image (`kernel7l.img`) is ready, no new warnings.

Run: `gmake distclean; gmake rpi2_defconfig && gmake -j4 2>&1 | tail -10`
Expected: builds successfully (regression check — `raspi_pci.c` isn't
compiled into this config at all, gated on `TARGET_RPI4`).

Run: `gmake distclean; gmake virt-arm_defconfig && gmake -j4 2>&1 | tail -10`
Expected: builds successfully (same regression reasoning).

- [ ] **Step 4: gitready**

Run: `gmake gitready`
Expected: `gitready checks passed.`

- [ ] **Step 5: Commit**

```bash
git add bios/machine/raspi/raspi_pci.c
git commit -m "bios/raspi: implement PCIe phys_to_bus() for real

The inbound (PCIe -> CPU) window was already correctly configured --
PCIe bus 0x0 mapped straight onto CPU physical 0x0 for 2 GiB, a 1:1
identity mapping already fully 32-bit reachable -- but
raspi_pci_phys_to_bus() discarded its input and always failed anyway.
Implement the real range check plus identity passthrough. Independent
of the outbound-window relocation: this direction was never blocked
by the >4 GiB problem.

Fixes #276."
```

- [ ] **Step 6: Push and report for hardware testing**

```bash
git push
```

Then report: build is green on rpi4/rpi2/virt-arm. Ask the project owner
to boot on real Raspberry Pi 4/400 hardware and check the serial/KDEBUG
log for:
- No `pci: detected RAM reaches the PCIe outbound MMIO window` line (if
  it appears, the boot-time safety check tripped — expected, correct
  behavior per the design doc, not a bug to silently work around).
- `VL805/xHCI: MMIO 0x... size 0x... irq ...` (from #270's
  `raspi_vl805_get_resources()`/`xhci_lowlevel_init()`) instead of
  `VL805/xHCI: PCI BAR0 is not usable yet` — this is the concrete signal
  that `bus_to_phys()` now works and #270's bring-up code can run.
- `xhci: controller reset complete` / `xhci: controller running` / the
  port trace lines following it (from #270), now reachable for the first
  time.

---

## Self-Review

**Spec coverage:** Every Components subsection in the design doc (outbound
window CPU-side base, boot-time safety check, `bus_to_phys()`,
`phys_to_bus()`) has a task step. The design's Non-Goals (no LPAE, no
window-size or bus-side-target change, no inbound-window change, no
config-space-access change) are respected — no task touches
`raspi_pci_set_inbound_window()`'s body, `RASPI_PCIE_MMIO_SIZE`,
`RASPI_PCIE_MMIO_BUS_BASE`, or `raspi_pci_config_ptr()`. The Testing
section's build matrix (rpi4/rpi2/virt-arm) is covered in Task 2, and the
manual hardware checklist is handed off in Task 2 Step 6.

**Placeholder scan:** No step says "add error handling" or "similar to
Task N" without code — every step has complete, real code taken directly
from the actual current file content (verified by reading the file before
writing this plan, not reconstructed from memory).

**Type consistency:** `raspi_pci_outbound_window_enabled` is declared once
in Task 1 Step 2 and referenced with the same name/type in Task 1 Steps 4
and 5. `RASPI_PCIE_OUTBOUND_CPU_BASE` is defined once in Task 1 Step 1 and
used identically in Steps 3-5. Task 2 introduces no new names and doesn't
depend on anything Task 1 produces, matching the Interfaces section's
statement that the two tasks are independent.
