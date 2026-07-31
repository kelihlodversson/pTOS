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

- New `startup.S` sets up the supervisor stack and vector table per the
  classic m68k exception table conventions already used in
  `bios/arch/m68k`, but skips all Atari MFP/ACIA setup. RAM starts at
  `0x0` on this board, so the reset vector table doubles as the pTOS
  `OSHEADER` the way the classic Atari boot does; needs verifying against
  `bios/startup.S` for layout compatibility during implementation, and a
  variant written if it doesn't line up directly.
- **Cross-reference from the ARM plan:** `tosvars.ld` hardcodes ~147 TOS
  system-variable addresses as absolute constants in `0x380`–`0x800`
  (see the ARM implementation plan's memory-layout task), and the
  classic ROM-based Atari targets (`TARGET_192`/`256`/`512`/`CART`)
  already split this into two distinct regions: `stram` (origin `0`,
  holding the fixed-address sysvars) and `rom` (origin `0x00e00000`,
  holding `.text`). Whether m68k `virt` needs anything beyond that
  existing two-region split — it does have real RAM at physical `0x0`,
  unlike ARM `virt`, so it may just work unmodified — or needs its own
  address-translation trick (the board's default CPU, m68040, has a
  PMMU already used elsewhere in this codebase via
  `CONF_WITH_68040_PMMU`) should be checked explicitly when #26 is
  designed, rather than assumed from the ARM plan.
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
