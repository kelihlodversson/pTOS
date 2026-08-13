# Raspberry Pi EmuCon USB Keyboard Input Design

## Scope

Fix GitHub issue #185 for QEMU Raspberry Pi 1 and Raspberry Pi 2 images.
The issue occurs after launching EmuCon from the GEM desktop with a USB
keyboard.  Desktop keyboard input works, but EmuCon receives no input.

An attached FAT16 SD-card image is required during testing because EmuCon
hangs when no SD card is present (issue #184).  Physical Raspberry Pi
hardware is out of scope.

Testing has two stages. The primary diagnostic build keeps the Raspberry Pi
kernel target but sets `CONF_WITH_AES=n`, which boots directly into EmuCon.
This isolates USB-to-console input from AES and desktop process launch. The
normal desktop image remains the regression test for File > Execute EmuCon.

## Existing Input Path

The DWC2 interrupt handler completes USB HID keyboard transfers.  The HID
driver translates USB usages into Atari scancodes and calls `kbd_int()`.
`kbd_int()` translates them and queues values in `ikbdiorec`.  EmuCon reads
that queue through `conin()` and `bconin2()`.

The FreeMiNT upstream driver does not inject input from a USB controller IRQ.
It polls the HID interrupt endpoint from a kernel thread (or periodic TOS
callback), then uses its TOS keyboard-injection mechanism.  pTOS changed to
the DWC2 asynchronous IRQ callback in commit `905a7552`.

The injection mechanisms also differ.  Upstream sends each translated
press/release through the installed extended keyboard vector with
`send_data(kbd_entry, iokbd, scancode)`, then calls `fake_hwint()` after the
report.  pTOS calls its own `kbd_int(scancode)` directly.  That updates pTOS
translation, modifier, repeat, and `ikbdiorec` state, but does not use a
separate keyboard-vector injection path.  The investigation must determine
whether EmuCon depends on a side effect of the upstream-style injection.

## Design

Instrument the DWC2 completion, HID-to-`kbd_int()`, keyboard-queue insertion,
and `bconin2()` consumption boundaries while reproducing the failure in QEMU.
This identifies whether events stop at USB interrupt delivery, report
completion, queue insertion, or queue consumption after AES launches EmuCon.

First test whether calling `kbd_int()` directly from the DWC2 IRQ callback is
the regression, using a minimal deferred execution mechanism in the USB/BIOS
integration layer.  The deferred path must retain the existing HID report
translation and BIOS `kbd_int()` queue path; it must not poll USB from
`bconin2()` or create an EmuCon-specific input route.  Only change ARM process
CPSR handling if the diagnostic evidence independently proves that IRQ masking
prevents USB completion delivery.

Before replacing the current injection path, trace the actual values inserted
into `ikbdiorec` and the `bconin2()` values consumed by EmuCon.  Compare that
with the upstream `send_data()` contract and `fake_hwint()` role.  Implement
an upstream-style vector injection only if this proves a missing consumer-
visible side effect; do not add it merely to match upstream structure.

Diagnostic instrumentation is temporary and must not remain in the final
change unless it is a concise, useful existing-style trace.

## Acceptance Criteria

For `raspi1ap` and `raspi2b` QEMU machines, launched with USB keyboard,
USB mouse, and a valid FAT16 SD-card image:

1. An AES-free Raspberry Pi kernel boots directly into EmuCon and accepts
   `help` input.
2. The normal GEM desktop still accepts USB keyboard input.
3. File > Execute EmuCon launches the console.
4. Typing `help` at the launched EmuCon prompt echoes and executes the
   command.
5. Typing `exit` returns to the GEM desktop.

The normal project build must continue to succeed for the selected target.
