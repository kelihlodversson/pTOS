# Async USB HID Interrupt Transfers Design

## Context

Issue #167 identified Raspberry Pi mouse clicks that are ignored when a
button is pressed and released without pointer movement. PR #168 reduced the
failure window by draining reports from Timer C and increasing polling from
50 Hz to 200 Hz, but real-hardware testing still loses sufficiently short
clicks.

The existing DWC2 driver treats HID interrupt endpoints as synchronous,
one-shot bulk-style transfers. The host sends an IN token only when
`usb_mouse_timerc()` or `usb_keyboard_timerc()` runs. If a boot mouse changes
from button-up to button-down and back to button-up between polls, its
one-report-deep state buffer retains only button-up. The VDI sees no state
change and cannot reconstruct the lost click.

Circle solves this by submitting an asynchronous transfer to the HID
interrupt endpoint. Its DWC2 controller IRQ processes each completion and
immediately re-arms the request. The host therefore polls at USB frame
cadence rather than at a software timer interval.

The current DWC2 code already contains the prerequisites for a focused
implementation: 16 host-channel register sets, DMA bounce-buffer support,
interrupt register definitions, Raspberry Pi IRQ connection support, and a
commented-out `raspi_connect_irq(ARM_IRQ_USB, ...)` hook. Channel 0 is the
existing synchronous path. The unused channels can carry persistent HID
interrupt-IN requests.

## Goal

Replace Timer-C USB HID polling with DWC2 IRQ-driven asynchronous interrupt
transfers for both boot-protocol mouse and keyboard devices on Raspberry Pi
1, 2, and 3 targets.

The result must preserve the current mouse packet translation, keyboard
rollover handling, and existing synchronous USB enumeration/control traffic,
while reliably capturing fast input transitions that software polling can
miss.

## Non-Goals

- Do not replace the generic UCD/UDD USB architecture with a general-purpose
  USB request scheduler.
- Do not change device enumeration, hub discovery, or synchronous control and
  bulk transfers.
- Do not add xHCI support or change Raspberry Pi 4 USB behavior.
- Do not support more than the existing one boot mouse and one boot keyboard.
- Do not modify PR #168 as part of this issue; it remains paused.

## Approach

### Narrow Async Interrupt-IN UCD Contract

Add a controller-facing async interrupt-IN message type to `usb/usb_api.h`.
It contains the USB device, interrupt pipe, persistent caller-owned report
buffer, transfer length, endpoint interval, callback, and callback context.
Add submit and cancel operations, then expose small wrappers from the generic
USB layer. This keeps UDDs independent of DWC2 details and follows the
existing `usb_bulk_msg()` routing model.

The callback runs in IRQ context. This is compatible with the current path:
Timer C already invokes the HID report handlers in ARM IRQ context, and the
mouse path already relies on the AES fork queue's interrupts-masked contract.

### DWC2 Async Request Slots

`usb/ucd_dwc2.c` retains host channel 0 for all synchronous transfers. It
adds a fixed pool of four async slots using channels 1 through 4. Each slot
owns:

- its assigned host channel;
- the submitted async message;
- cacheline-aligned DMA bounce storage;
- the endpoint DATA toggle;
- split-transaction phase when the device is behind a high-speed hub; and
- active/cancelled state.

Four slots are sufficient for the two current consumers while leaving a
small margin without implementing a dynamic allocator.

At low-level initialization, the driver connects `ARM_IRQ_USB`, enables the
DWC2 host-channel interrupt source, and enables the controller's global IRQ
mask. It only unmasks channel bits for active async slots, so channel 0's
synchronous polling path remains unaffected.

### Persistent Interrupt-IN State Machine

Submitting a request initializes its host channel, DMA buffer, endpoint
characteristics, and interrupt mask, then enables the channel on the next
appropriate USB frame parity.

The USB IRQ handler finds pending active channels and acknowledges their
channel interrupt status. It applies this state machine:

- **No data / NAK:** do not call the UDD. Re-arm the request on the next
  frame, keeping the DATA toggle unchanged.
