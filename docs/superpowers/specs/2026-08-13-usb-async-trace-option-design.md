# USB Async Trace Option Design

## Goal

Prevent normal USB HID operation from flooding the selected debug output while
retaining an opt-in trace for asynchronous USB host-controller scheduling.

## Configuration

Add `CONF_DEBUG_USB_ASYNC` to `usb/Kconfig`.  It depends on
`CONF_WITH_USB`, defaults to disabled, and is intended for host-controller
debugging across the USB stack.

## Trace Interface

Define `KINFO_USB_ASYNC(args)` in the shared USB header.  When
`CONF_DEBUG_USB_ASYNC` is enabled, it expands to `KINFO(args)`; otherwise it
compiles to a no-op.

## DWC2 Scope

Use the wrapper only for high-frequency DWC2 async progress messages:

- transfer start;
- successful completion;
- NAK re-arm;
- split ACK, NYET, and NAK progress.

Keep errors, cancellation, shutdown, release, and initialization diagnostics
as regular `KINFO()` output.

## Verification

Confirm the wrapper has both enabled and disabled definitions, all selected
DWC2 high-frequency traces use it, and the USB config defaults it to disabled.
Run the async contract test, `make gitready`, and a Raspberry Pi CI build.
