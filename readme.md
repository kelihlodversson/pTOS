# pTOS — Portable TOS

pTOS is a portable, open-source TOS-compatible operating system derived from [EmuTOS](https://github.com/emutos/emutos).

The project preserves the familiar GEMDOS, BIOS/XBIOS, VDI and AES environment while extending it beyond the original Atari hardware platform. pTOS targets classic Atari-compatible machines, other m68k systems, ARM boards and virtual hardware platforms.

The **p** stands first and foremost for **portable**.

Raspberry Pi was the first non-m68k target, but pTOS is no longer intended to be a Raspberry Pi-specific port. The long-term direction is a shared operating-system architecture in which machine-specific code discovers and exposes hardware while common GEMDOS, VDI and AES components provide the higher-level environment.

Parts of the Raspberry Pi support are derived from the [Circle bare-metal framework](https://github.com/rsta2/circle).

## Current status

pTOS is under active development. It is usable as a systems-development platform, but several ports and subsystems remain incomplete.

The repository currently contains configurations for:

- Atari-compatible m68k systems
- Amiga
- ColdFire systems
- Raspberry Pi 1–4
- QEMU ARM `virt`
- QEMU m68k `virt`

The exact level of runtime support varies between targets. Some configurations are primarily build-tested, while others boot far enough to run GEMDOS, VDI, AES and the desktop.

Current work includes:

- shared ARM and m68k operating-system infrastructure;
- native ARM and m68k ELF32 loading;
- QEMU `virt` support;
- shared modern virtio-mmio transport support;
- virtio block and input devices;
- PCI infrastructure;
- an ongoing transition toward runtime-selected VDI rendering backends;
- Raspberry Pi framebuffer support.

## Portable architecture

pTOS is moving away from machine-specific forks of common operating-system subsystems.

The intended structure is:

```text
Machine-specific startup and hardware discovery
                    ↓
          BIOS and device drivers
                    ↓
       Hardware and screen descriptors
                    ↓
       Shared GEMDOS, VDI and AES code
                    ↓
              GEM desktop
```

A new hardware port should normally provide:

- startup and interrupt handling;
- memory and MMU setup where required;
- hardware discovery;
- storage, input and other device drivers;
- a screen driver that describes the framebuffer.

Common code should then provide filesystem behaviour, process loading, graphics primitives, AES and the desktop.

## Graphics

Classic Atari graphics modes use interleaved bitplanes. Modern framebuffer devices generally use packed truecolor pixels.

The graphics architecture is being reworked so screen drivers report their actual mode using a descriptor containing:

- width and height;
- framebuffer pitch;
- bits per pixel;
- planar or packed layout;
- indexed or truecolor colour model;
- concrete pixel format.

VDI can then select an appropriate rendering backend at runtime instead of compiling machine-specific branches into shared drawing functions.

The first implementation slice is being developed in [PR #71](https://github.com/kelihlodversson/pTOS/pull/71). It introduces:

- `SCREEN_MODE_DESC`;
- runtime VDI backend selection;
- a backend wrapping the existing planar renderer;
- an initial packed RGB565 truecolor backend;
- Raspberry Pi migration from packed-indexed 8bpp to RGB565.

This work is not yet complete. The planar path remains the most mature rendering implementation.

Truecolor work still includes:

- text rendering;
- line drawing;
- software mouse cursor rendering;
- raster and BitBlt operations;
- palette and pseudo-palette semantics;
- additional pixel formats such as XRGB8888;
- runtime validation on physical Raspberry Pi hardware.

Completion of the shared truecolor rendering architecture is tracked in [issue #35](https://github.com/kelihlodversson/pTOS/issues/35).

## Executable formats

Classic m68k builds continue to support the traditional TOS program environment.

pTOS also includes a native ELF32 executable loader for supported targets. It can load:

- statically linked `ET_EXEC` files that retain relocation records;
- PIE-style `ET_DYN` executables without a dynamic linker;
- both ELF `REL` and `RELA` relocation encodings.

Native application ABI and toolchain work is still evolving. ELF support avoids requiring ARM applications to use an invented ARM variant of the classic Atari PRG relocation format.

See [`doc/elfload.txt`](doc/elfload.txt) for details.

## Building

The build system is configured similarly to the Linux kernel.

Select one of the configurations in `configs/`, optionally customise it with `make menuconfig`, and build:

```sh
make rpi2_defconfig
make
```

For the QEMU ARM `virt` target:

```sh
make virt-arm_defconfig
make
```

For the QEMU m68k `virt` target:

```sh
make virt-m68k_defconfig
make
```

`make help` lists the available configurations.

Detailed build and toolchain information is available in [`doc/install.txt`](doc/install.txt).

Typical requirements include:

- a suitable cross-compiler;
- Python 3;
- the `kconfiglib` Python module.

Install `kconfiglib` with:

```sh
pip3 install kconfiglib
```

## Running the regression tests

A small built-in regression test suite runs the same way across every
target. It is enabled by default; build the test image and boot it:

```sh
make rpi2_defconfig && make   # or any other config
make test-hd      # builds runtests.tos + test-hd.img
```

Attach `test-hd.img` to QEMU or Hatari alongside the normal kernel image and
it autoruns on boot, printing PASS/FAIL per test and a summary. See
[`tests/readme.md`](tests/readme.md) for emulator invocations, how to read
the output, and how to add a new test suite.

## Running QEMU ARM virt

Build the target:

```sh
make virt-arm_defconfig
make
```

The current non-LPAE ARM MMU cannot map the high PCI resources used by QEMU's default `highmem=on` layout. Until the Device Tree and LPAE work described below is implemented, use `highmem=off`:

```sh
qemu-system-arm \
  -M virt,highmem=off \
  -cpu cortex-a7 \
  -m 128 \
  -kernel virt-arm.elf \
  -d guest_errors \
  -serial stdio
```

To attach a virtio block device:

```sh
qemu-system-arm \
  -M virt,highmem=off \
  -cpu cortex-a7 \
  -m 128 \
  -kernel virt-arm.elf \
  -d guest_errors \
  -serial stdio \
  -global virtio-mmio.force-legacy=false \
  -drive file=disk.img,if=none,format=raw,id=hd0 \
  -device virtio-blk-device,drive=hd0
```

To attach virtio input devices:

```sh
qemu-system-arm \
  -M virt,highmem=off \
  -cpu cortex-a7 \
  -m 128 \
  -kernel virt-arm.elf \
  -d guest_errors \
  -serial stdio \
  -global virtio-mmio.force-legacy=false \
  -device virtio-keyboard-device \
  -device virtio-tablet-device
```

The shared virtio-mmio driver supports modern/version-2 virtio-mmio. On QEMU versions that default to legacy/version-1 transport behaviour, this option is required:

```text
-global virtio-mmio.force-legacy=false
```

The current `virt-arm` framebuffer exists only in guest memory and is not yet connected to a visible QEMU display device.

Visible graphical output through QEMU `ramfb` is planned in [issue #68](https://github.com/kelihlodversson/pTOS/issues/68).

## Running QEMU m68k virt

Build the target:

```sh
make virt-m68k_defconfig
make
```

Run it with:

```sh
qemu-system-m68k \
  -M virt \
  -m 128 \
  -cpu m68020 \
  -kernel virt-m68k.elf \
  -d guest_errors \
  -serial stdio
```

To attach a virtio block device:

```sh
qemu-system-m68k \
  -M virt \
  -m 128 \
  -cpu m68020 \
  -kernel virt-m68k.elf \
  -d guest_errors \
  -serial stdio \
  -global virtio-mmio.force-legacy=false \
  -drive file=disk.img,if=none,format=raw,id=hd0 \
  -device virtio-blk-device,drive=hd0
```

To attach virtio input devices:

```sh
qemu-system-m68k \
  -M virt \
  -m 128 \
  -cpu m68020 \
  -kernel virt-m68k.elf \
  -d guest_errors \
  -serial stdio \
  -global virtio-mmio.force-legacy=false \
  -device virtio-keyboard-device \
  -device virtio-tablet-device
```

## Raspberry Pi

Raspberry Pi remains an important physical ARM target.

Available configurations include:

- `rpi1_defconfig`
- `rpi2_defconfig`
- `rpi3_defconfig`
- `rpi4_defconfig`

The current graphics work moves Raspberry Pi from an 8bpp packed-indexed framebuffer to RGB565 truecolor and the shared runtime-selected VDI backend architecture.

The truecolor path currently builds, but visual correctness has not yet been validated on real Raspberry Pi hardware.

QEMU's Raspberry Pi machine support varies considerably between QEMU versions and is not the primary reference environment for current virtual-machine development. Testing uses a mixture of cross-build checks, QEMU `virt` machines and physical hardware.

## Near-term roadmap

The open issues represent the immediate development horizon rather than completed features.

### Complete runtime-dispatched truecolor VDI support

Tracked in [issue #35](https://github.com/kelihlodversson/pTOS/issues/35).

The descriptor, dispatch layer, planar backend and initial RGB565 implementation form the foundation.

Remaining work includes:

- text and text effects;
- lines and drawing modes;
- cursor save, restore and rendering;
- raster operations and BitBlt;
- colour and pseudo-palette handling;
- XRGB8888 and other truecolor formats;
- removal of the remaining old compile-time packed-pixel paths;
- broader runtime validation.

### Visible QEMU graphics for virt-arm

Tracked in [issue #68](https://github.com/kelihlodversson/pTOS/issues/68).

The planned implementation will:

- communicate with QEMU through `fw_cfg`;
- configure the `etc/ramfb` framebuffer;
- expose a packed truecolor screen mode;
- use the shared VDI backend rather than machine-specific drawing primitives;
- display the pTOS desktop in a QEMU graphical window.

QEMU `ramfb` normally uses XRGB8888, so this work also depends on generic XRGB8888 support in the shared truecolor backend.

### Device Tree hardware discovery on ARM virt

Tracked in [issue #75](https://github.com/kelihlodversson/pTOS/issues/75).

Parts of the QEMU ARM `virt` hardware layout are currently represented by fixed addresses.

The planned Device Tree work will discover:

- RAM;
- GIC;
- UART;
- virtio-mmio transports;
- PCI ECAM and PCI resource windows;
- `fw_cfg` and other platform devices where described.

Resources that cannot safely be addressed by the current MMU should be rejected without crashing the system.

### ARM32 LPAE and high-address MMIO

Tracked in [issue #76](https://github.com/kelihlodversson/pTOS/issues/76).

QEMU can place PCI ECAM and MMIO resources above 4 GiB. The current ARM32 short-descriptor MMU cannot map those addresses.

Planned work includes:

- ARMv7 LPAE page tables;
- explicit physical, bus and virtual address types;
- dynamic device mappings;
- `ioremap()` and `iounmap()`;
- high-address PCI ECAM and BAR support;
- continued 32-bit kernel and userspace pointers.

### QEMU boot regression testing

The build matrix checks whether configurations compile, but compilation alone does not prove that they boot.

A [regression test harness](tests/readme.md) now runs behavioral checks under an emulator (`make test-hd`), but CI does not yet build and boot it automatically as part of the matrix. Wiring that in is the remaining piece: a short QEMU smoke-boot test should eventually verify that key virtual targets:

- reach a known boot milestone;
- do not panic;
- do not trigger early data aborts;
- remain usable after changes to MMU, PCI and device discovery code.

## Contributing

Help is highly appreciated.

Useful areas include:

- bare-metal ARM and m68k work;
- MMU and memory management;
- VDI and graphics;
- QEMU devices;
- PCI and virtio;
- storage and USB drivers;
- native application toolchains;
- testing on physical hardware.

Please review the open issues before starting substantial work. Several areas now have an agreed architecture, and new implementations should extend shared subsystems rather than add machine-specific alternatives.

## Origins and licence

pTOS is derived from [EmuTOS](https://github.com/emutos/emutos).

Parts of the Raspberry Pi implementation are derived from the [Circle bare-metal framework](https://github.com/rsta2/circle).

See [`doc/license.txt`](doc/license.txt) and the individual source-file headers for licensing information.
