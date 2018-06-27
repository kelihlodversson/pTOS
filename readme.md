# pTOS
Native EmuTOS port to the Raspberry PI.

See https://github.com/emutos/emutos for the original project.

This is a native port of EmuTOS to the ARM architecture running on the
Raspberry PI. It's a very early effort that currently boots and sets up
all OS trap vectors and tries launching the AES but fails shortly after,
as most of the VDI is completely non-functional.

Currently all development effort happens using Qemu with the raspi2 machine type.

To create an image, run make rpi2 (rpi1 and rpi3 target are also available, but
even less tested than the rpi2 one.)
To test it using Quemu, run
    qemu-system-arm -M raspi2 -kernel emutos.elf  -d guest_errors -serial stdio

(The -serial option is no longer required, as conout can now output to the Raspberry PI
framebuffer.)

You can additionally pass "-S -s" to allow attaching a remote gdb to the machine.

## Future development

There are a few directions that could make this project more useful:

1. Get it to boot on actual hardware (and more testing on emulators).
  * Currently boots on some versions of Qemu.
  * Fails to boot on Raspberry PI 3.
  * Manages to set the default palette on Raspberry PI Zero but hangs with a white screen somewhere after that.
1. Add support for chunky graphics in VDI (and/or replace it with parts of fVDI.)
  * Some initial hacks for 8bb framebuffer already in progress.
1. Add SD card support.
  * The coldfire driver can possibly be adapted to support the EMMC controller.
1. Add a simple USB stack to emulate an IKBD keyboard. (Failing that, use the UART pins to connect an ATARI ST keyboard to the machine with some 5 to 3.3V level shifting.)
  * The USB stack in Mint seems to have support to be compiled for TOS only, and looks promising.
1. Create a mint/TOS/arm binutils and gcc toolchain port.

## Help highly appreciated

If you're interested in getting your hands dirty with some bare metal OS hacking,
feel free to fork this project and try to get things to compile and see how far
you get.
