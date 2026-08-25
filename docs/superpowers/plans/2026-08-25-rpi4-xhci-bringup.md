# RPi4 xHCI Controller Bring-Up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `usb/ucd_xhci.c`'s `xhci_lowlevel_init()` stub (currently unconditional `EOPNOTSUPP`) with real VL805 xHCI controller bring-up: reset, Device Context Base Address Array (DCBAA), Command Ring, Event Ring + Event Ring Segment Table (ERST), scratchpad buffers, start, and root hub port status tracing.

**Architecture:** All new register-layout constants and the TRB struct live in a new header, `usb/xhci_hw.h` (pure definitions, no logic — matches how `usb/ucd_dwc2.h` sits alongside `usb/ucd_dwc2.c`). All bring-up logic is added directly to `usb/ucd_xhci.c` as new static helper functions called in sequence from `xhci_lowlevel_init()`, mirroring the exact call order verified against U-Boot's public xHCI driver (`xhci_reset()` → `xhci_mem_init()` → `xhci_start()`). `SUBMIT_CONTROL_MSG`/`SUBMIT_BULK_MSG`/`SUBMIT_INT_MSG` are untouched — they keep returning `EOPNOTSUPP`, so device enumeration past the root hub will still fail cleanly (verified safe: `usb/ucd.c:82-87`'s `usb_new_device()` failure path already tears down the device and returns cleanly; `usb/ucd_xhci.c:104-107`'s `ucd_register()` failure path already logs and returns — this is the exact same tolerated failure path the driver exercises today, just triggered slightly later).

**Tech Stack:** C90/GNU extensions (`-std=gnu90`), `portab.h` types (`ULONG`/`UWORD`/`UBYTE`/`BOOL`), no libc. Bring-up sequence and register/TRB layout cross-checked against U-Boot's `drivers/usb/host/xhci{.c,-mem.c}` and `include/usb/xhci.h` (fetched and inspected during design; not vendored into the tree).

**Spec:** `docs/superpowers/specs/2026-08-25-rpi4-xhci-bringup-design.md`

## Global Constraints

- C90 with GNU extensions (`-std=gnu90`); declarations at the top of each block.
- 4 spaces, never a hard tab. Run `make gitready` before every commit.
- Use `portab.h` types (`ULONG`, `UWORD`, `UBYTE`, `BOOL`) — never bare `int`/`unsigned`/`long` in new code.
- Trace with `KINFO(())`/`KDEBUG(())` (already `#define ENABLE_KDEBUG`d at the top of `ucd_xhci.c`), never a private printf.
- No interrupt handling and no `pci_hook_interrupt()` usage in this stage (per spec Non-Goals) — the Event Ring is polled only when this stage explicitly reads it, which it never does (no commands are ever queued yet, so nothing to poll for completion).
- No dynamic allocation — all DMA-capable buffers are static, cacheline-aligned arrays via `usb/usb_io.h`'s `DEFINE_ALIGN_BUFFER` macro, matching `usb/ucd_dwc2.c`'s existing convention.
- Every controller-visible buffer write is followed by `flush_data_cache()` (`include/biosext.h`) before the corresponding register write that hands it to hardware.
- `make rpi4_defconfig && make` must succeed with no new warnings after every task. `make rpi2_defconfig && make` and `make virt-arm_defconfig && make` (regression) must still succeed — checked once, in the final task, since no task touches any file those configs build.
- No automated functional test exists for this code (QEMU has no VL805/xHCI model). Each task's test cycle is: build clean, `make gitready` clean, and — where the step content is inherently reviewable that way — the step's own commit message/comment cites which reference-verified detail it implements (register offset, bit position, sequencing rule) so a reviewer can check it against the spec doc without re-deriving it.

---

## File Structure

- **Create `usb/xhci_hw.h`**: capability/operational/runtime register offset macros, `USBCMD`/`USBSTS`/`CONFIG`/`PORTSC`/`HCSPARAMS1`/`HCSPARAMS2` bit-field macros, the 16-byte TRB struct (`xhci_trb_t`), the 64-bit DMA-pointer struct (`xhci_qword_t`), the ERST entry struct (`xhci_erst_entry_t`), and the fixed sizing constants (`XHCI_TRBS_PER_SEGMENT`, `XHCI_MAX_SLOTS_ENABLED`, `XHCI_MAX_PORTS_TRACED`, `XHCI_MAX_SCRATCHPAD_BUFS`, `XHCI_DMA_ALIGN`, `XHCI_PAGE_SIZE`, the two timeout constants). No logic, no register access — plain data-layout definitions, matching `usb/ucd_dwc2.h`'s role for the DWC2 driver.
- **Modify `usb/ucd_xhci.c`**: add register-access helpers (`xhci_readb`/`xhci_readl`/`xhci_writel`/`xhci_writeq`), the handshake-wait helpers, the static DMA buffers (DCBAA, Command Ring, Event Ring, ERST, scratchpad array + buffers), and the bring-up helper functions (`xhci_hw_reset`, `xhci_configure_slots`, `xhci_init_dcbaa`, `xhci_init_command_ring`, `xhci_init_event_ring`, `xhci_init_scratchpad`, `xhci_hw_start`, `xhci_trace_ports`), then rewrite `xhci_lowlevel_init()` to call them in sequence. `struct xhci_priv` gains three new fields (`cap_base`, `op_base`, `rt_base`) and two trace fields (`max_slots`, `max_ports`, `slots_enabled`).

## Interfaces

