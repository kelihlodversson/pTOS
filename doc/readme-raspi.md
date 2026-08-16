# EmuTOS - Raspberry Pi version

This SD card image is suitable for the following hardware:
- Raspberry Pi 1, Zero, A, A+, B, B+ and CM0
- Raspberry Pi 2B
- Raspberry Pi 3, 3+ and CM3 (running in 32-bit mode)
- Raspberry Pi 4 and 400

The firmware auto-detects the board and picks the matching image; all four
are on the card, so the same card works across the whole lineup:

kernel.img        - Pi 1, Zero, A, A+, B, B+ and CM0
kernel7.img       - Pi 2B
kernel8-32.img    - Pi 3, 3+ and CM3
kernel7l.img      - Pi 4 and 400

Also on the card are the files the Pi's GPU needs to get from power-on to
loading one of the images above: bootcode.bin, start.elf, fixup.dat,
start4.elf and fixup4.dat. They are Raspberry Pi's own, redistributed
unmodified under the terms in LICENCE.broadcom, which travels with them.
**No config.txt is needed**: the "-32" in kernel8-32.img is enough on its
own to force 32-bit mode on the Pi 3, 3+ and CM3, which can otherwise also
run 64-bit code.

## Writing the card

Write ptos-raspi-<version>.img (after unzipping it) directly to an SD card
with **dd**, **Raspberry Pi Imager** or **balenaEtcher**. Alternatively, on
a card you have already partitioned and formatted yourself, just copy over
the contents of ptos-raspi-<version>.zip.

The card's partition has room to spare beyond what's on it, so it is ready
to use for your own files right away.

## Optional extras

The following optional files are also supplied:
- emucurs.rsc - modifiable mouse cursors for the AES/desktop
- emucurs.def - definition file for the above
- emuicon.rsc - contains additional icons for the desktop
- emuicon.def - definition file for the above

Note that the emuicon.rsc file format differs from deskicon.rsc used by later
versions of the Atari TOS desktop.

These images have been built using:
make rpi1_defconfig && make
(and likewise for rpi2, rpi3 and rpi4)
