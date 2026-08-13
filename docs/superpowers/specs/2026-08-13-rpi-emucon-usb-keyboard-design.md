# Raspberry Pi EmuCon USB Keyboard Input Design

## Scope

Fix GitHub issue #185 for QEMU Raspberry Pi 1 and Raspberry Pi 2 images.
The issue occurs after launching EmuCon from the GEM desktop with a USB
keyboard.  Desktop keyboard input works, but EmuCon receives no input.

An attached FAT16 SD-card image is required during testing because EmuCon
hangs when no SD card is present (issue #184).  Physical Raspberry Pi
hardware is out of scope.

## Existing Input Path

The DWC2 interrupt handler completes USB HID keyboard transfers.  The HID
driver translates USB usages into Atari scancodes and calls `kbd_int()`.
`kbd_int()` translates them and queues values in `ikbdiorec`.  EmuCon reads
that queue through `conin()` and `bconin2()`.

## Design

Instrument the USB-completion, HID-to-`kbd_int()`, and `bconin2()` queue
boundaries while reproducing the failure in QEMU.  This identifies whether
events stop at DWC2 interrupt delivery, HID report completion, keyboard queue
insertion, or queue consumption after the AES launches EmuCon.

Apply one minimal correction at the identified ARM/QEMU boundary.  Do not add
a second input route, poll USB from `bconin2()`, or change the generic EmuCon
or BIOS console APIs.  The existing DWC2-to-IKBD path remains the sole input
path.

Diagnostic instrumentation is temporary and must not remain in the final
change unless it is a concise, useful existing-style trace.

## Acceptance Criteria

For `raspi1ap` and `raspi2b` QEMU machines, launched with USB keyboard,
USB mouse, and a valid FAT16 SD-card image:

1. The GEM desktop still accepts USB keyboard input.
2. File > Execute EmuCon launches the console.
3. Typing `help` at the EmuCon prompt echoes and executes the command.
4. Typing `exit` returns to the GEM desktop.

The normal project build must continue to succeed for the selected target.
