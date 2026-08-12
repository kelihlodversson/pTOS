# Final Fix Report: #177

## Review Findings Addressed

1. `usb/ucd_dwc2.c` now records an eligible `HFNUM` value per async slot.
   NAKs and successful reports schedule the next poll rather than immediately
   enabling the channel. The USB SOF interrupt starts only scheduled slots
   whose interval has elapsed. High-speed endpoint `bInterval` values use the
   USB 2.0 exponent in microframes. Full/low-speed values use milliseconds,
   converted to microframes only on a high-speed host port (the split path).
2. Error handling now records a pending error, halts the channel, releases the
   slot, and only then calls the UDD callback. A callback can submit its same
   persistent request immediately; no error status is rearmed.
3. `usb_lowlevel_stop()` now uses the existing channel-disable path and waits
   up to 2000 for `CHHLTD` before releasing a slot. A timeout is logged and
   ownership remains unreleased instead of racing controller teardown.
4. `.claude/skills/ptos-smoketest/SKILL.md` no longer claims its required QEMU
   HID invocations were verified in-tree. The mandatory `usb-mouse` and
   `usb-kbd` commands and HID probe pass criteria remain unchanged.

## Regression Coverage

Added `tools/test-dwc2-async-contract.sh`.

- Red: before the implementation, `sh tools/test-dwc2-async-contract.sh`
  failed with `missing async DWC2 contract: per-slot next eligible frame`.
- Green: after the implementation, the same command reports
  `dwc2 async contract test passed`.
- The check requires per-slot frame scheduling, USB speed-aware interval
  conversion, SOF wakeup, post-release error callback support, and a bounded
  `CHHLTD` shutdown wait.

## Verification Evidence

- `make gitready`: passed (`gitready checks passed.`).
- `git diff --check`: passed with no output.
- Legacy HID polling/stub search:
  `rg 'usb_(mouse|keyboard)_timerc|usb_(mouse|keyboard)_irq|irq_handle =' usb bios/arch/arm/vectors.c`
  returned no matches.
- `make <config>_defconfig && make obj/ucd_dwc2.o` completed for `rpi1`,
  `rpi2`, `rpi2-sparse`, `rpi3`, and `rpi4`. The existing unused `hcd_name`
  warning remains.
- Full `make rpi1_defconfig && make` reached the final link and failed on the
  pre-existing unrelated unresolved symbol `_strcpy` from `bios/bios.tr.c:666`.
  `usb/ucd_dwc2.c` compiled successfully before that failure.

## HID QEMU Evidence

The required rpi1 command was attempted with the implementation worktree
output:

```sh
qemu-system-arm -M raspi1ap -bios kernel.img -device usb-mouse -device usb-kbd \
  -d guest_errors -D /tmp/ptos-rpi1-hid-qemu.log -display none \
  -serial file:/tmp/ptos-rpi1-hid-serial.log
```

It could not start because the failed full link produced no `kernel.img`:
`qemu-system-arm: Failed to load firmware from kernel.img`. Both capture files
were empty. No branch-matching CI artifact containing a runnable rpi1/rpi2
image was available locally, so there is no HID enumeration or guest-error
evidence to claim. The smoke requirements remain documented as required,
not verified.
