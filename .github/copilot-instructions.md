# Copilot instructions for pTOS

pTOS is a port of [EmuTOS](https://github.com/emutos/emutos), a free
implementation of the Atari TOS, to the Raspberry Pi. The upstream Atari,
Amiga and ColdFire targets still build, so shared code must keep working on
m68k as well as on ARM.

This is a freestanding OS image: there is no libc and no host runtime. Do not
suggest `stdio.h`, `stdlib.h`, `malloc` or any other hosted facility. What the
code needs lives in `util/` and `include/` (`portab.h`, `string.h`,
`doprintf.h`, `intmath.h`, …).

## Building

The build is configured the way the Linux kernel is:

```sh
make help                # list the configurations in configs/
make rpi2_defconfig      # load one
make menuconfig          # optional: adjust it
make                     # build
```

There are no per-target make goals such as `make rpi2` or `make 512`; each is a
defconfig in `configs/`. Requirements are GNU make, `kconfiglib`
(`pip3 install kconfiglib`), and a cross toolchain: `arm-none-eabi-*` for the
Raspberry Pi, `m68k-atari-mint-*` for everything else.

## Configuration

`.config` is the single source of truth. `tools/genconfig.py` turns it into
`obj/autoconf.h` (for C) and `obj/auto.conf` (for make). **Never edit those two
files, and never propose a patch to them.**

Configuration symbols carry no `CONFIG_` prefix; they are named exactly like
the C macros they control:

- `CONF_WITH_FOO` and other feature options are always defined, to `0` or `1`.
  Test them with `#if CONF_WITH_FOO`, never `#ifdef` — `-Wundef` is enabled and
  `#ifdef` on an always-defined macro is always true.
- `MACHINE_*` and `TARGET_*` are defined only when set. Test them with
  `#ifdef` or `defined()`.
- `BUILD_*` symbols never reach the C code at all.

New options go in the `Kconfig` of the directory they belong to (`bios/Kconfig`,
`vdi/Kconfig`, …) or in the top level `Kconfig.machine`, `Kconfig.image`,
`Kconfig.i18n`, `Kconfig.debug`. Every option needs a help text and honest
`depends on` clauses. Do not add per-target `#ifdef` default blocks to
`include/config.h`; it holds only derived values, fixed limits and sanity
checks.

New source files go in the `obj-y` list of their directory's `build.mk`:

```make
obj-y += foo.o                    # always
obj-$(CONF_WITH_FOO) += foo.o     # only when the option is set
obj-$(ARCH_ARM) += foo.o          # only on ARM
```

The order in `build.mk` is the link order: `startup.o` stays first in `bios/`,
`endvdi.o` last in `vdi/`, `endgem.o` last in `desk/`.

## Layout

`bios/ bdos/ vdi/ aes/ desk/ cli/ usb/ util/` hold the OS, `include/` the
shared headers, `tools/` the host programs used by the build.

Those directories may have `arch/<m68k|coldfire|arm>/` and
`machine/<raspi|…>/` subdirectories, searched before the generic directory.
Two files of the same name in different `arch/` directories are alternative
implementations of the same object; prefer that over `#ifdef __arm__` inside a
shared file. Objects land flat in `obj/`, so source basenames must be unique
across the tree unless they are such alternatives.

## Code style

See `doc/coding.txt`. The rules that matter most:

- C90 with GNU extensions (`-std=gnu90`): declare variables at the top of a
  block. The codebase overwhelmingly uses `/* */` comments; match the
  surrounding file.
- Indent with 4 spaces. Never emit a hard tab in `.c`, `.h` or `.S`.
- **`int` is 16 bits on m68k** (`-mshort`) and 32 bits on ARM. Use the
  `portab.h` types `WORD`, `LONG`, `UBYTE`, `UWORD`, `ULONG`, `BOOL`, and
  suffix constants that must not overflow on m68k (`132 * 1000UL`).
- Assembler: leading underscore on symbols callable from C, CDECL calling
  conventions, 68000-only instructions, `movem.l` rather than `movem.w`.
- Trace with `KDEBUG(("..."))` or `KINFO(())` from `include/kprint.h`.

Further reading: `doc/install.txt` for the build system, `doc/status.txt` for
what currently works, `readme.md` for where this fork is going.