- **Successful transfer:** derive the actual length and updated DATA toggle
  from `HCTSIZ`, invalidate/copy the DMA buffer to the UDD report buffer, call
  the callback, then re-arm the same request unless cancelled.
- **Start split ACK:** re-arm as complete split.
- **Complete split NYET:** re-arm complete split.
- **Complete split NAK:** restart the split sequence on the next frame.
- **Transfer or protocol error:** call the callback with failure and leave
  the request stopped. A UDD may explicitly submit again after handling the
  error; this avoids a persistent error IRQ loop.
- **Cancellation/disconnect:** disable the host channel, release its slot, and
  never re-arm it.

Split handling is required: Raspberry Pi external ports commonly sit behind
the LAN951x high-speed hub, so full- and low-speed HID devices need the same
start-split/complete-split protocol as the current synchronous DWC2 path.

### HID Device Drivers

After HID boot-protocol setup, mouse and keyboard UDDs submit a persistent
async request instead of depending on Timer C.

Mouse completion reuses the current boot-report translation:

- compare button state, motion, and available wheel byte to suppress genuine
  duplicate reports, because some mice send identical reports even after
  `SET_IDLE(0)`;
- convert left/right button bits to the IKBD relative-mouse packet;
- dispatch extra-button and wheel events; and
- call `call_mousevec()` for changed reports.

The filter does not reintroduce the original race: each edge has its own
hardware-polled transfer completion, so the filter only removes reports that
are truly identical to the last processed one.

Keyboard completion retains the current six-key rollover rejection and
release-before-press ordering. Both UDDs cancel their request on disconnect.
The legacy `dev->irq_handle` stubs are no longer used.

### Timer C

Remove mouse and keyboard polling calls and their declarations from
`bios/arch/arm/vectors.c::int_timerc()`. Timer C continues to provide its
normal 200 Hz OS tick, keyboard repeat handling, sound IRQ work, and 50 Hz
VBL emulation; it no longer performs USB I/O.

## Data Flow

1. USB enumeration identifies a HID boot mouse or keyboard.
2. Its UDD configures boot protocol and idle mode, initializes its report
   state, and submits one async interrupt-IN request.
3. DWC2 hardware polls the endpoint at USB frame cadence. Empty polls NAK
   and are re-armed internally without entering the UDD.
4. A completed report raises `ARM_IRQ_USB`. The DWC2 handler copies the DMA
   result into the persistent UDD report buffer and invokes its callback.
5. Mouse converts the report to the existing IKBD/VDI path. Keyboard converts
   it to existing Atari scancodes. The DWC2 request is then re-armed.
6. Disconnect cancels the active request and clears the UDD device pointer.

## Validation

### Build

CI must compile all Raspberry Pi configurations: rpi1, rpi2, rpi2-sparse,
rpi3, and rpi4 (the latter must remain unaffected because it does not use
DWC2).

### QEMU Smoke Test

Update `.claude/skills/ptos-smoketest/SKILL.md` so both Raspberry Pi QEMU
commands explicitly attach HID devices:

```sh
-device usb-mouse -device usb-kbd
```

This is required for input-driver validation; without those options QEMU
does not present a boot mouse or keyboard for pTOS to enumerate. Smoke tests
must run the updated rpi1 and rpi2 commands, confirm the image boots without
guest errors, and confirm HID devices are enumerated through the driver
traces.

### Real Hardware

On Raspberry Pi hardware, verify:

- repeated short left-clicks with no pointer movement register;
- normal drag, movement, wheel, and extra-button behavior remain correct;
- fast press/release keyboard input and modifier changes are received;
- rollover reports do not manufacture releases or repeats; and
- unplugging a supported HID device does not crash or leave a request
  re-arming.

## Alternatives Rejected

### Faster Timer Polling

Increasing Timer C polling or draining synchronous reports reduces the race
window but cannot capture a press and release that both happen before the
next software-initiated IN token.

### Full Circle USB Scheduler Port

Circle has a complete dynamic channel allocator, URB model, frame scheduler,
and broader USB-device lifecycle support. Porting it would exceed this
issue's narrow HID requirement. This design adopts only the required IRQ,
channel, DMA, and persistent-transfer mechanics.
