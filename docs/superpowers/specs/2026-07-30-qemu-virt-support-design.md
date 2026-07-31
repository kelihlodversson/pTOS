# QEMU `virt` machine support — design

GitHub tracking issue: #24 (sub-issues #25 ARM virt, #26 m68k virt).

## Scope & goal

Two new machine targets that let pTOS boot and be smoke-tested under QEMU's
generic `virt` boards instead of emulated real hardware:

- `qemu-system-arm -M virt` (#25)
- `qemu-system-m68k -M virt` (#26)

Both are QEMU-only targets — no real hardware exists for either board — so
drivers only need to satisfy what QEMU's device models actually implement,
not real-world edge cases.

v1 scope for both: reach a running BIOS with a working serial console and a
ticking periodic timer/interrupt, matching roughly where the existing raspi
port already is (it boots and reaches AES launch, which then fails because
most of the VDI is non-functional). Disk (virtio-blk), networking, and
AES/desktop polish are follow-up work, not part of this design.

The two machines are different CPU architectures with no shared boot code
today (ARM virt can reuse `bios/arch/arm`; m68k virt shares nothing with the
existing Atari/Amiga/ColdFire boot chain). They are designed together here
for consistency, but are implemented and landed as two independent pieces of
work, ARM first.

## Kconfig additions

New entries in the `Machine` choice in `Kconfig.machine`, alongside
`MACHINE_RPI`:

```
config MACHINE_VIRT_ARM
	bool "QEMU virt (ARM)"
	help
	  QEMU's generic ARM 'virt' board (qemu-system-arm -M virt). No real
	  hardware; PL011 UART, GIC, and virtio-mmio at fixed addresses,
	  boots a -kernel ELF directly. The periodic tick uses the ARM
	  generic timer rather than board MMIO.

config MACHINE_VIRT_M68K
	bool "QEMU virt (m68k)"
	help
	  QEMU's generic m68k 'virt' board (qemu-system-m68k -M virt). No
	  Atari-compatible chipset; Goldfish PIC/RTC/TTY and virtio-mmio at
	  fixed addresses, boots a -kernel ELF directly.
```

- `ARCH_ARM` default gains `|| MACHINE_VIRT_ARM`.
- `ARCH_M68K` default gains `|| MACHINE_VIRT_M68K`. `MACHINE_VIRT_M68K` is
  **not** added to `CONF_ATARI_HARDWARE`'s default-y list — same treatment as
  `MACHINE_AMIGA` — since the board has no Atari chipset.
- That one exclusion cascades further than on ARM: `DETECT_NATIVE_FEATURES`,
  `CONF_WITH_BUS_ERROR`, `CONF_WITH_68030_PMMU`, and `CONF_WITH_68040_PMMU`
  all `depends on ... CONF_ATARI_HARDWARE`, so they switch off automatically
  with no per-defconfig override needed. Unlike `virt-arm_defconfig`, which
  has to explicitly turn off `CONF_WITH_CLI` and `CONF_WITH_LINEA` (their
  only implementations, `cmdasm.S`/`linea.S`, are m68k-only), `virt-m68k`
  keeps both **on** by default: it is `ARCH_M68K_CLASSIC`, so those
  implementations apply unchanged. `CONF_WITH_IDE=n` is still needed in
  `configs/virt-m68k_defconfig` (no IDE controller on this board), matching
  the ARM defconfig's reasoning.
- New `Kconfig.image` entries, following the existing `TARGET_RPI_KERNEL`
  precedent (`EMUTOS_LIVES_IN_RAM = y`, flat/ELF output, no ROM header):
  - `TARGET_VIRT_ARM_KERNEL` depends on `MACHINE_VIRT_ARM`
  - `TARGET_VIRT_M68K_KERNEL` depends on `MACHINE_VIRT_M68K`
- New defconfigs: `configs/virt-arm_defconfig`, `configs/virt-m68k_defconfig`.

## Directory layout & boot code

Two new machine subdirectories, mirroring `bios/machine/raspi/`:

### `bios/machine/virt-arm/`

Reuses `bios/arch/arm/` (vectors, exception dispatch, cache, `intmask.c`)
unchanged.

- New `startup.S` replaces the raspi one: no VideoCore mailbox call, no
  HYP-mode dance (QEMU `virt` starts the CPU already in SVC), sets up the
  vector base at a fixed RAM address. RAM size is a hardcoded constant
  matching the `-m` value used in the launch command for v1 (no FDT
  parsing yet — same "fixed by convention" approach the raspi port already
  takes with its documented `-bios kernel7.img` invocation).
- New `virt_uart.c` driving the PL011 UART at `0x09000000` for
  `KDEBUG`/console I/O.
- New `virt_pic.c` driving the GIC distributor (`0x08000000`) and CPU
  interface (`0x08010000`, GICv2) for interrupt handling.
- Periodic tick: the ARM generic timer (CP15/architectural, not board MMIO)
  rather than the PL031 RTC — simpler, portable across ARMv7+ cores, and
  implemented by every QEMU `virt` CPU model.

### `bios/machine/virt-m68k/`

New arch path; nothing existing to reuse, since it's the first non-Atari
-chipset, non-ColdFire m68k machine.

- New `startup.S`, modeled on `virt-arm`'s (a new file, not a derivative of
  `bios/arch/m68k/startup.S`, which is saturated with Atari-only steps: the
  Falcon reset-instruction dance, ST-MMU bank-register probe, cartridge
  detection, PMMU/cache teardown for real 68020-60 hardware). None of that
  applies: QEMU's `-kernel` loader jumps straight to the ELF entry point
  with the CPU already in supervisor mode, so there is no hardware
  reset-vector fetch to satisfy. The `OSHEADER` fields (`_os_entry`,
  `reseth`, `os_magic`, etc.) stay in the same shape as every other machine
  purely for TOS-header-format compatibility — they do **not** need to
  double as the real CPU reset vector the way ROM-resident Atari boot
  requires, since nothing reads vectors 0/1 from physical address 0 here.
  Needed steps: disable interrupts, set the initial supervisor stack
  pointer, reset the VBR to 0, jump to `_biosmain`. `bios/arch/m68k/vectors.S`
  (`init_exc_vec`/`init_user_vec`) is architecture-generic — it writes the
  vector 2-63 default table and vectors 64-255 user table into RAM, with no
  Atari-hardware assumptions — and is already invoked later from shared
  `bios/bios.c`, not from machine-specific `startup.S`, so it needs no
  changes and no early call from the new file.
- **Cross-reference from the ARM plan, resolved:** `tosvars.ld` needs no
  changes. It already branches on `#if ARCH_ARM` vs. a plain `#else` for
  the classic m68k layout; `MACHINE_VIRT_M68K` sets `ARCH_M68K` (not
  `ARCH_ARM`), so it falls into the `#else` branch — the same tightly
  packed, 2-byte-aligned layout every Atari/Amiga/ColdFire target already
  uses — automatically, with no new arch conditional needed.
  `emutos.ld` similarly needs **no changes**: `MACHINE_VIRT_M68K` is
  neither `MACHINE_RPI` nor `MACHINE_VIRT_ARM`, so it falls into the
  existing "classic" branches for both the `.text`/`ROM_ORIGIN` placement
  and the `.stack` section (2 KiB, placed in low `stram` before `.text`) —
  the same path `TARGET_PRG` and every other RAM-resident, non-ROM m68k
  target already exercises successfully. This works because m68k `virt`'s
  RAM sits at physical `0x0`, exactly where `stram` is already anchored;
  unlike ARM `virt` (RAM at `0x40000000`), there is no VMA/LMA split or
  MMU bootstrap trick needed to make the fixed-address TOS system
  variables land in real memory. No PMMU is required for v1 either:
  `CONF_WITH_68040_PMMU` (and `CONF_WITH_68030_PMMU`) `depends on
  CONF_ATARI_HARDWARE`, which `MACHINE_VIRT_M68K` deliberately doesn't set
  (see Kconfig section below), so both stay off without any extra
  handling.
- New `goldfish_tty.c` driving the Goldfish TTY device at `0xff008000` for
  console I/O.
- New `goldfish_pic.c` driving the Goldfish PIC (6 instances at
  `0xff000000`, mapped to CPU autovector IRQ levels 1–6) — this maps
  naturally onto the m68k's native 7-level autovector scheme.
- New `goldfish_rtc.c` driving the Goldfish RTC at `0xff006000` for the
  periodic tick.

### Boot convention

Both link the image as ELF with the entry point at the reset vector and rely
on QEMU's `-kernel` loader to jump straight there — no ROM header/magic
parsing, no floppy/cartridge boot chain. This is an incremental variation on
the existing `TARGET_PRG`/`TARGET_RPI_KERNEL` pattern (flat image loaded
directly into RAM), not a new build concept.

## Shared code / future virtio-mmio driver

Not needed for v1 (console + timer only), but worth placing correctly now so
#25 and #26 don't duplicate it later: virtio-mmio's register layout and
virtqueue descriptor format are architecture-independent; the only
architecture-specific bit is byte order (virtio 1.0+ registers are
little-endian regardless of guest endianness). `include/endian.h` already
abstracts exactly this (`le2cpu32`/`cpu2le16`/etc.), and is already used by
`usb/` and `bios/disk.c` for the same LE-hardware-on-BE-CPU problem on m68k.

Recommendation for the follow-up virtio work (separate issue, after both v1
machines land): put the transport driver in `util/` (shared, non-machine
-specific) rather than duplicating it under each `machine/virt-*/`, gated by
a new `CONF_WITH_VIRTIO` option depended on by both `MACHINE_VIRT_ARM` and
`MACHINE_VIRT_M68K`.

## Build system wiring

- `Makefile`: add `MACHINE-$(MACHINE_VIRT_ARM) += virt-arm` and
  `MACHINE-$(MACHINE_VIRT_M68K) += virt-m68k`, following the existing
  `MACHINE-$(MACHINE_RPI) += raspi` line.
- Each new `bios/machine/virt-*/build.mk` lists its `obj-y`, with
  `startup.o` first per the documented link-order rule.
- `readme.md` gains the new QEMU invocations next to the existing raspi one.

## Testing / definition of done

Per sub-issue:

- ARM: `make virt-arm_defconfig && make` produces an ELF image;
  `qemu-system-arm -M virt -cpu cortex-a7 -kernel <image> -serial stdio -d guest_errors`
  boots to the same "BIOS runs, AES launch fails" point the raspi port
  currently reaches, with visible `KDEBUG` output on the serial console.
- m68k: `make virt-m68k_defconfig && make` produces an ELF image;
  `qemu-system-m68k -M virt -kernel <image> -serial stdio -d guest_errors`
  reaches the same point.

## Reference material gathered during design

- ARM `virt` memory map (from QEMU's `hw/arm/virt.c` `base_memmap`): flash
  at `0x0` (128 MiB), GIC distributor `0x08000000`, GIC CPU interface
  `0x08010000`, UART0 (PL011) `0x09000000`, RTC (PL031) `0x09010000`,
  virtio-mmio region starting `0x0a000000` (0x200 bytes/device, IRQ base 16,
  contiguous), RAM starting `0x40000000`.
- m68k `virt` memory map (from QEMU's `hw/m68k/virt.c`): RAM at `0x0`
  (up to ~3.2 GiB), Goldfish PIC `0xff000000`–`0xff005fff` (6 instances,
  mapped to CPU IRQ levels 1–6), Goldfish RTC `0xff006000`, Goldfish TTY
  `0xff008000`, virtio-mmio region from `0xff010000` (128 devices, 0x200
  bytes each). CPU default is m68040 (m68020/030/060 also selectable);
  single CPU only. Boot loads an ELF directly to its entry point via
  `-kernel`, no BIOS/boot ROM required.
