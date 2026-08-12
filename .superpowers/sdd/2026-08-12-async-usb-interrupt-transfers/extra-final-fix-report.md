# Extra Final Lifecycle Fix Report: #177

## Scope

This wave addresses only the remaining shutdown-lifecycle findings from the
final re-review.

## Root Cause

`usb_lowlevel_stop()` previously called `dwc2_async_shutdown()` for each slot,
but the latter logged and returned if its bounded `CHHLTD` wait expired. The
caller then masked DWC2 interrupts, disconnected the ARM USB IRQ, and reset
the controller. A retained async slot could therefore still have controller DMA
ownership when teardown continued.

Teardown also did not mark a controller-wide shutdown state before draining
slots. An error completion could release its slot and invoke its callback while
shutdown was in progress. The callback could submit another request using the
still-connected IRQ/controller, creating a new DMA transfer that the remaining
teardown did not halt.

## Changes

- Added `dwc2_priv.shutting_down`, set before any async slot is stopped.
- Reject async submissions while shutdown is active. Cancellation is accepted
  as a no-op because teardown itself owns every slot until `CHHLTD`.
- Suppress success/error callbacks and post-callback rearming while shutdown is
  active. Existing normal error handling is unchanged outside shutdown: a
  halted error slot is released before its callback, so the callback can retry.
- Made `dwc2_async_shutdown()` return a status. It waits synchronously for
  `CHHLTD`; on timeout it logs the channel and returns `ETIMEDOUT`.
- While shutdown is active, the channel halt interrupt remains masked from the
  IRQ handler, making the bounded shutdown wait the sole `CHHLTD` observer.
  This prevents the handler from acknowledging the bit before that wait sees
  it.
- Made `usb_lowlevel_stop()` fail closed on that timeout. It leaves global/host
  interrupts, the ARM IRQ handler, controller registers, and retained slots in
  place, so a later `CHHLTD` IRQ remains observable and no DMA buffer is reused
  or reset early.
- Added contract assertions for shutdown state, guards, and timeout deferral.

The existing `KINFO` trace now records both the intentional suppression of an
error callback during shutdown and a retained-controller halt timeout.

## Validation

- RED: `sh tools/test-dwc2-async-contract.sh` failed before implementation with
  `missing async DWC2 contract: controller shutdown state`.
- GREEN: `sh tools/test-dwc2-async-contract.sh` passed after implementation.
- `make gitready` passed. The Makefile emitted its existing duplicate `&`
  target warnings before the check ran.
- `git diff --check` passed.
- Targeted `make obj/ucd_dwc2.o` succeeded for `rpi1`, `rpi2`,
  `rpi2-sparse`, and `rpi3`. `rpi4` uses xHCI and does not compile DWC2.
- Full `make` / `make -k` image links for all five configurations failed on the
  pre-existing unresolved `_strcpy` from `bios/bios.tr.c:666`; this is outside
  the changed driver. No DWC2 compilation errors were reported.

## Residual Concern

No hardware/QEMU lifecycle exercise was possible because the current images do
not link. A `CHHLTD` timeout now intentionally leaves the USB controller live
and returns `ETIMEDOUT`; callers must not treat that low-level stop as complete
or reuse its controller state.
