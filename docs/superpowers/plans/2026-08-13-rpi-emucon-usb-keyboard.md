# Raspberry Pi EmuCon USB Keyboard Input Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make USB keyboard input continue to reach EmuCon launched from the GEM desktop on QEMU Raspberry Pi 1 and 2 images.

**Architecture:** USB HID keyboard reports remain integrated through pTOS's BIOS input path.  The investigation first compares the current direct `kbd_int()` call from DWC2 IRQ context with upstream's deferred `send_data()` plus `fake_hwint()` injection: execution context, vector dispatch, and the values visible in the keyboard IOREC.  It must prove which difference matters before changing `kbd_int()`, process CPSR, or introducing a deferred/vector injection path.

**Tech Stack:** GNU90 C, ARMv6/ARMv7 CPSR exception return, pTOS BIOS/BDOS, QEMU `raspi1ap` and `raspi2b`.

## Global Constraints

- Scope is QEMU Raspberry Pi 1 and Raspberry Pi 2 only; physical hardware is out of scope.
- QEMU must attach USB keyboard, USB mouse, and an empty valid FAT16 SD-card image.
- Build an AES-free Raspberry Pi kernel (`CONF_WITH_AES=n`) for the primary direct-to-EmuCon diagnostic; use the normal desktop image only for the final launch regression.
- Desktop input and EmuCon input must both use the existing USB HID -> `kbd_int()` -> `ikbdiorec` path.
- Do not poll USB from `bconin2()` and do not change EmuCon's console API.
- Keep shared m68k behavior unchanged.
- Remove temporary diagnostic tracing before the final change unless it is a concise, established-style trace.
- Do not substitute upstream `send_data()`/`fake_hwint()` semantics until tracing proves that pTOS's direct `kbd_int()` path misses a consumer-visible side effect.

---

## File Structure

- Modify only if proven: `usb/udd_keyboard.c`, `bios/ikbd.c`, and the ARM DWC2/timer integration point selected by tracing.
- Temporarily modify: `usb/ucd_dwc2.c`, `usb/udd_keyboard.c`, and `bios/ikbd.c` — provides boundary traces for completion, direct injection, queue insertion, and queue consumption; remove before the final commit.
- Create: `/var/folders/_3/s5x2rvs93b9d1b0hmmt664mh0000gn/T/opencode/empty-fat16-sd.img` — disposable 64 MiB FAT16 QEMU SD-card test medium; never add it to Git.
- Create only if retained: `configs/rpi1-cli_defconfig` and `configs/rpi2-cli_defconfig` — generated with `CONF_WITH_AES=n`, retaining the Raspberry Pi kernel image target and default `CONF_WITH_CLI=y`.

### Task 1: Prove the Runtime Boundary Where Input Stops

**Files:**
- Modify temporarily: `usb/ucd_dwc2.c:773-816`
- Modify temporarily: `usb/udd_keyboard.c:183-265`
- Modify temporarily: `bios/ikbd.c:140-180,629-...`

**Interfaces:**
- Consumes: `keyboard_report_complete(struct usb_async_int_msg *msg, LONG status, LONG actual_length)`, `kbd_int(UBYTE scancode)`, and `LONG bconin2(void)`.
- Compares: upstream `send_data(kbd_entry, iokbd, scancode)` plus `fake_hwint()` with pTOS `kbd_int(scancode)`.
- Produces: serial evidence identifying whether a key press reaches DWC2 completion, HID decoding, the pTOS queue insertion, and `bconin2()` after EmuCon is launched; it also records the exact 32-bit queue value at insertion and consumption.

- [ ] **Step 1: Add bounded temporary traces at the three input boundaries**

  In `usb/ucd_dwc2.c`, trace one callback delivery per completed keyboard report.

  ```c
  KDEBUG(("dwc2_async: keyboard callback generation=%lu\n", generation));
  ```

  In `usb/udd_keyboard.c`, immediately before each call to `kbd_int(scancode)` for a key press, add:

  ```c
  KDEBUG(("usb keyboard: press scancode 0x%02x\n", scancode));
  ```

  In `bios/ikbd.c`, use the existing `kbd_int()` entry trace and add a bounded trace at `push_ikbdiorec()` with the inserted value; add one trace in `bconin2()` after it dequeues `value`:

  ```c
  KDEBUG(("bconin2: dequeued 0x%08lx\n", value));
  ```

  Do not trace the empty polling loop in `bconin2()`; it would flood the serial console.  Also record whether a keyboard vector (`kbdvecs.ikbdsys`) is invoked by the direct path; upstream's `send_data()` deliberately targets the installed vector while pTOS's direct `kbd_int()` does not.