- `usb/xhci_hw.h` produces: `xhci_trb_t`, `xhci_qword_t`, `xhci_erst_entry_t` (struct types), all `XHCI_CAP_*`/`XHCI_OP_*`/`XHCI_RT_*` offset macros, all `XHCI_CMD_*`/`XHCI_STS_*`/`XHCI_HCS1_*`/`XHCI_HCS2_*`/`XHCI_PORTSC_*`/`XHCI_TRB_*` bit macros, and the sizing/timeout constants listed above. `usb/ucd_xhci.c` consumes all of these via `#include "xhci_hw.h"`.
- `usb/ucd_xhci.c`'s new static helpers consume `struct xhci_priv *priv` (with `cap_base`/`op_base`/`rt_base` already populated by the time any helper past `xhci_hw_reset` runs) and produce no return value except where noted (`xhci_hw_reset`, `xhci_hw_start`, `xhci_init_scratchpad` return `BOOL`; the rest return `void`).
- Already-existing, unchanged: `raspi_vl805_resources_t` (`bios/machine/raspi/raspi_vl805.h`: `ULONG mmio_base`, `ULONG mmio_size`, `UWORD irq`), `raspi_vl805_get_resources()`, `raspi_delay_us(ULONG us)` (`bios/raspi_int.h`), `flush_data_cache(void *start, long size)`/`invalidate_data_cache(void *start, long size)` (`include/biosext.h`), `EOPNOTSUPP`/`ETIMEDOUT`/`E_OK` (`include/gemerror.h`), `DEFINE_ALIGN_BUFFER(type, name, size, align)` (`usb/usb_io.h`), `le2cpu32`/`cpu2le32` (`include/endian.h`).

---

### Task 1: xHCI register/TRB header

**Files:**
- Create: `usb/xhci_hw.h`
- Modify: `usb/ucd_xhci.c:1-14` (add `#include "xhci_hw.h"` and a compile-time TRB-size check)

**Interfaces:**
- Produces: everything listed in "Interfaces" above under `usb/xhci_hw.h`.
- Consumes: `portab.h` types only.

- [ ] **Step 1: Write `usb/xhci_hw.h`**

```c
/*
 * xhci_hw.h - xHCI register layout and TRB definitions (BCM2711/VL805)
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef XHCI_HW_H
#define XHCI_HW_H

#include "portab.h"

/* Capability registers (from the controller's BAR base, read-only) */
#define XHCI_CAP_CAPLENGTH   0x00U   /* UBYTE */
#define XHCI_CAP_HCSPARAMS1  0x04U   /* ULONG */
#define XHCI_CAP_HCSPARAMS2  0x08U   /* ULONG */
#define XHCI_CAP_HCSPARAMS3  0x0cU   /* ULONG */
#define XHCI_CAP_HCCPARAMS1  0x10U   /* ULONG */
#define XHCI_CAP_DBOFF       0x14U   /* ULONG */
#define XHCI_CAP_RTSOFF      0x18U   /* ULONG */

/* HCSPARAMS1 fields */
#define XHCI_HCS1_MAX_SLOTS(p)   ((p) & 0xffUL)
#define XHCI_HCS1_MAX_PORTS(p)   (((p) >> 24) & 0xffUL)

/* HCSPARAMS2 Max Scratchpad Buffers: hi 5 bits at 21:25, lo 5 bits at 27:31 */
#define XHCI_HCS2_MAX_SCRATCHPAD(p) \
    ((((p) >> 16) & 0x3e0UL) | (((p) >> 27) & 0x1fUL))

/* Operational registers (from base + CAPLENGTH) */
#define XHCI_OP_USBCMD    0x00U   /* ULONG */
#define XHCI_OP_USBSTS    0x04U   /* ULONG */
#define XHCI_OP_PAGESIZE  0x08U   /* ULONG */
#define XHCI_OP_DNCTRL    0x14U   /* ULONG */
#define XHCI_OP_CRCR      0x18U   /* 64-bit: lo at +0x18, hi at +0x1c */
#define XHCI_OP_DCBAAP    0x30U   /* 64-bit: lo at +0x30, hi at +0x34 */
#define XHCI_OP_CONFIG    0x38U   /* ULONG */
/* Port register sets start at +0x400, 16 bytes each; n is 0-based */
#define XHCI_OP_PORTSC(n) (0x400U + (0x10U * (n)))

/* USBCMD bits */
#define XHCI_CMD_RUN     0x00000001UL
#define XHCI_CMD_RESET   0x00000002UL

/* USBSTS bits */
#define XHCI_STS_HALT    0x00000001UL
#define XHCI_STS_CNR     0x00000800UL

/* PORTSC bits (read-only status subset needed for bring-up tracing) */
#define XHCI_PORTSC_CCS          0x00000001UL   /* Current Connect Status */
#define XHCI_PORTSC_PED          0x00000002UL   /* Port Enabled/Disabled */
#define XHCI_PORTSC_SPEED_SHIFT  10U
#define XHCI_PORTSC_SPEED_MASK   (0xfUL << XHCI_PORTSC_SPEED_SHIFT)

/* Runtime registers (from base + RTSOFF), Interrupter Register Set 0 at +0x20 */
#define XHCI_RT_IR0_IMAN    0x20U   /* ULONG */
#define XHCI_RT_IR0_IMOD    0x24U   /* ULONG */
#define XHCI_RT_IR0_ERSTSZ  0x28U   /* ULONG */
#define XHCI_RT_IR0_ERSTBA  0x30U   /* 64-bit: lo at +0x30, hi at +0x34 */
#define XHCI_RT_IR0_ERDP    0x38U   /* 64-bit: lo at +0x38, hi at +0x3c */

/* TRB: 16 bytes, 4 dwords. control bit 0 = Cycle, bits 10-15 = Type. */
typedef struct {
    ULONG param_lo;
    ULONG param_hi;
    ULONG status;
    ULONG control;
} xhci_trb_t;

#define XHCI_TRB_CYCLE          0x00000001UL
#define XHCI_TRB_LINK_TOGGLE    0x00000002UL
#define XHCI_TRB_TYPE_SHIFT     10U
#define XHCI_TRB_TYPE(t)        (((ULONG)(t)) << XHCI_TRB_TYPE_SHIFT)
#define XHCI_TRB_TYPE_LINK      6UL

/* A plain 64-bit DMA pointer stored in RAM (DCBAA entries, scratchpad array
 * entries) -- not a register, just memory the controller reads via DMA. */
typedef struct {
    ULONG lo;
    ULONG hi;
} xhci_qword_t;

/* One Event Ring Segment Table entry */
typedef struct {
    ULONG seg_addr_lo;
    ULONG seg_addr_hi;
    ULONG seg_size;
    ULONG rsvd;
} xhci_erst_entry_t;

#define XHCI_TRBS_PER_SEGMENT    64U
#define XHCI_MAX_SLOTS_ENABLED   8U
#define XHCI_MAX_PORTS_TRACED    8U
#define XHCI_MAX_SCRATCHPAD_BUFS 8U
#define XHCI_DMA_ALIGN           128U
#define XHCI_PAGE_SIZE           4096UL

/* Handshake timeouts, in microseconds -- exact values verified against a
 * real xHCI driver (U-Boot's XHCI_MAX_HALT_USEC/XHCI_MAX_RESET_USEC). */
#define XHCI_HALT_TIMEOUT_US     16000UL
#define XHCI_RESET_TIMEOUT_US    250000UL

#endif /* XHCI_HW_H */
```

