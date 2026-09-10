EmuTOS - PRG versions

These special versions allow EmuTOS to be loaded from the filesystem (floppy
or hard disk) without the need of replacing the system ROM.
This is the simplest way to test EmuTOS on real hardware.
The drawback is less available RAM compared to ROM versions.

ptos.prg - Multilanguage
ptoscz.prg - Czech (PAL)
ptosde.prg - German (PAL)
ptoses.prg - Spanish (PAL)
ptosfi.prg - Finnish (PAL)
ptosfr.prg - French (PAL)
ptosgr.prg - Greek (PAL)
ptosit.prg - Italian (PAL)
ptosno.prg - Norwegian (PAL)
ptosru.prg - Russian (PAL)
ptosse.prg - Swedish (PAL)
ptossg.prg - Swiss German (PAL)
ptosuk.prg - English (PAL)
ptosus.prg - English (NTSC)

The following optional files are also supplied:
emucurs.rsc - modifiable mouse cursors for the AES/desktop
emucurs.def - definition file for the above
emuicon.rsc - contains additional icons for the desktop
emuicon.def - definition file for the above

Note that the emuicon.rsc file format differs from deskicon.rsc used by later
versions of the Atari TOS desktop.

Notes:
- these versions are compatible with any Atari hardware (except the FireBee)
- TT and Falcon 030 are supported
- the language of the Norwegian/Swedish versions is English; however the
  keyboard layouts are Norwegian/Swedish

For multilanguage version, the default language is English.
It can be changed by setting the NVRAM appropriately.

These special versions have been built using:
make release-prg