- [ ] **Step 2: Build an AES-free Raspberry Pi 2 diagnostic image**

  Start from `rpi2_defconfig`, set `CONF_WITH_AES=n` in `make menuconfig`,
  preserve the configuration with `make savedefconfig`, and build it:

  ```sh
   make rpi2_defconfig
   make menuconfig
   make savedefconfig
   make
   ```

   In the tested macOS environment, use Homebrew GNU Make (`gmake`) in place
   of the system `make`, which is GNU Make 3.81 while this tree requires 4.3
   or later. In that environment, do not run `olddefconfig` with `--kconfig`:
   the installed kconfiglib accepts `Kconfig` only as a positional argument.

   Expected: successful build producing `kernel7.img` that boots directly into
  EmuCon, without AES or EmuDesk.

- [ ] **Step 3: Create the required FAT16 test image outside the repository**

  Run:

  ```sh
  qemu-img create -f raw /var/folders/_3/s5x2rvs93b9d1b0hmmt664mh0000gn/T/opencode/empty-fat16-sd.img 64M
  hdiutil attach -imagekey diskimage-class=CRawDiskImage -nobrowse -noverify -nomount /var/folders/_3/s5x2rvs93b9d1b0hmmt664mh0000gn/T/opencode/empty-fat16-sd.img
  /sbin/newfs_msdos -F 16 -S 512 -c 1 /dev/rdiskN
  hdiutil detach /dev/diskN
  ```

  Replace `diskN` with the disk identifier printed by `hdiutil attach`.  Expected: `file .../empty-fat16-sd.img` reports `FAT (16 bit)`.

- [ ] **Step 4: Reproduce the failure and capture the trace**

  Run:

  ```sh
  qemu-system-arm -M raspi2b -bios kernel7.img -d guest_errors \
    -device usb-mouse -device usb-kbd \
    -drive file=/var/folders/_3/s5x2rvs93b9d1b0hmmt664mh0000gn/T/opencode/empty-fat16-sd.img,if=sd,format=raw \
    -serial stdio
  ```

  At the booted EmuCon prompt, type `h`. This tests the USB-to-console path
  without AES application launch or desktop input handling.
  Expected before the fix: the trace proves one of these mutually exclusive outcomes:

  ```text
  A. no USB keyboard press trace: USB IRQ delivery stopped
  B. USB and kbd_int traces, but no bconin2 dequeue: queue insertion/visibility stopped
  C. USB, kbd_int, insertion, and bconin2 dequeue traces: input is consumed elsewhere
  D. USB, kbd_int, and insertion traces but no dequeue: EmuCon cannot observe the pTOS direct-injection result
  ```

  If direct EmuCon accepts input, rebuild normal `rpi2_defconfig` and repeat
  the trace after **File > Execute EmuCon**. The changed boundary then isolates
  the AES launch state.

- [ ] **Step 5: Compare direct and upstream injection semantics**

  Compare pTOS `kbd_int()`'s queue value and vector calls with upstream's
  `send_data(kbd_entry, iokbd, scancode)` followed by one `fake_hwint()` per
  report.  Determine whether the installed `ikbdsys` vector or the hardware-
  interrupt emulation is required for the failing console consumer.  Record
  exact serial lines and the observed queue values in the issue work notes.

- [ ] **Step 6: Remove the temporary HID and BIOS diagnostics**

  Delete the temporary traces from `usb/udd_keyboard.c` and `bios/ikbd.c`.  Retain no diagnostic-only changes there.

- [ ] **Step 7: Commit retained Task 1 documentation or lasting diagnostics**

   Do not commit temporary instrumentation. Commit accurate, environment-
   specific diagnostic command corrections made to this plan separately. If a
   lasting `proc_go()` trace is justified by existing `KDEBUG` conventions,
   commit it separately:

  ```sh
  git add bdos/proc.c
  git commit -m "Trace ARM process interrupt state"
  ```

   Otherwise proceed with no commit from this task.

### Task 2: Implement Only the Injection Correction Proven by Task 1

**Files:**
- Modify: exact file and function identified by Task 1

**Interfaces:**
- Consumes: Task 1 evidence for one proven failure boundary.
- Produces: the existing HID press/release sequence delivered through the single pTOS BIOS input path, with the missing required side effect restored.