- [ ] **Step 2: Add the include and a compile-time TRB-size check to `usb/ucd_xhci.c`**

In `usb/ucd_xhci.c`, add the include after the existing includes (after line 14, `#include "ucd_xhci.h"`), and add a file-scope compile-time size assertion right after it — a plain C90-compatible trick (a `typedef` of a negative-length array is a compile error if the condition is false), so a struct-layout mistake fails the build immediately instead of silently producing a wrong-sized TRB:

```c
#include "xhci_hw.h"

typedef char xhci_trb_size_check[(sizeof(xhci_trb_t) == 16U) ? 1 : -1];
typedef char xhci_qword_size_check[(sizeof(xhci_qword_t) == 8U) ? 1 : -1];
typedef char xhci_erst_entry_size_check[(sizeof(xhci_erst_entry_t) == 16U) ? 1 : -1];
```

- [ ] **Step 3: Build**

Run: `gmake rpi4_defconfig && gmake -j4 2>&1 | grep -i "ucd_xhci\|xhci_hw"`
Expected: `obj/ucd_xhci.o` compiles with no warnings and no errors. The three `typedef char ...[-1]` lines would fail compilation if any struct were the wrong size — if the build fails here, the struct layout is wrong, not the test.

- [ ] **Step 4: gitready**

Run: `gmake gitready`
Expected: `gitready checks passed.`

- [ ] **Step 5: Commit**

```bash
git add usb/xhci_hw.h usb/ucd_xhci.c
git commit -m "usb: add xHCI register/TRB layout header

Pure data-layout definitions for the VL805 xHCI controller's
capability/operational/runtime registers and TRB format, cross-checked
against U-Boot's public xhci.h. No logic yet -- xhci_lowlevel_init()
still returns EOPNOTSUPP unconditionally.

Part of #270."
```

---

### Task 2: Register mapping and controller reset

**Files:**
- Modify: `usb/ucd_xhci.c` (the `struct xhci_priv` definition, new static helpers, `xhci_lowlevel_init()`)

**Interfaces:**
- Consumes: `xhci_trb_t`/`xhci_qword_t`/`XHCI_CAP_*`/`XHCI_OP_*`/`XHCI_CMD_*`/`XHCI_STS_*`/`XHCI_HALT_TIMEOUT_US`/`XHCI_RESET_TIMEOUT_US` from Task 1's header. `raspi_delay_us(ULONG)` (already declared via the existing `#include "raspi_int.h"` — see Step 1, this task adds that include).
- Produces: `xhci_readb()`, `xhci_readl()`, `xhci_writel()`, `xhci_writeq()`, `xhci_wait_clear()`, `xhci_wait_set()`, `xhci_hw_reset()` — all `static`, all consumed by later tasks in this same file.

- [ ] **Step 1: Add includes and extend `struct xhci_priv`**

