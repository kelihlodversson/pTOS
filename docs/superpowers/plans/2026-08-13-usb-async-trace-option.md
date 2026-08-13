# USB Async Trace Option Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in shared USB asynchronous trace macro so normal HID operation does not flood serial debug output.

**Architecture:** `usb/Kconfig` owns a disabled-by-default `CONF_DEBUG_USB_ASYNC` option. `usb/usb_global.h` exposes `KINFO_USB_ASYNC(args)`, which compiles to `KINFO(args)` when enabled and otherwise to a no-op. DWC2 uses it only for repeated transfer-progress messages.

**Tech Stack:** Kconfig, C90 GNU C, pTOS USB stack, shell contract test.

## Global Constraints

- `CONF_DEBUG_USB_ASYNC` depends on `CONF_WITH_USB` and defaults to `n`.
- The macro must be reusable by all USB host controllers.
- Only high-frequency async progress traces are suppressed by default.
- Errors, cancellation, shutdown, release, and initialization diagnostics remain `KINFO()`.
- C sources use four spaces and no hard tabs; generated configuration files are not edited.

---

### Task 1: Add the Shared USB Async Trace Gate

**Files:**
- Modify: `usb/Kconfig`
- Modify: `usb/usb.h`
- Modify: `tools/test-dwc2-async-contract.sh`

**Interfaces:**
- Produces: `CONF_DEBUG_USB_ASYNC`, always defined as `0` or `1` in C.
- Produces: `KINFO_USB_ASYNC(args)`, accepting the existing double-parenthesized `KINFO` argument convention.

- [ ] **Step 1: Add a failing contract assertion**

Add checks requiring the disabled Kconfig default and both macro branches:

```sh
require 'config CONF_DEBUG_USB_ASYNC' 'shared USB async trace option'
require 'default n' 'async trace disabled by default'
require_multiline '#if CONF_DEBUG_USB_ASYNC[\s\S]*#define KINFO_USB_ASYNC\(a\) KINFO\(a\)[\s\S]*#else[\s\S]*#define KINFO_USB_ASYNC\(a\) \(\(void\)0\)[\s\S]*#endif' \
    'compile-time USB async trace gate'
```

- [ ] **Step 2: Run the contract test and verify it fails**

Run: `sh tools/test-dwc2-async-contract.sh`

Expected: failure describing the missing shared USB async trace option.

- [ ] **Step 3: Add the minimal Kconfig symbol and shared macro**

Add to `usb/Kconfig` before `endmenu`:

```kconfig
config CONF_DEBUG_USB_ASYNC
    bool "Trace asynchronous USB transfers"
    depends on CONF_WITH_USB
    default n
    help
      Enable high-frequency progress traces from USB host-controller
      asynchronous transfers.  This is useful while diagnosing USB transfer
      scheduling, but can flood serial debug output during normal HID use.
```

Add to `usb/usb.h` after its includes:

```c
#if CONF_DEBUG_USB_ASYNC
#define KINFO_USB_ASYNC(a) KINFO(a)
#else
#define KINFO_USB_ASYNC(a) ((void)0)
#endif
```

- [ ] **Step 4: Run the contract test and verify it passes**

Run: `sh tools/test-dwc2-async-contract.sh`

Expected: `dwc2 async contract test passed`.

- [ ] **Step 5: Commit**

```bash
git add usb/Kconfig usb/usb.h tools/test-dwc2-async-contract.sh
git commit -m "Add USB async trace option"
```

### Task 2: Gate DWC2 High-Frequency Progress Traces

**Files:**
- Modify: `usb/ucd_dwc2.c:654,802,871-895`
- Modify: `tools/test-dwc2-async-contract.sh`

**Interfaces:**
- Consumes: `KINFO_USB_ASYNC(args)` from `usb/usb.h`.
- Produces: normal DWC2 HID operation without repeated serial output unless `CONF_DEBUG_USB_ASYNC=y`.

- [ ] **Step 1: Add a failing contract assertion for gated trace sites**

Require that all high-frequency trace strings use the shared macro:

```sh
require_multiline 'KINFO_USB_ASYNC\(\("dwc2_async: start[\s\S]*KINFO_USB_ASYNC\(\("dwc2_async: complete[\s\S]*KINFO_USB_ASYNC\(\("dwc2_async: split-start-ack[\s\S]*KINFO_USB_ASYNC\(\("dwc2_async: split-complete-nyet[\s\S]*KINFO_USB_ASYNC\(\("dwc2_async: split-complete-nak[\s\S]*KINFO_USB_ASYNC\(\("dwc2_async: rearm' \
    'DWC2 high-frequency async traces use the shared trace gate'
```

- [ ] **Step 2: Run the contract test and verify it fails**

Run: `sh tools/test-dwc2-async-contract.sh`

Expected: failure describing ungated DWC2 high-frequency traces.

- [ ] **Step 3: Replace only repeated progress logs**

Change these calls in `usb/ucd_dwc2.c` from `KINFO` to
`KINFO_USB_ASYNC`:

```c
KINFO_USB_ASYNC(("dwc2_async: start channel=%d generation=%lu split=%d pid=%d\n",
                 channel, slot->generation, slot->split_state, slot->pid));
KINFO_USB_ASYNC(("dwc2_async: complete generation=%lu length=%ld\n", generation,
                 actual_length));
KINFO_USB_ASYNC(("dwc2_async: split-start-ack generation=%lu\n",
                 slot->generation));
KINFO_USB_ASYNC(("dwc2_async: split-complete-nyet generation=%lu\n",
                 slot->generation));
KINFO_USB_ASYNC(("dwc2_async: split-complete-nak generation=%lu\n",
                 slot->generation));
KINFO_USB_ASYNC(("dwc2_async: rearm generation=%lu reason=%s\n",
                 slot->generation, (hcint & DWC2_HCINT_NAK) ? "nak" :
                 "chhltd"));
```

Do not change stop, release, error, shutdown, submit, cancel, or controller
initialization messages.

- [ ] **Step 4: Run verification**

Run: `sh tools/test-dwc2-async-contract.sh && make gitready && git diff --check`

Expected: contract and style checks pass with no whitespace errors.

- [ ] **Step 5: Commit**

```bash
git add usb/ucd_dwc2.c tools/test-dwc2-async-contract.sh
git commit -m "Gate DWC2 async progress traces"
```

### Task 3: Build and PR Verification

**Files:**
- No source changes expected.

**Interfaces:**
- Verifies: Raspberry Pi configurations compile with the default-disabled option.

- [ ] **Step 1: Run final local verification**

Run: `sh tools/test-dwc2-async-contract.sh && make gitready && git diff --check master...HEAD`

Expected: contract and style checks pass; the diff has no whitespace errors.

- [ ] **Step 2: Push and inspect CI**

Run:

```bash
git push origin feature/177-async-usb-interrupt-transfers
gh pr checks 178 --watch
```

Expected: rpi1, rpi2, rpi2-sparse, rpi3, and rpi4 build successfully.
