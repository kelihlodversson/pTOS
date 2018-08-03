# pTOS
Native EmuTOS port to the Raspberry PI.

See https://github.com/emutos/emutos for the original project.
Parts of this project are derived from the [Circle bare metal framework](https://github.com/rsta2/circle) for Raspberry PI.

This is a native port of EmuTOS to the ARM architecture running on the
Raspberry PI. It's a very early effort that currently boots and sets up
all OS trap vectors and tries launching the AES but fails shortly after,
as most of the VDI is completely non-functional.

Currently all development effort happens using Qemu with the raspi2 machine type.

To create an image, run make rpi2 (rpi1 and rpi3 target are also available, but
even less tested than the rpi2 one.)
To test it using Quemu, run
    qemu-system-arm -M raspi2 -bios kernel7.img  -d guest_errors -serial stdio

You can additionally pass "-S -s" to allow attaching a remote gdb to the machine.

For more information on which Qemu versions to use, see [Circle's Qemu documentation](https://github.com/rsta2/circle/blob/f5999e58f14b90204aafa7859428661cd01b22b1/doc/qemu.txt)

## Future development

There are a few directions that could make this project more useful:

### Add support for chunky graphics in VDI (and/or replace it with parts of fVDI.)

Some initial hacks for 8bb framebuffer already in progress:
* Lines and pattern fills implemented.
* Font rendering supported without text effects.
* Raster functions are still not implemented. Icons show up as garbled output as they are poked into the right memory location, but the bitblit functions still assume bitplanes.

### SD card support.

* Adapted the EMMC controller code from the Circle project. Write is currently disabled for safety.

### Add a simple USB stack to emulate an IKBD keyboard.

* The USB stack in Mint has been integrated with some host controller code from U-Boot.
* Currently does not work reliably on RPI3.
* Only mice devices in boot report mode is currently implemented.

### Create a mint/TOS/arm binutils and gcc toolchain port.

* Otherwise having an OS without apps is no fun in the long run.
* Reusing bFLT toolchains may be easier than hacking support for an ARM version the old TOS format.

## Help highly appreciated

If you're interested in getting your hands dirty with some bare metal OS hacking,
feel free to fork this project and try to get things to compile and see how far
you get.