In `usb/ucd_xhci.c`, add `#include "raspi_int.h"` (needed for `raspi_delay_us`) and `#include "endian.h"` (needed for `le2cpu32`/`cpu2le32`, used by this task's register-access helpers) alongside the existing includes. `endian.h` is already transitively available via `usb_global.h`, but this project includes what it uses directly rather than relying on transitive includes. Replace the existing `struct xhci_priv` (currently lines 16-19):

```c
struct xhci_priv {
    raspi_vl805_resources_t resources;
    BOOL have_resources;
    volatile UBYTE *cap_base;
    volatile UBYTE *op_base;
    volatile UBYTE *rt_base;
    UWORD max_slots;
    UWORD max_ports;
    UWORD slots_enabled;
};
```

- [ ] **Step 2: Add register-access and handshake helpers**

Add these `static` functions above `xhci_lowlevel_init()` (which currently starts at line 61):

```c
static volatile ULONG *xhci_reg32(volatile UBYTE *base, ULONG offset)
{
    return (volatile ULONG *)(base + offset);
}

static UBYTE xhci_readb(volatile UBYTE *base, ULONG offset)
{
    return *(base + offset);
}

static ULONG xhci_readl(volatile UBYTE *base, ULONG offset)
{
    return le2cpu32(*xhci_reg32(base, offset));
}

static void xhci_writel(volatile UBYTE *base, ULONG offset, ULONG value)
{
    *xhci_reg32(base, offset) = cpu2le32(value);
}

/* 64-bit registers: write the low dword first, then the high dword --
 * per the xHCI spec, write order is irrelevant on implementations that
 * ignore the high dword, and mandatory (low first) on ones that don't. */
static void xhci_writeq(volatile UBYTE *base, ULONG offset, ULONG addr_lo)
{
    xhci_writel(base, offset, addr_lo);
    xhci_writel(base, offset + 4UL, 0UL);
}

static BOOL xhci_wait_clear(volatile UBYTE *base, ULONG offset, ULONG mask, ULONG timeout_us)
{
    ULONG waited;

    waited = 0UL;
    while (xhci_readl(base, offset) & mask) {
        if (waited >= timeout_us)
            return FALSE;
        raspi_delay_us(10UL);
        waited += 10UL;
    }
    return TRUE;
}

static BOOL xhci_wait_set(volatile UBYTE *base, ULONG offset, ULONG mask, ULONG timeout_us)
{
    ULONG waited;

    waited = 0UL;
    while (!(xhci_readl(base, offset) & mask)) {
        if (waited >= timeout_us)
            return FALSE;
        raspi_delay_us(10UL);
        waited += 10UL;
    }
    return TRUE;
}

/*
 * Reset sequence per xHCI spec section 4.2, verified against U-Boot's
 * xhci_reset(): halt if running, then reset, then wait for CNR to clear.
 * No doorbell or operational register other than USBSTS may be touched
 * before CNR clears.
 */
static BOOL xhci_hw_reset(struct xhci_priv *priv)
{
    ULONG cmd;

    if (!(xhci_readl(priv->op_base, XHCI_OP_USBSTS) & XHCI_STS_HALT)) {
        cmd = xhci_readl(priv->op_base, XHCI_OP_USBCMD);
        cmd &= ~XHCI_CMD_RUN;
        xhci_writel(priv->op_base, XHCI_OP_USBCMD, cmd);
    }

    if (!xhci_wait_set(priv->op_base, XHCI_OP_USBSTS, XHCI_STS_HALT, XHCI_HALT_TIMEOUT_US)) {
        KINFO(("xhci: timed out waiting for HALT before reset\n"));
        return FALSE;
    }

    cmd = xhci_readl(priv->op_base, XHCI_OP_USBCMD);
    cmd |= XHCI_CMD_RESET;
    xhci_writel(priv->op_base, XHCI_OP_USBCMD, cmd);

    if (!xhci_wait_clear(priv->op_base, XHCI_OP_USBCMD, XHCI_CMD_RESET, XHCI_RESET_TIMEOUT_US)) {
        KINFO(("xhci: timed out waiting for RESET to self-clear\n"));
        return FALSE;
    }

    if (!xhci_wait_clear(priv->op_base, XHCI_OP_USBSTS, XHCI_STS_CNR, XHCI_RESET_TIMEOUT_US)) {
        KINFO(("xhci: timed out waiting for CNR to clear\n"));
        return FALSE;
    }

    return TRUE;
}
```

- [ ] **Step 3: Rewrite `xhci_lowlevel_init()` to map registers and reset**

Replace the existing `xhci_lowlevel_init()` body (currently lines 61-75):

```c
static long xhci_lowlevel_init(struct xhci_priv *priv)
{
    UBYTE caplength;
    ULONG rtsoff;

    priv->have_resources = raspi_vl805_get_resources(&priv->resources);
    if (!priv->have_resources) {
        KINFO(("xhci: VL805 controller not available\n"));
        return EOPNOTSUPP;
    }

    KINFO(("xhci: MMIO 0x%lx size 0x%lx irq %u\n",
           priv->resources.mmio_base,
           priv->resources.mmio_size,
           priv->resources.irq));

    priv->cap_base = (volatile UBYTE *)priv->resources.mmio_base;
    caplength = xhci_readb(priv->cap_base, XHCI_CAP_CAPLENGTH);
    priv->op_base = priv->cap_base + caplength;

    rtsoff = xhci_readl(priv->cap_base, XHCI_CAP_RTSOFF) & ~0x1fUL;
    priv->rt_base = priv->cap_base + rtsoff;

    if (!xhci_hw_reset(priv)) {
        return ETIMEDOUT;
    }
    KINFO(("xhci: controller reset complete\n"));

    KINFO(("xhci: controller bring-up is not implemented yet\n"));
    return EOPNOTSUPP;
}
```

- [ ] **Step 4: Build**

Run: `gmake rpi4_defconfig && gmake -j4 2>&1 | grep -i "ucd_xhci"`
Expected: `obj/ucd_xhci.o` compiles with no warnings and no errors.

- [ ] **Step 5: gitready**

Run: `gmake gitready`
Expected: `gitready checks passed.`

- [ ] **Step 6: Commit**

```bash
git add usb/ucd_xhci.c
git commit -m "usb/xhci: map registers and reset the controller

xhci_lowlevel_init() now locates the operational and runtime register
blocks from CAPLENGTH/RTSOFF and runs the reset handshake (halt, then
HCRST, then wait for CNR to clear). Still returns EOPNOTSUPP after --
DCBAA/rings/start come in later tasks.

Part of #270."
```

---

### Task 3: DCBAA, device slot configuration, and Command Ring

**Files:**
- Modify: `usb/ucd_xhci.c`

**Interfaces:**
- Consumes: `xhci_qword_t`, `xhci_trb_t`, `XHCI_HCS1_MAX_SLOTS`, `XHCI_MAX_SLOTS_ENABLED`, `XHCI_TRBS_PER_SEGMENT`, `XHCI_DMA_ALIGN`, `XHCI_TRB_TYPE`/`XHCI_TRB_TYPE_LINK`/`XHCI_TRB_LINK_TOGGLE`, `XHCI_OP_CONFIG`, `XHCI_OP_CRCR` from Task 1; `xhci_readl`/`xhci_writel`/`xhci_writeq` from Task 2.
- Produces: `xhci_dcbaa` (static `xhci_qword_t *`), `xhci_cmd_ring` (static `xhci_trb_t *`), `xhci_configure_slots()`, `xhci_init_dcbaa()`, `xhci_init_command_ring()` — consumed by Task 6's `xhci_lowlevel_init()` rewrite.

- [ ] **Step 1: Add the `usb_io.h` include, static DMA buffers, and helpers**

Add `#include "usb_io.h"` to `usb/ucd_xhci.c`'s includes (needed for `DEFINE_ALIGN_BUFFER`, used below and by every later task that allocates a buffer) and `#include "biosext.h"` (needed for `flush_data_cache`, also used below).

Add above `xhci_lowlevel_init()`:

```c
DEFINE_ALIGN_BUFFER(xhci_qword_t, xhci_dcbaa, XHCI_MAX_SLOTS_ENABLED + 1U, XHCI_DMA_ALIGN);
DEFINE_ALIGN_BUFFER(xhci_trb_t, xhci_cmd_ring, XHCI_TRBS_PER_SEGMENT, XHCI_DMA_ALIGN);

/* CONFIG.MaxSlotsEn is capped at XHCI_MAX_SLOTS_ENABLED regardless of what
 * the hardware reports, so the statically-sized DCBAA/context tables never
 * need to grow at runtime -- the spec permits enabling fewer slots than
 * the hardware maximum. */
static void xhci_configure_slots(struct xhci_priv *priv)
{
    ULONG hcs1;
    ULONG hw_max_slots;

    hcs1 = xhci_readl(priv->cap_base, XHCI_CAP_HCSPARAMS1);
    hw_max_slots = XHCI_HCS1_MAX_SLOTS(hcs1);
    priv->max_slots = (UWORD)hw_max_slots;
    priv->slots_enabled = (hw_max_slots < (ULONG)XHCI_MAX_SLOTS_ENABLED)
        ? (UWORD)hw_max_slots
        : (UWORD)XHCI_MAX_SLOTS_ENABLED;

    xhci_writel(priv->op_base, XHCI_OP_CONFIG, (ULONG)priv->slots_enabled);
}

static void xhci_init_dcbaa(void)
{
    ULONG i;

    for (i = 0UL; i < (ULONG)(XHCI_MAX_SLOTS_ENABLED + 1U); i++) {
        xhci_dcbaa[i].lo = 0UL;
        xhci_dcbaa[i].hi = 0UL;
    }
    flush_data_cache((void *)xhci_dcbaa,
                      (long)((ULONG)(XHCI_MAX_SLOTS_ENABLED + 1U) * sizeof(xhci_qword_t)));
}

/*
 * Single-segment Command Ring, closed into a loop by a Link TRB. Per the
 * xHCI spec (4.11.1.1, "All components of all Command and Transfer TRBs
 * shall be initialized to 0") and verified against U-Boot's
 * xhci_link_segments(): the Link TRB's own Cycle bit stays 0 at init --
 * only its Type field and the Toggle Cycle control bit are set. The
 * ring's initial producer cycle state (1) is written separately, into
 * CRCR itself, not into any TRB.
 */
static void xhci_init_command_ring(struct xhci_priv *priv)
{
    ULONG i;
    ULONG addr;
    ULONG last;

    last = (ULONG)(XHCI_TRBS_PER_SEGMENT - 1U);
    for (i = 0UL; i < last; i++) {
        xhci_cmd_ring[i].param_lo = 0UL;
        xhci_cmd_ring[i].param_hi = 0UL;
        xhci_cmd_ring[i].status = 0UL;
        xhci_cmd_ring[i].control = 0UL;
    }

    addr = (ULONG)xhci_cmd_ring;
    xhci_cmd_ring[last].param_lo = addr;
    xhci_cmd_ring[last].param_hi = 0UL;
    xhci_cmd_ring[last].status = 0UL;
    xhci_cmd_ring[last].control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_LINK) | XHCI_TRB_LINK_TOGGLE;

    flush_data_cache((void *)xhci_cmd_ring, (long)((ULONG)XHCI_TRBS_PER_SEGMENT * sizeof(xhci_trb_t)));

    /* CRCR low dword: 64-byte-aligned ring address with the initial Ring
     * Cycle State (1) OR'd into the low bits the alignment guarantees
     * are zero. */
    xhci_writeq(priv->op_base, XHCI_OP_CRCR, addr | 1UL);
}
```

- [ ] **Step 2: Call the new helpers from `xhci_lowlevel_init()`**

In `xhci_lowlevel_init()`, replace:

```c
    KINFO(("xhci: controller bring-up is not implemented yet\n"));
    return EOPNOTSUPP;
```

with:

```c
    xhci_configure_slots(priv);
    xhci_init_dcbaa();
    xhci_init_command_ring(priv);
    xhci_writeq(priv->op_base, XHCI_OP_DCBAAP, (ULONG)xhci_dcbaa);

    KINFO(("xhci: controller bring-up is not implemented yet\n"));
    return EOPNOTSUPP;
```

- [ ] **Step 3: Build**

Run: `gmake rpi4_defconfig && gmake -j4 2>&1 | grep -i "ucd_xhci"`
Expected: `obj/ucd_xhci.o` compiles with no warnings and no errors.

- [ ] **Step 4: gitready**

Run: `gmake gitready`
Expected: `gitready checks passed.`

- [ ] **Step 5: Commit**

```bash
git add usb/ucd_xhci.c
git commit -m "usb/xhci: program CONFIG, DCBAA, and the Command Ring

CONFIG.MaxSlotsEn is capped at XHCI_MAX_SLOTS_ENABLED so the static
DCBAA never needs runtime sizing. The Command Ring is a single
64-entry segment closed into a loop by a Link TRB, matching the exact
zero-init-except-Type-and-Toggle-Cycle behavior verified against
U-Boot's xhci_link_segments(). Still returns EOPNOTSUPP after --
Event Ring and start come in later tasks.

Part of #270."
```

---

### Task 4: Event Ring and ERST

**Files:**
- Modify: `usb/ucd_xhci.c`

**Interfaces:**
- Consumes: `xhci_trb_t`, `xhci_erst_entry_t`, `XHCI_TRBS_PER_SEGMENT`, `XHCI_DMA_ALIGN`, `XHCI_RT_IR0_ERDP`/`XHCI_RT_IR0_ERSTSZ`/`XHCI_RT_IR0_ERSTBA` from Task 1; `xhci_writel`/`xhci_writeq` from Task 2.
- Produces: `xhci_event_ring` (static `xhci_trb_t *`), `xhci_erst` (static `xhci_erst_entry_t *`), `xhci_init_event_ring()` — consumed by Task 6.

- [ ] **Step 1: Add the static buffers and the init function**

Add above `xhci_lowlevel_init()`:

```c
DEFINE_ALIGN_BUFFER(xhci_trb_t, xhci_event_ring, XHCI_TRBS_PER_SEGMENT, XHCI_DMA_ALIGN);
DEFINE_ALIGN_BUFFER(xhci_erst_entry_t, xhci_erst, 1U, XHCI_DMA_ALIGN);

/*
 * Single-segment Event Ring. Unlike the Command Ring, the Event Ring has
 * no Link TRB -- the hardware walks segments through the ERST, not
 * in-ring links (verified: U-Boot's xhci_ring_alloc() call for the event
 * ring passes link_trbs=false). Write order matters: ERDP and ERSTSZ
 * must be valid before ERSTBA is written, since writing ERSTBA arms the
 * ring.
 */
static void xhci_init_event_ring(struct xhci_priv *priv)
{
    ULONG i;
    ULONG addr;

    for (i = 0UL; i < (ULONG)XHCI_TRBS_PER_SEGMENT; i++) {
        xhci_event_ring[i].param_lo = 0UL;
        xhci_event_ring[i].param_hi = 0UL;
        xhci_event_ring[i].status = 0UL;
        xhci_event_ring[i].control = 0UL;
    }
    flush_data_cache((void *)xhci_event_ring,
                      (long)((ULONG)XHCI_TRBS_PER_SEGMENT * sizeof(xhci_trb_t)));

    addr = (ULONG)xhci_event_ring;
    xhci_erst[0].seg_addr_lo = addr;
    xhci_erst[0].seg_addr_hi = 0UL;
    xhci_erst[0].seg_size = (ULONG)XHCI_TRBS_PER_SEGMENT;
    xhci_erst[0].rsvd = 0UL;
    flush_data_cache((void *)xhci_erst, (long)sizeof(xhci_erst_entry_t));

    xhci_writeq(priv->rt_base, XHCI_RT_IR0_ERDP, addr);
    xhci_writel(priv->rt_base, XHCI_RT_IR0_ERSTSZ, 1UL);
    xhci_writeq(priv->rt_base, XHCI_RT_IR0_ERSTBA, (ULONG)xhci_erst);
}
```

- [ ] **Step 2: Call it from `xhci_lowlevel_init()`**

Replace:

```c
    xhci_writeq(priv->op_base, XHCI_OP_DCBAAP, (ULONG)xhci_dcbaa);

    KINFO(("xhci: controller bring-up is not implemented yet\n"));
    return EOPNOTSUPP;
```

with:

```c
    xhci_writeq(priv->op_base, XHCI_OP_DCBAAP, (ULONG)xhci_dcbaa);
    xhci_init_event_ring(priv);

    KINFO(("xhci: controller bring-up is not implemented yet\n"));
    return EOPNOTSUPP;
```

- [ ] **Step 3: Build**

Run: `gmake rpi4_defconfig && gmake -j4 2>&1 | grep -i "ucd_xhci"`
Expected: `obj/ucd_xhci.o` compiles with no warnings and no errors.

- [ ] **Step 4: gitready**

Run: `gmake gitready`
Expected: `gitready checks passed.`

- [ ] **Step 5: Commit**

```bash
git add usb/ucd_xhci.c
git commit -m "usb/xhci: program the Event Ring and ERST

Single-segment Event Ring with no Link TRB (segment traversal is
ERST-driven, not link-driven, for event rings). ERDP and ERSTSZ are
written before ERSTBA, since writing ERSTBA arms the ring. Still
returns EOPNOTSUPP after -- scratchpad, start, and port tracing come
in later tasks.

Part of #270."
```

---

### Task 5: Scratchpad buffers

**Files:**
- Modify: `usb/ucd_xhci.c`

**Interfaces:**
- Consumes: `xhci_qword_t`, `XHCI_HCS2_MAX_SCRATCHPAD`, `XHCI_MAX_SCRATCHPAD_BUFS`, `XHCI_PAGE_SIZE`, `XHCI_DMA_ALIGN`, `XHCI_OP_PAGESIZE`, `XHCI_CAP_HCSPARAMS2` from Task 1; `xhci_readl` from Task 2; `xhci_dcbaa` from Task 3.
- Produces: `xhci_scratchpad_array` (static `xhci_qword_t *`), `xhci_scratchpad_bufs` (static `UBYTE *`), `xhci_init_scratchpad()` (returns `BOOL`) — consumed by Task 6.

- [ ] **Step 1: Add the static buffers and the init function**

Add above `xhci_lowlevel_init()`:

```c
DEFINE_ALIGN_BUFFER(xhci_qword_t, xhci_scratchpad_array, XHCI_MAX_SCRATCHPAD_BUFS, XHCI_DMA_ALIGN);
DEFINE_ALIGN_BUFFER(UBYTE, xhci_scratchpad_bufs, XHCI_MAX_SCRATCHPAD_BUFS * XHCI_PAGE_SIZE, XHCI_DMA_ALIGN);

/*
 * HCSPARAMS2's Max Scratchpad Buffers field tells the driver how many
 * page-sized buffers the controller needs for internal use; if nonzero,
 * DCBAA[0] must point to an array of their addresses (xHCI spec 4.20,
 * verified against U-Boot's xhci_scratchpad_alloc()). PAGESIZE's lowest
 * set bit gives the actual page size as 4096 << bit_index -- this
 * driver's static buffers are sized for exactly 4096, so any other
 * reported page size is a clean failure rather than a silent
 * mis-sized allocation.
 */
static BOOL xhci_init_scratchpad(struct xhci_priv *priv)
{
    ULONG hcs2;
    ULONG num_sp;
    ULONG page_size_bits;
    ULONG page_size;
    ULONG i;
    ULONG addr;

    hcs2 = xhci_readl(priv->cap_base, XHCI_CAP_HCSPARAMS2);
    num_sp = XHCI_HCS2_MAX_SCRATCHPAD(hcs2);

    if (num_sp == 0UL) {
        return TRUE;
    }
    if (num_sp > (ULONG)XHCI_MAX_SCRATCHPAD_BUFS) {
        KINFO(("xhci: %lu scratchpad buffers required, only %lu supported\n",
               num_sp, (ULONG)XHCI_MAX_SCRATCHPAD_BUFS));
        return FALSE;
    }

    page_size_bits = xhci_readl(priv->op_base, XHCI_OP_PAGESIZE) & 0xffffUL;
    for (i = 0UL; i < 16UL; i++) {
        if (page_size_bits & 1UL)
            break;
        page_size_bits >>= 1;
    }
    if (i == 16UL) {
        KINFO(("xhci: PAGESIZE register reports no valid page size\n"));
        return FALSE;
    }
    page_size = 4096UL << i;
    if (page_size != XHCI_PAGE_SIZE) {
        KINFO(("xhci: unsupported hardware page size %lu (only %lu supported)\n",
               page_size, XHCI_PAGE_SIZE));
        return FALSE;
    }

    for (i = 0UL; i < num_sp; i++) {
        addr = (ULONG)(xhci_scratchpad_bufs + (i * XHCI_PAGE_SIZE));
        xhci_scratchpad_array[i].lo = addr;
        xhci_scratchpad_array[i].hi = 0UL;
    }
    flush_data_cache((void *)xhci_scratchpad_bufs, (long)(num_sp * XHCI_PAGE_SIZE));
    flush_data_cache((void *)xhci_scratchpad_array, (long)(num_sp * sizeof(xhci_qword_t)));

    xhci_dcbaa[0].lo = (ULONG)xhci_scratchpad_array;
    xhci_dcbaa[0].hi = 0UL;
    flush_data_cache((void *)&xhci_dcbaa[0], (long)sizeof(xhci_qword_t));

    return TRUE;
}
```

- [ ] **Step 2: Call it from `xhci_lowlevel_init()`**

Replace:

```c
    xhci_init_event_ring(priv);

    KINFO(("xhci: controller bring-up is not implemented yet\n"));
    return EOPNOTSUPP;
```

with:

```c
    xhci_init_event_ring(priv);

    if (!xhci_init_scratchpad(priv)) {
        return EOPNOTSUPP;
    }

    KINFO(("xhci: controller bring-up is not implemented yet\n"));
    return EOPNOTSUPP;
```

- [ ] **Step 3: Build**

Run: `gmake rpi4_defconfig && gmake -j4 2>&1 | grep -i "ucd_xhci"`
Expected: `obj/ucd_xhci.o` compiles with no warnings and no errors.

- [ ] **Step 4: gitready**

Run: `gmake gitready`
Expected: `gitready checks passed.`

- [ ] **Step 5: Commit**

```bash
git add usb/ucd_xhci.c
git commit -m "usb/xhci: allocate scratchpad buffers when required

Reads HCSPARAMS2's Max Scratchpad Buffers field and, if nonzero,
allocates that many static page-sized buffers plus a pointer array,
writing the array's address into DCBAA[0] per xHCI spec 4.20. Fails
cleanly (does not guess) if the hardware needs more buffers than the
fixed cap or reports a page size other than 4096 bytes.

Part of #270."
```

---

### Task 6: Start the controller and trace root hub ports

**Files:**
- Modify: `usb/ucd_xhci.c`

**Interfaces:**
- Consumes: `XHCI_CMD_RUN`, `XHCI_STS_HALT`, `XHCI_OP_DNCTRL`, `XHCI_HALT_TIMEOUT_US`, `XHCI_HCS1_MAX_PORTS`, `XHCI_MAX_PORTS_TRACED`, `XHCI_OP_PORTSC`, `XHCI_PORTSC_CCS`/`XHCI_PORTSC_PED`/`XHCI_PORTSC_SPEED_MASK`/`XHCI_PORTSC_SPEED_SHIFT` from Task 1; `xhci_readl`/`xhci_writel`/`xhci_wait_clear` from Task 2.
- Produces: `xhci_hw_start()` (returns `BOOL`), `xhci_trace_ports()` — this task's `xhci_lowlevel_init()` rewrite is the final one; it returns `E_OK` on success instead of `EOPNOTSUPP`.

- [ ] **Step 1: Add the start and port-tracing functions**

Add above `xhci_lowlevel_init()`:

```c
static BOOL xhci_hw_start(struct xhci_priv *priv)
{
    ULONG cmd;

    cmd = xhci_readl(priv->op_base, XHCI_OP_USBCMD);
    cmd |= XHCI_CMD_RUN;
    xhci_writel(priv->op_base, XHCI_OP_USBCMD, cmd);

    return xhci_wait_clear(priv->op_base, XHCI_OP_USBSTS, XHCI_STS_HALT, XHCI_HALT_TIMEOUT_US);
}

static void xhci_trace_ports(struct xhci_priv *priv)
{
    ULONG hcs1;
    ULONG hw_max_ports;
    ULONG n;
    ULONG port;
    ULONG value;
    ULONG speed;

    hcs1 = xhci_readl(priv->cap_base, XHCI_CAP_HCSPARAMS1);
    hw_max_ports = XHCI_HCS1_MAX_PORTS(hcs1);
    priv->max_ports = (UWORD)hw_max_ports;

    KINFO(("xhci: %lu root hub ports\n", hw_max_ports));

    n = (hw_max_ports < (ULONG)XHCI_MAX_PORTS_TRACED) ? hw_max_ports : (ULONG)XHCI_MAX_PORTS_TRACED;

    for (port = 0UL; port < n; port++) {
        value = xhci_readl(priv->op_base, XHCI_OP_PORTSC(port));
        speed = (value & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
        KINFO(("xhci: port %lu: connect=%lu enabled=%lu speed=%lu\n",
               port + 1UL,
               (value & XHCI_PORTSC_CCS) ? 1UL : 0UL,
               (value & XHCI_PORTSC_PED) ? 1UL : 0UL,
               speed));
    }
}
```

- [ ] **Step 2: Finish `xhci_lowlevel_init()`**

Replace:

```c
    if (!xhci_init_scratchpad(priv)) {
        return EOPNOTSUPP;
    }

    KINFO(("xhci: controller bring-up is not implemented yet\n"));
    return EOPNOTSUPP;
```

with:

```c
    if (!xhci_init_scratchpad(priv)) {
        return EOPNOTSUPP;
    }

    xhci_writel(priv->op_base, XHCI_OP_DNCTRL, 0UL);

    if (!xhci_hw_start(priv)) {
        KINFO(("xhci: controller did not start\n"));
        return ETIMEDOUT;
    }
    KINFO(("xhci: controller running\n"));

    xhci_trace_ports(priv);

    KINFO(("xhci: bring-up complete; transfer support is not implemented yet\n"));
    return E_OK;
```

Note: `xhci_ioctl()`'s `SUBMIT_CONTROL_MSG`/`SUBMIT_BULK_MSG`/`SUBMIT_INT_MSG` handling (currently lines 89-94) is **not** changed by this task. `LOWLEVEL_INIT` now returning `E_OK` means `ucd_register()` will proceed to call `usb_new_device()` for the root hub, which will fail at its first control transfer (still `EOPNOTSUPP`) and be torn down cleanly by the existing, already-exercised failure path in `usb/ucd.c:82-87` -- this is expected and matches the design doc's Data Flow section, not a regression.

- [ ] **Step 3: Build all three configs**

Run: `gmake distclean; gmake rpi4_defconfig && gmake -j4 2>&1 | tail -20`
Expected: `kernel8.img` (or the configured RPi4 image name) is ready, no new warnings from `ucd_xhci.c`.

Run: `gmake distclean; gmake rpi2_defconfig && gmake -j4 2>&1 | tail -10`
Expected: builds successfully (regression check -- `ucd_xhci.c` isn't compiled into this config at all, `CONF_WITH_USB_XHCI` depends on `TARGET_RPI4`).

Run: `gmake distclean; gmake virt-arm_defconfig && gmake -j4 2>&1 | tail -10`
Expected: builds successfully (same regression reasoning).

- [ ] **Step 4: gitready**

Run: `gmake gitready`
Expected: `gitready checks passed.`

- [ ] **Step 5: Commit**

```bash
git add usb/ucd_xhci.c
git commit -m "usb/xhci: start the controller and trace root hub ports

USBCMD.RUN is set and USBSTS.HALT is awaited; on success,
xhci_lowlevel_init() reads HCSPARAMS1's port count and traces each
port's connect/enable/speed via PORTSC, then returns E_OK (previously
unconditional EOPNOTSUPP). SUBMIT_CONTROL_MSG/SUBMIT_BULK_MSG/
SUBMIT_INT_MSG are unchanged and still fail with EOPNOTSUPP, so root
hub device enumeration will fail past this point until a later stage
implements control transfers -- ucd_register()'s existing failure path
(usb/ucd.c:82-87) already tears that down cleanly, so this is a later,
not new, failure point.

This completes RPi4 xHCI bring-up (stage 1 of #270): the controller
resets, initializes its DCBAA/Command Ring/Event Ring, and starts
running. Functional verification (does PORTSC actually reflect a
plugged-in device, do the handshakes complete instead of timing out)
requires real Raspberry Pi 4/400 hardware -- QEMU has no VL805/xHCI
model to test against.

Part of #270."
```

- [ ] **Step 6: Push and report for hardware testing**

```bash
git push
```

Then report to the project owner: build is green on rpi4/rpi2/virt-arm.

**Known blocking prerequisite (found during this stage's final review, not
introduced by this plan):** `raspi_pci_bus_to_phys()`
(`bios/machine/raspi/raspi_pci.c`) unconditionally returns
`PCI_BACKEND_UNMAPPABLE`, so `pci_decode_bar()` never populates a BAR's
resource and `raspi_vl805_get_resources()` always fails at
`pci_get_resource()` today, logging `VL805/xHCI: PCI BAR0 is not usable
yet`. None of this stage's bring-up code can run on real hardware until
that lands (tracked as #276). Check for that line first: if present,
this is the known prerequisite gap, not a bug in this branch -- do not
proceed to the checklist below until it's resolved.

Once the prerequisite is resolved, ask the project owner to boot the image
on real Raspberry Pi 4/400 hardware and check the serial/KDEBUG log for:
- `xhci: controller reset complete`
- `xhci: controller running`
- `xhci: N root hub ports` followed by one `xhci: port K: connect=.. enabled=.. speed=..` line per port, with `connect=1` and a plausible `speed` (1-4) on any port that has a device plugged in.
- No `xhci: timed out waiting for ...` lines.

If a handshake times out on real hardware, the fix is almost certainly a wrong register offset/bit or an insufficient timeout, not a design problem -- report the exact `KINFO` output back for diagnosis before changing anything speculatively.

---

## Self-Review

**Spec coverage:** Every "Bring-up sequence" step in the design doc (reset; CONFIG.MaxSlotsEn; DCBAA; Command Ring; Event Ring + ERST with write-order requirement; scratchpad; start; port trace) has a task. The design doc's "No interrupt handling" and "No transfer logic" Non-Goals are respected — no task touches `SUBMIT_*_MSG` or any interrupt/doorbell register. The "DMA-capable memory" convention (`DEFINE_ALIGN_BUFFER`, `flush_data_cache`) is used in every task that allocates a buffer. The Testing section's build matrix (rpi4/rpi2/virt-arm) is covered in Task 6, and the manual hardware checklist is handed off in Task 6 Step 6.

**Placeholder scan:** No task step says "add error handling" or "similar to Task N" without code — every step has complete, real code. No `TODO`/`TBD` anywhere in a code block (the two `KINFO` "bring-up is not implemented yet" lines in Tasks 2-5 are real, correct runtime behavior for that task's intermediate state, not planning placeholders — they're removed by Task 6).

**Type consistency:** `struct xhci_priv`'s fields (`cap_base`, `op_base`, `rt_base`, `max_slots`, `max_ports`, `slots_enabled`) are introduced in Task 2 and used with matching names/types in Tasks 3-6. All register-access helper names (`xhci_readb`/`xhci_readl`/`xhci_writel`/`xhci_writeq`/`xhci_wait_clear`/`xhci_wait_set`) are defined once in Task 2 and referenced identically afterward. All Task 1 macro/type names (`xhci_trb_t`, `xhci_qword_t`, `xhci_erst_entry_t`, every `XHCI_*` macro) are used with consistent spelling in every later task — checked against the header text in Task 1 Step 1.
