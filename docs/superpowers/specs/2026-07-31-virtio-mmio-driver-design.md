# Shared virtio-mmio driver for the QEMU virt ports — design

GitHub tracking issue: #29. Follow-up to #25 (ARM virt) and #26 (m68k virt),
both landed. Reference: `docs/superpowers/specs/2026-07-30-qemu-virt-support-design.md`,
"Shared code / future virtio-mmio driver" section, which scoped this work but
deliberately left it for a separate issue.

## Scope & goal

Both QEMU `virt` boards expose virtio-mmio transports at fixed addresses.
This adds:

- A shared, architecture-neutral virtio-mmio **transport** driver.
- A **virtio-blk** device driver on top of it, wired into `bios/disk.c` as a
  new bus type, so a virtio disk shows up and partitions exactly like an
  ACSI/IDE/SD device does today.

Non-goals for this issue: virtio-net (explicitly deferred by #29 itself),
legacy (pre-1.0/pre-version-2) virtio-mmio register layout, multi-queue,
device hot-plug. QEMU's `virt` boards default to modern (version 2)
virtio-mmio, so only `VIRTIO_F_VERSION_1` needs to be negotiated.

Completion is **interrupt-driven**, not polled. This is the one point where
this design diverges from doing the least possible work: a polled/busy-wait
design would need zero interrupt-controller changes on either board, but
was rejected in favor of real interrupts. Section "Interrupt wiring" below
is the direct consequence of that choice and is the bulk of the new code in
this change, especially on m68k.

## Layout

```
util/virtio.c, util/virtio.h            -- transport (arch-neutral)
bios/virtio_blk.c, bios/virtio_blk.h    -- virtio-blk driver, disk.c bus glue
bios/machine/virt-arm/virt_pic.{c,h}    -- extended: SPI support
bios/machine/virt-m68k/goldfish_pic.{c,h}  -- extended: shared-line dispatch
bios/machine/virt-m68k/goldfish_pic_isr.S  -- new: per-instance ISR stubs
```

`util/virtio.c` has no knowledge of block devices, request formats, or
machine addresses — it only knows the virtio-mmio register layout and the
virtqueue descriptor/avail/used-ring format, both of which are identical
across every virtio device type and both boards. `bios/virtio_blk.c` is the
first (only, for now) consumer; it lives in `bios/` alongside `sd.c`,
`ide.c`, and `raspi_emmc.c` — not under either `bios/machine/virt-*/` — since
`disk.c` and its sibling bus drivers are already all there, and the driver
itself isn't machine-specific beyond three `#ifdef MACHINE_VIRT_ARM` /
`MACHINE_VIRT_M68K` constants for the mmio base/stride/count, matching the
`MACHINE_*` `#ifdef`-for-machine-selection convention in `CLAUDE.md`.

## Kconfig

In `bios/Kconfig`, alongside `CONF_WITH_IDE`/`CONF_WITH_SDMMC`:

```
config CONF_WITH_VIRTIO
	bool "virtio-mmio transport support"
	depends on MACHINE_VIRT_ARM || MACHINE_VIRT_M68K
	default y
	help
	  Shared driver for the virtio-mmio transports QEMU's ARM and m68k
	  'virt' boards expose. No effect without a device driver built on
	  top of it (see CONF_WITH_VIRTIO_BLK).

config CONF_WITH_VIRTIO_BLK
	bool "virtio-blk disk driver"
	depends on CONF_WITH_VIRTIO
	default y
	help
	  Block device driver for virtio-blk devices found on the
	  virtio-mmio transport. Devices appear as a new disk bus,
	  partitioned the same way as ACSI/IDE/SD devices.
```

Both default to `y`: on either `virt` board, if nothing is attached on the
QEMU command line the probe loop simply finds no matching device and
`VIRTIO_BUS` stays empty, matching how e.g. `CONF_WITH_SDMMC` already
behaves when no card is present.

`disk.h` gets a 5th bus:

```c
#define ACSI_BUS   0
#define SCSI_BUS   1
#define IDE_BUS    2
#define SDMMC_BUS  3
#define VIRTIO_BUS 4
#define MAX_BUS    VIRTIO_BUS
```

`DEVICES_PER_BUS` (already 8) is reused as-is as the cap on virtio-blk units;
no new constant needed.

## Transport driver (`util/virtio.h`/`.c`)

Register layout (modern virtio-mmio, offsets from a device's base):

```
0x000 MagicValue (must read "virt" / 0x74726976)
0x004 Version (must be 2)
0x008 DeviceID (2 = block device; 0 = slot unused)
0x010/0x014 DeviceFeatures / DeviceFeaturesSel
0x020/0x024 DriverFeatures / DriverFeaturesSel
0x030 QueueSel, 0x034 QueueNumMax, 0x038 QueueNum, 0x044 QueueReady
0x050 QueueNotify
0x060/0x064 InterruptStatus / InterruptACK
0x070 Status
0x080/0x084, 0x090/0x094, 0x0a0/0x0a4  QueueDesc/QueueDriver/QueueDevice (lo/hi)
0x100+ device-specific config space
```

Public API (shape, not final signatures):

```c
BOOL virtio_probe(ULONG base, UWORD want_device_id, struct virtio_dev *out);
BOOL virtio_setup_queue(struct virtio_dev *dev, UWORD queue_size);
void virtio_submit(struct virtio_dev *dev, struct virtio_desc *chain, UWORD n);
void virtio_notify(struct virtio_dev *dev);
/* called from the board's IRQ dispatch once the ISR identifies the device */
void virtio_handle_interrupt(struct virtio_dev *dev);
```

One statically allocated split virtqueue per `struct virtio_dev` (queue size
8: enough for a 3-descriptor request chain with headroom, small enough that
`DEVICES_PER_BUS` (8) static instances cost only a few KiB total). No heap:
`disk_init_all()` runs well before any general allocator would be available,
matching how every other bus driver already works.

Feature negotiation reads `DeviceFeatures`, ANDs in only
`VIRTIO_F_VERSION_1`, writes it back as `DriverFeatures`, and requires the
`FEATURES_OK` status bit to stick — if the device rejects it, the slot is
treated as absent, same as a failed magic/version/device-id check.

All multi-byte register and descriptor fields are little-endian by spec,
regardless of guest endianness. Every access goes through
`include/endian.h` (`le2cpu32`/`cpu2le32`/`le2cpu16`/`cpu2le16`) — a no-op on
`virt-arm` (LE CPU), real byteswaps on `virt-m68k` (BE CPU) — the same
pattern `usb/ucd_dwc2.c` and `raspi_emmc.c` already use for LE-hardware-on-
BE-CPU.

## Interrupt wiring

### ARM: extending `virt_pic.c`

Today `virt_pic.c` only handles GIC PPIs (IRQ 0–31): `GICD_ISENABLER0`/
`ICENABLER0` are hardcoded single registers, and there's no `ITARGETSR`
write because PPIs are implicitly per-CPU and don't need target-CPU routing.
virtio-mmio needs GIC SPIs 48–79 (SPI 16+i for transport i, confirmed from
QEMU's `hw/arm/virt.c`: base `0x0a000000`, stride `0x200`, 32 transports,
`irqmap[VIRT_MMIO] = 16`).

Changes: generalize `GICD_ISENABLER0`/`ICENABLER0` into `GICD_ISENABLER(n)`/
`ICENABLER(n)` macros that pick the right word for any IRQ number; add a
`GICD_ITARGETSR(n) = 0x01` (target CPU0) write in `virt_connect_irq` when
`irq >= 32`; grow `VIRT_IRQ_LINES` to 80. `virt_int_handler`'s
`GICC_IAR`-keyed dispatch and `virt_connect_irq`'s per-IRQ handler-table
shape are unchanged — this is a capacity/generality extension, not a
rewrite.

### m68k: extending `goldfish_pic.c`

QEMU's `hw/m68k/virt.c` wires the 128 virtio-mmio transports across Goldfish
PIC instances 1–4 (32 lines each; instance 0 already carries the TTY,
instance 5 already carries the RTC — both claimed by the existing boot
work). Each PIC instance drives one CPU autovector level (instance N → level
N+1), confirmed against this repo's own `goldfish_pic.c` comment and QEMU
source.

This is genuinely new infrastructure, not an extension: the only existing
precedent, `goldfish_rtc_isr.S`, is a single dedicated stub hardcoded to one
fixed line (level 6, PIC instance 5, bit 0) with no notion of a shared line
carrying multiple devices. virtio needs bit-level dispatch within a PIC
instance. Additions:

- `goldfish_pic.c`: a `STATUS` register read (offset `0x00`, one bit per
  line) and a `goldfish_pic_connect_irq(pic_index, bit, handler)` API that
  records the handler in a per-instance table and lazily installs that
  instance's ISR stub into its `VEC_LEVELn` sysvar on first use — mirroring
  `virt_connect_irq`'s per-IRQ handler-table shape on the ARM side.
- `goldfish_pic_isr.S`: one small stub per PIC instance actually wired
  (only instances among 1–4 that end up hosting a registered device, not
  all four unconditionally) — register save, `jsr` into a C dispatcher for
  that instance (reads `STATUS`, calls the matching handler, acks the bit),
  restore, `rte`. Structurally like `goldfish_rtc_isr.S` but simpler: it
  doesn't need that file's tail-jump-into-`int_timerc` dance, since that
  exists only because the RTC interrupt doubles as EmuTOS's system timer
  tick — virtio has no such chaining requirement.

`VEC_LEVEL2`–`VEC_LEVEL5` (`bios/vectors.h`) are unused on `virt-m68k` today
(no MFP/HBL/VBL — this machine doesn't set `CONF_ATARI_HARDWARE`), so there
is no conflict with existing vector usage.

## virtio-blk driver (`bios/virtio_blk.c`/`.h`)

Board addresses, picked by `#ifdef` (constants only; the transport itself
stays board-agnostic):

```c
#if defined(MACHINE_VIRT_ARM)
#define VIRTIO_MMIO_BASE    0x0a000000UL
#define VIRTIO_MMIO_STRIDE  0x200UL
#define VIRTIO_MMIO_COUNT   32
#elif defined(MACHINE_VIRT_M68K)
#define VIRTIO_MMIO_BASE    0xff010000UL
#define VIRTIO_MMIO_STRIDE  0x200UL
#define VIRTIO_MMIO_COUNT   128
#endif
```

`virtio_blk_init()` scans up to `VIRTIO_MMIO_COUNT` slots calling
`virtio_probe(base, 2 /* block device */, &dev)`, registering each match as
the next `VIRTIO_BUS` unit up to `DEVICES_PER_BUS` (8), and connecting its
IRQ (`virt_connect_irq`/`goldfish_pic_connect_irq`, board-specific, called
from `virtio_blk.c` since it's the one place that already knows both the
board and the slot index → IRQ mapping) to a shared
`virtio_blk_interrupt(unit)` handler that calls `virtio_handle_interrupt()`
and sets that unit's completion flag.

Request format (virtio-blk spec, unchanged by board):

```c
struct virtio_blk_req {   /* all fields LE on the wire */
    ULONG type;      /* VIRTIO_BLK_T_IN = 0, VIRTIO_BLK_T_OUT = 1 */
    ULONG reserved;
    QUAD  sector;
};
```

A 3-descriptor chain per request: header (device-readable) → caller's data
buffer from `disk_rw`'s `buf` argument, no bounce buffer (device-readable
for writes, device-writable for reads) → 1-byte status (device-writable).
`virtio_blk_rw()` fills the chain, calls `virtio_submit`/`virtio_notify`,
then blocks on the unit's completion flag — a `WFI`-style wait on ARM
(matching `virt_pic.c`'s "let a real interrupt wake the CPU" model, the
actual behavioral difference from the rejected polled design), a plain
wait loop on m68k (no equivalent low-power wait instruction is currently
used elsewhere in this port). `virtio_blk_init/_ioctl/_rw` match the
existing bus-driver signature shape (`sd.c`/`raspi_emmc.c`, not `ide.c`'s
extra `need_byteswap` parameter — byte order is handled internally via
`endian.h`). 512-byte sectors only for v1: `SECTOR_SIZE` is assumed
throughout `disk.c` already; a device advertising a non-512 `blk_size` in
its config space is rejected at probe time rather than handled generically.

### Cache coherency

`virt-arm` boots with the D-cache enabled (`bios/machine/virt-arm/virt_mmu.c`);
QEMU's virtio-mmio backend reads/writes guest RAM directly, so stale dirty
cache lines or stale clean lines around a submitted buffer are a real bug,
not a theoretical one. `bios/arch/arm/cache_armv7.c` already exports
range-based `flush_data_cache(start, size)` / `invalidate_data_cache(start,
size)`; `virtio_submit` flushes the descriptor chain and any
device-readable buffer before notifying, and invalidates any
device-writable buffer (read data, the used-ring entry) after the
completion interrupt. `virt-m68k` boots with caches off
(`bios/machine/virt-m68k/startup.S` explicitly does no cache setup), so no
maintenance is needed there — the cache calls are `#if ARCH_ARM`-only.

## Build system wiring

```make
# util/build.mk
obj-$(CONF_WITH_VIRTIO) += virtio.o

# bios/build.mk
obj-$(CONF_WITH_VIRTIO_BLK) += virtio_blk.o
obj-$(MACHINE_VIRT_M68K)    += goldfish_pic_isr.o   # alongside the other goldfish_*.o entries
```

`disk.c` gets a `#if CONF_WITH_VIRTIO_BLK` include of `virtio_blk.h` and
three new dispatch arms (`internal_inquire()`, the ioctl switch, `disk_rw()`)
following the existing `IS_xxx_DEVICE(major)` pattern used for the other
four buses.

`configs/virt-arm_defconfig` and `configs/virt-m68k_defconfig`: no changes
expected, since both new options default to `y` under their respective
machines; verified by running `make savedefconfig` after wiring the Kconfig
and diffing against the current files.

`readme.md` gains `-drive`/`-device virtio-blk-device` additions to the
existing QEMU invocations for both boards.

## Testing / definition of done

```sh
qemu-system-arm -M virt -cpu cortex-a7 -kernel <image> -serial stdio -d guest_errors \
  -drive file=disk.img,if=none,format=raw,id=hd0 -device virtio-blk-device,drive=hd0

qemu-system-m68k -M virt -kernel <image> -serial stdio -d guest_errors \
  -drive file=disk.img,if=none,format=raw,id=hd0 -device virtio-blk-device,drive=hd0
```

Definition of done: `disk_init_all()`'s boot-time probe finds the virtio-blk
unit, reads its MBR/AHDI partition table successfully (visible via
`KDEBUG`), and a read and write through the normal `disk_rw()` path
round-trip correctly (verified by writing a known pattern, rebooting QEMU,
and reading it back).