- [ ] **Step 1: State the single confirmed hypothesis before editing**

  Write the result in the issue work notes in this exact form:

  ```text
  Hypothesis: <one boundary> prevents EmuCon from receiving USB key <key> because <trace evidence>.
  ```

  Do not edit code until the statement names one of: DWC2 IRQ delivery,
  IRQ-context direct injection, a missing `send_data()`/`fake_hwint()` side
  effect, queue visibility, or child CPSR IRQ masking.

- [ ] **Step 2: Implement the corresponding minimal experiment**

  Apply exactly one change matching the hypothesis:

  ```text
  IRQ-context direct injection: defer only existing kbd_int() calls; do not poll USB.
  Missing vector/interrupt side effect: add the minimal pTOS-native equivalent around the existing kbd_int() path.
  Child CPSR IRQ masking: clear only the CPSR IRQ mask in the ARM child SPSR.
  ```

  Preserve key ordering: released keys, released modifiers, pressed modifiers,
  then pressed keys.  Do not combine these alternatives in one experiment.

- [ ] **Step 3: Build the AES-free Raspberry Pi 2 diagnostic image to verify the change**

  Run:

  ```sh
  make rpi2_defconfig
  make menuconfig
  make
  ```

  Set `CONF_WITH_AES=n` in `make menuconfig`. Expected: successful build
  producing a direct-to-EmuCon `kernel7.img`.

- [ ] **Step 4: Verify direct EmuCon, then desktop-launched EmuCon, on Raspberry Pi 2 QEMU**

  Run:

  ```sh
  qemu-system-arm -M raspi2b -bios kernel7.img -d guest_errors \
    -device usb-mouse -device usb-kbd \
    -drive file=/var/folders/_3/s5x2rvs93b9d1b0hmmt664mh0000gn/T/opencode/empty-fat16-sd.img,if=sd,format=raw \
    -serial stdio
  ```

  First verify direct EmuCon accepts `help`. Then rebuild normal
  `rpi2_defconfig` and verify:

  ```text
  1. Use the keyboard in the GEM desktop.
  2. Select File > Execute EmuCon.
  3. Type help and press Enter; verify command output.
  4. Type exit and press Enter; verify return to GEM.
  ```

  Expected: every step succeeds and no new guest errors appear after desktop startup.

- [ ] **Step 5: Commit the minimal proven fix**

  Run:

  ```sh
  git add <only-the-proven-source-files>
  git commit -m "Fix USB keyboard console input"
  ```

### Task 3: Confirm Raspberry Pi 1 and Guard Against Shared-Target Regression

**Files:**
- Modify: none expected

**Interfaces:**
- Consumes: the single injection correction from Task 2.
- Produces: QEMU evidence that the same BIOS input path works on `raspi1ap` and that an m68k configuration still builds without changes.

- [ ] **Step 1: Build the AES-free Raspberry Pi 1 diagnostic image**

  Run:

  ```sh
  make rpi1_defconfig
  make menuconfig
  make savedefconfig
  make
  ```

  Set `CONF_WITH_AES=n` in `make menuconfig`. Expected: successful build
  producing a direct-to-EmuCon `kernel.img`.

- [ ] **Step 2: Verify direct EmuCon and desktop-launched EmuCon on Raspberry Pi 1 QEMU**

  Run:

  ```sh
  qemu-system-arm -M raspi1ap -bios kernel.img -d guest_errors \
    -device usb-mouse -device usb-kbd \
    -drive file=/var/folders/_3/s5x2rvs93b9d1b0hmmt664mh0000gn/T/opencode/empty-fat16-sd.img,if=sd,format=raw \
    -serial stdio
  ```

  Verify direct EmuCon accepts `help`. Then rebuild normal `rpi1_defconfig`
  and verify desktop keyboard input, File > Execute EmuCon, `help`, and
  `exit`. Expected: both direct and desktop-launched EmuCon accept input;
  desktop-launched `exit` returns to GEM.

- [ ] **Step 3: Build an m68k configuration without altering its behavior**

  Run:

  ```sh
  make atari512_defconfig && make
  ```

  Expected: successful build.  The selected integration boundary must keep the
  m68k keyboard path unchanged.

- [ ] **Step 4: Run repository hygiene checks**

  Run:

  ```sh
  make gitready
  git diff --check
  git status --short
  ```

  Expected: formatter/checks pass; no FAT16 image, serial log, or temporary trace remains in the worktree.

- [ ] **Step 5: Update GitHub issue #185 with evidence and close it if the acceptance criteria pass**

  Add a comment describing the confirmed injection difference and fix,
  including whether pTOS required deferred execution, a vector side effect, or
  a CPSR correction.  Include the two QEMU machine names and the `help`/`exit`
  checks.  Close #185 only after both QEMU validations pass.
