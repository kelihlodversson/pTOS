# Shared virtio-mmio driver Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a shared virtio-mmio transport driver and a virtio-blk device driver on top of it, so both QEMU `virt` boards (ARM and m68k) can see a disk through `bios/disk.c`'s normal bus/partition machinery.

**Architecture:** `util/virtio.c` is an architecture-neutral transport (probe, feature negotiation, one static virtqueue per device, submit/notify/interrupt-handling). `bios/virtio_blk.c` is the block-device consumer, registered into `disk.c` as a 5th bus (`VIRTIO_BUS`). Completion is interrupt-driven, which requires extending `bios/machine/virt-arm/virt_pic.c` (GIC SPI support — today PPI-only) and `bios/machine/virt-m68k/goldfish_pic.c` (new shared-line dispatch — today only a single dedicated-line precedent exists, for the RTC).

**Tech Stack:** C90/gnu90 freestanding (no libc), m68k and ARM GNU cross assemblers. No host-side test framework exists for this codebase — verification is build success plus booting the actual image under QEMU and reading `KDEBUG` serial output, exactly as the rest of this port has been verified (see `git log --oneline -- bios/machine/virt-arm bios/machine/virt-m68k`).

## Global Constraints

- C90 with GNU extensions (`-std=gnu90`): all declarations at the top of a block. Match the surrounding file's comment style (`/* */`).
- 4-space indentation, no tabs, in `.c`/`.h`/`.S`. Run `make gitready` before every commit.
- Use `portab.h` types (`WORD`, `LONG`, `UBYTE`, `UWORD`, `ULONG`, `BOOL`, `QUAD`, `PFVOID`) — never bare `int`/`long`.
- Every multi-byte virtio-mmio register or in-memory virtqueue field is little-endian by spec, regardless of guest endianness. Always go through `include/endian.h` (`le2cpu32`/`cpu2le32`/`le2cpu16`/`cpu2le16`); never read/write a multi-byte field directly.
- `util/` code must not `#include` any `bios/` header (matches how `util/` is used today — no existing `util/*.c` does this). Where `util/virtio.c` needs ARM cache-maintenance functions defined in `bios/`, use local `extern` declarations, not a `bios/processor.h` include.
- Modern virtio-mmio only: require `Version == 2` at probe time; negotiate only `VIRTIO_F_VERSION_1` (bit 32). No legacy (version 1) support.
- `VIRTIO_QUEUE_SIZE` is 8 throughout (descriptors per virtqueue) — fixed, not negotiated down from `QueueNumMax`; if a device advertises less, treat it as absent.
- `DEVICES_PER_BUS` (already defined as 8 in `disk.h`) is the cap on virtio-blk units; do not add a new constant for this.
- 512-byte sectors only (`SECTOR_SIZE` in `disk.h`); a device whose config-space `blk_size` isn't 512 is rejected at probe time (out of scope for v1 — not needed since QEMU's virtio-blk defaults to 512 and no config-space size check is implemented at all in this plan; see Task 2).
- ARM `virt` boots with D-cache on; every buffer the virtio device DMAs into/out of needs explicit `flush_data_cache()`/`invalidate_data_cache()` (from `bios/processor.h`, `bios/arch/arm/cache_armv7.c`) around the submit/complete boundary. m68k `virt` boots with caches off — those calls are `#if ARCH_ARM`-only and must not appear unconditionally.
- Follow the existing bus-driver signature shape exactly: `void xxx_init(void); LONG xxx_ioctl(UWORD drv, UWORD ctrl, void *arg); LONG xxx_rw(WORD rw, LONG sector, WORD count, UBYTE *buf, WORD dev);` (see `bios/sd.h`, `bios/raspi_emmc.h`).
- Any shared-tree file this plan touches (`bios/disk.c`, `bios/disk.h`, `bios/Kconfig`, `util/build.mk`, `bios/build.mk`) compiles for **every** machine, not just the two virt boards — new code must be `#if CONF_WITH_VIRTIO`/`#if CONF_WITH_VIRTIO_BLK`-gated so it fully disappears on Atari/Amiga/ColdFire/raspi builds.

---

## Task 1: Transport probe/negotiate + build wiring + discovery-only virtio-blk

**Files:**
- Create: `util/virtio.h`, `util/virtio.c`
- Create: `bios/virtio_blk.h`, `bios/virtio_blk.c`
- Modify: `bios/machine/virt-arm/virt_memmap.h`
- Modify: `util/build.mk`
- Modify: `bios/build.mk`
- Modify: `bios/Kconfig`
- Modify: `bios/disk.h`
- Modify: `bios/disk.c`
- Modify: `Makefile` — add `bios_copts = -Iutil` (next to the existing `usb_copts` line). Without this, `bios/virtio_blk.c`'s `#include "virtio.h"` can't resolve, since the per-directory include search only adds a source directory's own machine/arch/generic paths, not other top-level directories.
- Modify: `bios/blkdev.c` — inside `bus_init()`, right after the existing `raspi_emmc_init()` call, add:
  ```c
  #if CONF_WITH_VIRTIO_BLK
      virtio_blk_init();
  #endif
  ```
  (with `#include "virtio_blk.h"` added to the top of the file). `disk_init_all()` never calls per-bus init functions itself — every bus driver registers its init call in `blkdev.c`'s `bus_init()` — so without this, `virtio_blk_init()` is defined but never invoked and the boot-test in Step 12 finds nothing.

**Interfaces:**
- Produces (from `util/virtio.h`, used by Task 2/3):
  ```c
  #define VIRTIO_QUEUE_SIZE 8
  #define VIRTIO_DESC_F_NEXT   1
  #define VIRTIO_DESC_F_WRITE  2
  typedef struct { ULONG addr_lo, addr_hi, len; UWORD flags, next; } VIRTIO_DESC;
  typedef struct { UWORD flags, idx, ring[VIRTIO_QUEUE_SIZE], used_event; } VIRTIO_AVAIL;
  typedef struct { ULONG id, len; } VIRTIO_USED_ELEM;
  typedef struct { UWORD flags, idx; VIRTIO_USED_ELEM ring[VIRTIO_QUEUE_SIZE]; UWORD avail_event; } VIRTIO_USED;
  typedef struct {
      ULONG base;
      UWORD last_used_idx;
      volatile BOOL done;
      VIRTIO_DESC  desc[VIRTIO_QUEUE_SIZE];
      VIRTIO_AVAIL avail;
      VIRTIO_USED  used;
  } VIRTIO_DEV;
  BOOL virtio_probe(ULONG base, UWORD want_device_id, VIRTIO_DEV *dev);
  ```
  (`virtio_setup_queue`/`virtio_desc_set`/`virtio_submit`/`virtio_notify`/`virtio_handle_interrupt` are declared here too but implemented in Task 2.)
- Produces (from `bios/virtio_blk.h`): `void virtio_blk_init(void); LONG virtio_blk_ioctl(UWORD drv, UWORD ctrl, void *arg); LONG virtio_blk_rw(WORD rw, LONG sector, WORD count, UBYTE *buf, WORD dev);`
- Produces (from `bios/disk.h`): `#define VIRTIO_BUS 4`, `#define MAX_BUS VIRTIO_BUS`, `#define IS_VIRTIO_DEVICE(major) (GET_BUS(major) == VIRTIO_BUS)`.

- [ ] **Step 1: Write `util/virtio.h`**

```c
/*
 * virtio.h - shared virtio-mmio transport (modern/version-2 only)
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef VIRTIO_H
#define VIRTIO_H

#include "portab.h"

#define VIRTIO_QUEUE_SIZE  8   /* descriptors/ring slots per queue; power of two */

#define VIRTIO_DESC_F_NEXT   1
#define VIRTIO_DESC_F_WRITE  2

/* One virtqueue descriptor. Fields are stored LE-encoded (see virtio_desc_set()
 * in virtio.c) since the device reads this memory directly, regardless of
 * guest endianness. */
typedef struct
{
    ULONG addr_lo;
    ULONG addr_hi;
    ULONG len;
    UWORD flags;
    UWORD next;
} VIRTIO_DESC;

typedef struct
{
    UWORD flags;
    UWORD idx;
    UWORD ring[VIRTIO_QUEUE_SIZE];
    UWORD used_event;
} VIRTIO_AVAIL;

typedef struct
{
    ULONG id;
    ULONG len;
} VIRTIO_USED_ELEM;

typedef struct
{
    UWORD flags;
    UWORD idx;
    VIRTIO_USED_ELEM ring[VIRTIO_QUEUE_SIZE];
    UWORD avail_event;
} VIRTIO_USED;

typedef struct
{
    ULONG base;             /* mmio base address of this device's transport window */
    UWORD last_used_idx;    /* used->idx last consumed by virtio_handle_interrupt() */
    volatile BOOL done;     /* set by virtio_handle_interrupt(), cleared by virtio_submit() */
    VIRTIO_DESC  desc[VIRTIO_QUEUE_SIZE];
    VIRTIO_AVAIL avail;
    VIRTIO_USED  used;
} VIRTIO_DEV;

/* Probes one virtio-mmio slot: checks magic/version/device-id, negotiates
 * VIRTIO_F_VERSION_1 only. On success dev->base/last_used_idx/done are
 * initialized and the device is left in the FEATURES_OK state (no queue
 * configured yet, DRIVER_OK not set). Returns FALSE if the slot is empty,
 * is a different device type, or isn't version-2 virtio-mmio. */
BOOL virtio_probe(ULONG base, UWORD want_device_id, VIRTIO_DEV *dev);

/* Configures queue 0 from dev->desc/avail/used and sets DRIVER_OK.
 * Must be called exactly once after a successful virtio_probe(). */
BOOL virtio_setup_queue(VIRTIO_DEV *dev);

/* Writes one descriptor slot (LE-encoded). addr must already be a
 * physical address (see VIRTIO_PHYS() in virtio.c / VIRTIO_BLK_PHYS() in
 * virtio_blk.c on virt-arm, where the MMU aliases the kernel's low
 * virtual window onto physical RAM at VIRT_RAM_BASE) -- addr_hi is
 * always 0 (neither virt board needs a 64-bit DMA address). */
void virtio_desc_set(VIRTIO_DEV *dev, UWORD index, ULONG addr, ULONG len, UWORD flags, UWORD next);

/* Appends head_index to the avail ring and bumps avail->idx. Clears dev->done. */
void virtio_submit(VIRTIO_DEV *dev, UWORD head_index);

/* Rings the device's doorbell (QueueNotify). */
void virtio_notify(VIRTIO_DEV *dev);

/* Called from the board's IRQ dispatch once it has identified this device
 * as the interrupt source. Acks the device's InterruptStatus and, if the
 * used ring advanced, sets dev->done. */
void virtio_handle_interrupt(VIRTIO_DEV *dev);

#endif /* VIRTIO_H */
```

- [ ] **Step 2: Write `util/virtio.c` (probe/negotiate only for this task)**

```c
/*
 * virtio.c - shared virtio-mmio transport (modern/version-2 only)
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#include "config.h"
#include "portab.h"
#include "endian.h"
#include "virtio.h"

#define VIRTIO_MAGIC  0x74726976UL   /* "virt" */

#define VIRTIO_STATUS_ACKNOWLEDGE  0x01
#define VIRTIO_STATUS_DRIVER       0x02
#define VIRTIO_STATUS_DRIVER_OK    0x04
#define VIRTIO_STATUS_FEATURES_OK  0x08
#define VIRTIO_STATUS_FAILED       0x80

typedef struct
{
    volatile ULONG magic_value;         /* 0x000 */
    volatile ULONG version;             /* 0x004 */
    volatile ULONG device_id;           /* 0x008 */
    volatile ULONG vendor_id;           /* 0x00c */
    volatile ULONG device_features;     /* 0x010 */
    volatile ULONG device_features_sel; /* 0x014 */
    ULONG pad1[2];                      /* 0x018 */
    volatile ULONG driver_features;     /* 0x020 */
    volatile ULONG driver_features_sel; /* 0x024 */
    ULONG pad2[2];                      /* 0x028 */
    volatile ULONG queue_sel;           /* 0x030 */
    volatile ULONG queue_num_max;       /* 0x034 */
    volatile ULONG queue_num;           /* 0x038 */
    ULONG pad3[2];                      /* 0x03c */
    volatile ULONG queue_ready;         /* 0x044 */
    ULONG pad4[2];                      /* 0x048 */
    volatile ULONG queue_notify;        /* 0x050 */
    ULONG pad5[3];                      /* 0x054 */
    volatile ULONG interrupt_status;    /* 0x060 */
    volatile ULONG interrupt_ack;       /* 0x064 */
    ULONG pad6[2];                      /* 0x068 */
    volatile ULONG status;              /* 0x070 */
    ULONG pad7[3];                      /* 0x074 */
    volatile ULONG queue_desc_low;      /* 0x080 */
    volatile ULONG queue_desc_high;     /* 0x084 */
    ULONG pad8[2];                      /* 0x088 */
    volatile ULONG queue_driver_low;    /* 0x090 */
    volatile ULONG queue_driver_high;   /* 0x094 */
    ULONG pad9[2];                      /* 0x098 */
    volatile ULONG queue_device_low;    /* 0x0a0 */
    volatile ULONG queue_device_high;   /* 0x0a4 */
    ULONG pad10[21];                    /* 0x0a8 */
    volatile ULONG config_generation;   /* 0x0fc */
} VIRTIO_MMIO_REGS;

static ULONG vreg_read(volatile ULONG *reg)
{
    return le2cpu32(*reg);
}

static void vreg_write(volatile ULONG *reg, ULONG val)
{
    *reg = cpu2le32(val);
}

BOOL virtio_probe(ULONG base, UWORD want_device_id, VIRTIO_DEV *dev)
{
    volatile VIRTIO_MMIO_REGS *regs = (volatile VIRTIO_MMIO_REGS *)base;
    ULONG feat_hi;

    if (vreg_read(&regs->magic_value) != VIRTIO_MAGIC)
        return FALSE;
    if (vreg_read(&regs->version) != 2)
        return FALSE;
    if (vreg_read(&regs->device_id) != (ULONG)want_device_id)
        return FALSE;

    vreg_write(&regs->status, 0);
    vreg_write(&regs->status, VIRTIO_STATUS_ACKNOWLEDGE);
    vreg_write(&regs->status, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    vreg_write(&regs->device_features_sel, 1);
    feat_hi = vreg_read(&regs->device_features);
    if (!(feat_hi & 1))   /* VIRTIO_F_VERSION_1 is bit 32 (bit 0 of the sel=1 word) */
    {
        vreg_write(&regs->status, VIRTIO_STATUS_FAILED);
        return FALSE;
    }

    vreg_write(&regs->driver_features_sel, 0);
    vreg_write(&regs->driver_features, 0);
    vreg_write(&regs->driver_features_sel, 1);
    vreg_write(&regs->driver_features, 1);

    vreg_write(&regs->status, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
    if (!(vreg_read(&regs->status) & VIRTIO_STATUS_FEATURES_OK))
    {
        vreg_write(&regs->status, VIRTIO_STATUS_FAILED);
        return FALSE;
    }

    dev->base = base;
    dev->last_used_idx = 0;
    dev->done = FALSE;
    return TRUE;
}

BOOL virtio_setup_queue(VIRTIO_DEV *dev)
{
    return FALSE;   /* implemented in Task 2 */
}

void virtio_desc_set(VIRTIO_DEV *dev, UWORD index, ULONG addr, ULONG len, UWORD flags, UWORD next)
{
}

void virtio_submit(VIRTIO_DEV *dev, UWORD head_index)
{
}

void virtio_notify(VIRTIO_DEV *dev)
{
}

void virtio_handle_interrupt(VIRTIO_DEV *dev)
{
}
```

  Note: the last four functions are stubbed here only so the file compiles and links; Task 2 replaces every one of these bodies with a real implementation. This mirrors how `virt_uart.c` etc. were built up incrementally in the existing git history for this port.

- [ ] **Step 3: Add virtio-mmio constants to `virt_memmap.h`**

Modify `bios/machine/virt-arm/virt_memmap.h`, adding before the closing `#endif /* MACHINE_VIRT_ARM */`. `bios/virtio_blk.c` (Step 4 below) needs these to build for `MACHINE_VIRT_ARM` even before any queue/interrupt code exists, since it uses them to compute each probed slot's address:

```c
/* virtio-mmio: 32 transports, 0x200 bytes apart, starting at GIC SPI 16
 * (see hw/arm/virt.c: base_memmap[VIRT_MMIO], irqmap[VIRT_MMIO],
 * NUM_VIRTIO_TRANSPORTS). GIC INTIDs are SPI number + 32, so transport i's
 * IRQ is GIC INTID 48+i -- see virt_connect_irq() in virt_pic.c (extended
 * for this in Task 2). */
#define VIRT_VIRTIO_MMIO_BASE    0x0a000000UL
#define VIRT_VIRTIO_MMIO_STRIDE  0x200UL
#define VIRT_VIRTIO_MMIO_COUNT   32
#define VIRT_VIRTIO_IRQ_BASE     48
```

- [ ] **Step 4: Write `bios/virtio_blk.h`**

```c
/*
 * virtio_blk.h - virtio-blk driver for the QEMU virt-arm/virt-m68k boards
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H

#include "portab.h"

#if CONF_WITH_VIRTIO_BLK

void virtio_blk_init(void);
LONG virtio_blk_ioctl(UWORD drv, UWORD ctrl, void *arg);
LONG virtio_blk_rw(WORD rw, LONG sector, WORD count, UBYTE *buf, WORD dev);

#endif /* CONF_WITH_VIRTIO_BLK */

#endif /* VIRTIO_BLK_H */
```

- [ ] **Step 5: Write `bios/virtio_blk.c` (discovery-only for this task)**

```c
/*
 * virtio_blk.c - virtio-blk driver for the QEMU virt-arm/virt-m68k boards
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#include "config.h"

#if CONF_WITH_VIRTIO_BLK

#include "portab.h"
#include "disk.h"
#include "gemerror.h"
#include "kprint.h"
#include "virtio.h"
#include "virtio_blk.h"

#if defined(MACHINE_VIRT_ARM)
#include "virt_memmap.h"
#define VIRTIO_MMIO_BASE    VIRT_VIRTIO_MMIO_BASE
#define VIRTIO_MMIO_STRIDE  VIRT_VIRTIO_MMIO_STRIDE
#define VIRTIO_MMIO_COUNT   VIRT_VIRTIO_MMIO_COUNT
#elif defined(MACHINE_VIRT_M68K)
#define VIRTIO_MMIO_BASE    0xff010000UL
#define VIRTIO_MMIO_STRIDE  0x200UL
#define VIRTIO_MMIO_COUNT   128
#endif

#define VIRTIO_BLK_DEVICE_ID  2

static VIRTIO_DEV virtio_blk_dev[DEVICES_PER_BUS];
static WORD virtio_blk_count;

void virtio_blk_init(void)
{
    WORD slot;
    ULONG base;

    virtio_blk_count = 0;

    for (slot = 0; slot < VIRTIO_MMIO_COUNT && virtio_blk_count < DEVICES_PER_BUS; slot++)
    {
        base = VIRTIO_MMIO_BASE + (ULONG)slot * VIRTIO_MMIO_STRIDE;

        if (!virtio_probe(base, VIRTIO_BLK_DEVICE_ID, &virtio_blk_dev[virtio_blk_count]))
            continue;

        KDEBUG(("virtio_blk_init: found device at slot %d (base 0x%08lx)\n", slot, base));
        virtio_blk_count++;
    }

    KDEBUG(("virtio_blk_init: %d device(s) found\n", virtio_blk_count));
}

LONG virtio_blk_ioctl(UWORD drv, UWORD ctrl, void *arg)
{
    return EUNDEV;   /* implemented in Task 2 */
}

LONG virtio_blk_rw(WORD rw, LONG sector, WORD count, UBYTE *buf, WORD dev)
{
    return EUNDEV;   /* implemented in Task 2 */
}

#endif /* CONF_WITH_VIRTIO_BLK */
```

- [ ] **Step 6: Wire `util/build.mk`**

Modify `util/build.mk`, adding after the existing `obj-y +=` block:

```make
obj-$(CONF_WITH_VIRTIO) += virtio.o
```

- [ ] **Step 7: Wire `bios/build.mk`**

Modify `bios/build.mk`, adding a new line after the `MACHINE_VIRT_M68K` line (currently line 42):

```make
obj-$(CONF_WITH_VIRTIO_BLK) += virtio_blk.o
```

- [ ] **Step 8: Add Kconfig options**

Modify `bios/Kconfig`, inserting after the `CONF_WITH_RASPI_EMMC` block (after line 149):

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

- [ ] **Step 9: Add the `VIRTIO_BUS` bus type to `bios/disk.h`**

Modify `bios/disk.h`:

```c
#define ACSI_BUS            0
#define SCSI_BUS             1
#define IDE_BUS              2
#define SDMMC_BUS            3
#define VIRTIO_BUS           4

#define MAX_BUS              VIRTIO_BUS
#define DEVICES_PER_BUS      8

#define UNITSNUM            (NUMFLOPPIES+(DEVICES_PER_BUS*(MAX_BUS+1)))

#define GET_BUS(major)          ((major)/DEVICES_PER_BUS)
#define IS_ACSI_DEVICE(major)   (GET_BUS(major) == ACSI_BUS)
#define IS_SCSI_DEVICE(major)   (GET_BUS(major) == SCSI_BUS)
#define IS_IDE_DEVICE(major)    (GET_BUS(major) == IDE_BUS)
#define IS_SDMMC_DEVICE(major)  (GET_BUS(major) == SDMMC_BUS)
#define IS_VIRTIO_DEVICE(major) (GET_BUS(major) == VIRTIO_BUS)
```

(Only the lines shown change; the rest of `disk.h` is untouched. Match existing column alignment when editing — this codebase aligns the `#define` value column within each block.)

- [ ] **Step 10: Wire `bios/disk.c`**

Add the include near the top, alongside the existing driver includes (after `#include "raspi_emmc.h"` at line 31):

```c
#include "virtio_blk.h"
```

Add virtio's majors (32-39, the next `DEVICES_PER_BUS`-sized block after SD/MMC's 24-31) to the `majors[]` array in `disk_init_all()` (`bios/disk.c:167-171`):

```c
    static const int majors[] =
        {16, 18, 17, 19, 20, 22, 21, 23,    /* IDE primary/secondary */
         8, 9, 10, 11, 12, 13, 14, 15,      /* SCSI */
         0, 1, 2, 3, 4, 5, 6, 7,            /* ACSI */
         24, 25, 26, 27, 28, 29, 30, 31,    /* SD/MMC */
         32, 33, 34, 35, 36, 37, 38, 39};   /* virtio-blk */
```

Add a `VIRTIO_BUS` arm to each of the four dispatch `switch(bus)` blocks, following the exact pattern of the adjacent `SDMMC_BUS`/`CONF_WITH_SDMMC` arm in each:

In `disk_mediach()` (`bios/disk.c`, inside the `switch(bus)` starting at line 259), after the `CONF_WITH_RASPI_EMMC` arm:
```c
#if CONF_WITH_VIRTIO_BLK
    case VIRTIO_BUS:
        ret = virtio_blk_ioctl(reldev,GET_MEDIACHANGE,NULL);
        break;
#endif /* CONF_WITH_VIRTIO_BLK */
```

In `internal_inquire()` (`bios/disk.c`, `switch(bus)` starting at line 738), after the `CONF_WITH_RASPI_EMMC` arm:
```c
#if CONF_WITH_VIRTIO_BLK
    case VIRTIO_BUS:
        ret = virtio_blk_ioctl(reldev,GET_DISKNAME,name);
        break;
#endif /* CONF_WITH_VIRTIO_BLK */
```

In `disk_get_capacity()` (`bios/disk.c`, `switch(bus)` starting at line 819), after the `CONF_WITH_RASPI_EMMC` arm:
```c
#if CONF_WITH_VIRTIO_BLK
    case VIRTIO_BUS:
        ret = virtio_blk_ioctl(reldev,GET_DISKINFO,info);
        KDEBUG(("virtio_blk_ioctl(%d) returned %ld\n", reldev, ret));
        if (ret < 0)
            return ret;
        break;
#endif /* CONF_WITH_VIRTIO_BLK */
```

In `disk_rw()` (`bios/disk.c`, `switch(bus)` starting at line 893), after the `CONF_WITH_RASPI_EMMC` arm:
```c
#if CONF_WITH_VIRTIO_BLK
    case VIRTIO_BUS:
        ret = virtio_blk_rw(rw, sector, count, buf, reldev);
        KDEBUG(("virtio_blk_rw() returned %ld\n", ret));
        break;
#endif /* CONF_WITH_VIRTIO_BLK */
```

- [ ] **Step 11: Verify portability — build every non-virt config**

```bash
make distclean
make firebee-prg_defconfig && make -j"$(nproc)" 2>&1 | tail -20
make distclean
make rpi2_defconfig && make -j"$(nproc)" 2>&1 | tail -20
```

Expected: both builds succeed with no new warnings/errors. This confirms the `disk.c`/`disk.h` changes fully compile out (`CONF_WITH_VIRTIO_BLK` is `0` for both, since neither sets `MACHINE_VIRT_ARM`/`MACHINE_VIRT_M68K`) and don't disturb the shared `majors[]`/`UNITSNUM` sizing other buses rely on. If any other defconfig exists in `configs/` beyond these two and `virt-arm`/`virt-m68k`, build it too.

- [ ] **Step 12: Build and boot-test discovery on ARM**

```bash
make distclean
make virt-arm_defconfig && make -j"$(nproc)"
qemu-system-arm -M virt -cpu cortex-a7 -kernel <image-from-the-"is-ready"-line> \
  -global virtio-mmio.force-legacy=false \
  -serial stdio -d guest_errors \
  -drive file=/tmp/virtio-test.img,if=none,format=raw,id=hd0 \
  -device virtio-blk-device,drive=hd0
```

(Create `/tmp/virtio-test.img` first: `truncate -s 16M /tmp/virtio-test.img`. The `-global virtio-mmio.force-legacy=false` flag is required: this QEMU's `virt` board defaults virtio-mmio transports to legacy/version-1, which `virtio_probe()` correctly rejects since it's modern/version-2 only — without the flag you'll see `0 device(s) found` and that is *not* a driver bug.)

Expected KDEBUG output includes `virtio_blk_init: found device at slot N` (N depends on QEMU's internal device-plugging order — with a single `-device virtio-blk-device` it is not necessarily slot 0) and `virtio_blk_init: 1 device(s) found`. This confirms `virtio_probe()`'s magic/version/device-id/feature-negotiation sequence is correct against real QEMU device emulation, independent of the queue/interrupt code Task 2 adds. `ENABLE_KDEBUG` must be enabled in `bios/virtio_blk.c` (and `disk.c`) to see this output — it's off by default like every other driver in this tree.

- [ ] **Step 13: `make gitready` and commit**

```bash
make gitready
git add util/virtio.h util/virtio.c bios/virtio_blk.h bios/virtio_blk.c \
        bios/machine/virt-arm/virt_memmap.h \
        util/build.mk bios/build.mk bios/Kconfig bios/disk.h bios/disk.c \
        Makefile bios/blkdev.c
git commit -m "virtio: add transport probe/negotiate and virtio-blk discovery"
git push
```

---

## Task 2: ARM — GIC SPI support + full virtqueue I/O + virtio-blk read/write

**Files:**
- Modify: `bios/machine/virt-arm/virt_pic.h`
- Modify: `bios/machine/virt-arm/virt_pic.c`
- Modify: `bios/machine/virt-arm/virt_mmu.h`, `bios/machine/virt-arm/virt_mmu.c` — add `ULONG virt_to_phys(void *va)` (`return (ULONG)va + VIRT_RAM_BASE;`). Required and load-bearing: `virt-arm` runs with the MMU on and `emutos.ld` links the whole kernel — `.text`/`.data`/`.bss`, including every static/stack address `util/virtio.c` and `bios/virtio_blk.c` hand to the device — into the low virtual window `[0, ram_size)`, which `virt_mmu_bootstrap()` maps onto physical `[VIRT_RAM_BASE, VIRT_RAM_BASE+ram_size)`. virtio-mmio devices sit outside this MMU and only understand physical addresses; a bare `(ULONG)` cast of a C pointer (as naively used below) points the device at whatever physical memory the untranslated low address collides with (in testing: `virt.flash0`, silently dropped writes, no interrupt ever raised). Every address `virtio_setup_queue()`, `virtio_submit()`'s callers, or a descriptor's `addr_lo` field hands to the device on this board must go through `virt_to_phys()` first.
- Modify: `util/virtio.c` (replace the Task-1 stub bodies)
- Modify: `bios/virtio_blk.c` (replace `virtio_blk_ioctl`/`virtio_blk_rw`, extend `virtio_blk_init`)
- Modify: `include/endian.h` — add `cpu2le64`/`le2cpu64`, mirroring the existing `bswap32`/`le2cpu32`/`cpu2le32` pattern exactly:
  ```c
  #define bswap64		__builtin_bswap64
  ```
  placed next to the existing `#define bswap32 __builtin_bswap32` line, then in both the `BIG_ENDIAN` and `LITTLE_ENDIAN` branches add (next to the existing `le2cpu32`/`cpu2le32` lines):
  ```c
  /* BIG_ENDIAN branch: */
  #   define le2cpu64(x) (bswap64(x))
  #   define cpu2le64(x) (bswap64(x))
  /* LITTLE_ENDIAN branch: */
  #   define le2cpu64(x) (x)
  #   define cpu2le64(x) (x)
  ```
  Required: the virtio-blk request header's `sector` field is a 64-bit LE-by-spec field (see `struct virtio_blk_req` below) — without this, `hdr->sector`'s stored bit pattern is only correct on a little-endian CPU (silently, since `virt-arm` is LE-native) and would be byte-order-wrong on `virt-m68k` (big-endian) once Task 3 runs this same shared code. `UQUAD` (`include/portab.h`) is the unsigned 64-bit type to pass through `cpu2le64`.

**Interfaces:**
- Consumes: `VIRTIO_DEV`, `virtio_probe()` from Task 1; `virt_connect_irq(int irq, PFVOID handler)` (existing, being extended here to accept `irq >= 32`); `VIRT_VIRTIO_MMIO_BASE`/`_STRIDE`/`_COUNT`/`_IRQ_BASE` (already added to `virt_memmap.h` in Task 1, since `bios/virtio_blk.c` needed them from the start).
- Produces: `virtio_setup_queue()`, `virtio_desc_set()`, `virtio_submit()`, `virtio_notify()`, `virtio_handle_interrupt()` — real implementations, used unchanged by Task 3 on m68k.

- [ ] **Step 1: Extend `virt_pic.h` for SPI range**

Modify `bios/machine/virt-arm/virt_pic.h`:

```c
#define VIRT_IRQ_LINES  80     /* PPIs (16-31) plus GIC SPI 16-47 (INTID 48-79) for virtio-mmio */
```

- [ ] **Step 2: Extend `virt_pic.c` for SPI enable/target/dispatch**

Modify `bios/machine/virt-arm/virt_pic.c`. Replace the register macro block:

```c
#define GICD_CTLR        (*(volatile ULONG*)(VIRT_GIC_DIST_BASE + 0x000))
#define GICD_ISENABLER(n) (*(volatile ULONG*)(VIRT_GIC_DIST_BASE + 0x100 + 4*((n)/32)))
#define GICD_ICENABLER(n) (*(volatile ULONG*)(VIRT_GIC_DIST_BASE + 0x180 + 4*((n)/32)))
#define GICD_IPRIORITYR(n) (*(volatile UBYTE*)(VIRT_GIC_DIST_BASE + 0x400 + (n)))
#define GICD_ITARGETSR(n) (*(volatile UBYTE*)(VIRT_GIC_DIST_BASE + 0x800 + (n)))
#define GICC_CTLR  (*(volatile ULONG*)(VIRT_GIC_CPU_BASE + 0x000))
#define GICC_PMR   (*(volatile ULONG*)(VIRT_GIC_CPU_BASE + 0x004))
#define GICC_IAR   (*(volatile ULONG*)(VIRT_GIC_CPU_BASE + 0x00C))
#define GICC_EOIR  (*(volatile ULONG*)(VIRT_GIC_CPU_BASE + 0x010))
```

Replace `virt_pic_init()`'s single `GICD_ICENABLER0 = 0xffffffffUL;` line with a loop covering every 32-IRQ word up to `VIRT_IRQ_LINES`:

```c
void virt_pic_init(void)
{
    int i;

    for (i = 0; i < VIRT_IRQ_LINES; i++)
        virt_irq_handlers[i] = 0;

    for (i = 0; i < VIRT_IRQ_LINES; i += 32)
        GICD_ICENABLER(i) = 0xffffffffUL;   /* disable everything to start from a known state */
    GICD_CTLR = 1;                          /* enable distributor */

    GICC_PMR = 0xff;                        /* let every priority through */
    GICC_CTLR = 1;                          /* enable this CPU's interface */
}
```

Replace `virt_connect_irq()`'s enable line, adding the SPI target-CPU write:

```c
PFVOID virt_connect_irq(int irq, PFVOID handler)
{
    PFVOID old;

    if (irq < 0 || irq >= VIRT_IRQ_LINES)
    {
        KDEBUG(("virt_connect_irq: IRQ %d out of range\n", irq));
        return 0;
    }

    old = virt_irq_handlers[irq];

    virt_irq_handlers[irq] = handler;
    GICD_IPRIORITYR(irq) = 0x80;
    if (irq >= 32)
        GICD_ITARGETSR(irq) = 0x01;    /* SPIs need explicit CPU targeting; PPIs are implicitly per-CPU */
    GICD_ISENABLER(irq) = (1UL << (irq % 32));
    return old;
}
```

`virt_int_handler()` is unchanged (its `irq < VIRT_IRQ_LINES` bound now covers the larger array automatically).

- [ ] **Step 3: Implement the real virtqueue functions in `util/virtio.c`**

Replace the four stub bodies from Task 1 (`virtio_setup_queue`, `virtio_desc_set`, `virtio_submit`, `virtio_notify`, `virtio_handle_interrupt`) with:

```c
BOOL virtio_setup_queue(VIRTIO_DEV *dev)
{
    volatile VIRTIO_MMIO_REGS *regs = (volatile VIRTIO_MMIO_REGS *)dev->base;
    ULONG qmax;
    WORD i;

    vreg_write(&regs->queue_sel, 0);
    if (vreg_read(&regs->queue_ready) != 0)
        return FALSE;

    qmax = vreg_read(&regs->queue_num_max);
    if (qmax < VIRTIO_QUEUE_SIZE)
        return FALSE;

    for (i = 0; i < VIRTIO_QUEUE_SIZE; i++)
        virtio_desc_set(dev, i, 0, 0, 0, 0);
    dev->avail.flags = 0;
    dev->avail.idx = 0;
    dev->used.flags = 0;
    dev->used.idx = 0;

    vreg_write(&regs->queue_num, VIRTIO_QUEUE_SIZE);
    vreg_write(&regs->queue_desc_low,   VIRTIO_PHYS(dev->desc));
    vreg_write(&regs->queue_desc_high,  0);
    vreg_write(&regs->queue_driver_low, VIRTIO_PHYS(&dev->avail));
    vreg_write(&regs->queue_driver_high,0);
    vreg_write(&regs->queue_device_low, VIRTIO_PHYS(&dev->used));
    vreg_write(&regs->queue_device_high,0);
    vreg_write(&regs->queue_ready, 1);

    vreg_write(&regs->status, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER
                             | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);
    return TRUE;
}

void virtio_desc_set(VIRTIO_DEV *dev, UWORD index, ULONG addr, ULONG len, UWORD flags, UWORD next)
{
    dev->desc[index].addr_lo = cpu2le32(addr);
    dev->desc[index].addr_hi = cpu2le32(0);
    dev->desc[index].len     = cpu2le32(len);
    dev->desc[index].flags   = cpu2le16(flags);
    dev->desc[index].next    = cpu2le16(next);
}

void virtio_submit(VIRTIO_DEV *dev, UWORD head_index)
{
    UWORD slot = le2cpu16(dev->avail.idx) % VIRTIO_QUEUE_SIZE;

    dev->avail.ring[slot] = cpu2le16(head_index);
    dev->avail.idx = cpu2le16((UWORD)(le2cpu16(dev->avail.idx) + 1));
    dev->done = FALSE;
}

#if ARCH_ARM
extern void flush_data_cache(void *start, long size);
extern void invalidate_data_cache(void *start, long size);
extern ULONG virt_to_phys(void *va);
#define VIRTIO_PHYS(p) virt_to_phys((void *)(p))
#else
#define VIRTIO_PHYS(p) ((ULONG)(p))
#endif

void virtio_notify(VIRTIO_DEV *dev)
{
    volatile VIRTIO_MMIO_REGS *regs = (volatile VIRTIO_MMIO_REGS *)dev->base;

#if ARCH_ARM
    flush_data_cache(dev->desc, sizeof(dev->desc));
    flush_data_cache(&dev->avail, sizeof(dev->avail));
#endif
    vreg_write(&regs->queue_notify, 0);
}

void virtio_handle_interrupt(VIRTIO_DEV *dev)
{
    volatile VIRTIO_MMIO_REGS *regs = (volatile VIRTIO_MMIO_REGS *)dev->base;
    ULONG isr = vreg_read(&regs->interrupt_status);

    vreg_write(&regs->interrupt_ack, isr);

#if ARCH_ARM
    invalidate_data_cache(&dev->used, sizeof(dev->used));
#endif

    if (le2cpu16(dev->used.idx) != dev->last_used_idx)
    {
        dev->last_used_idx = le2cpu16(dev->used.idx);
        dev->done = TRUE;
    }
}
```

(`VIRTIO_STATUS_*` macros already exist from Task 1's `virtio_probe()`.)

- [ ] **Step 4: Implement the real `virtio_blk_init`/`_ioctl`/`_rw` in `bios/virtio_blk.c`**

Replace the entire file body (keep the header comment and `#if CONF_WITH_VIRTIO_BLK` guard) with:

```c
#include "config.h"

#if CONF_WITH_VIRTIO_BLK

#include "portab.h"
#include "disk.h"
#include "gemerror.h"
#include "kprint.h"
#include "string.h"
#include "endian.h"
#include "tosvars.h"
#include "mfp.h"
#include "virtio.h"
#include "virtio_blk.h"

#define VIRTIO_BLK_TIMEOUT_MSEC   1000UL
#define VIRTIO_BLK_TIMEOUT_TICKS  ((VIRTIO_BLK_TIMEOUT_MSEC*CLOCKS_PER_SEC+999)/1000)

#if defined(MACHINE_VIRT_ARM)
#include "virt_memmap.h"
#include "virt_pic.h"
#define VIRTIO_MMIO_BASE    VIRT_VIRTIO_MMIO_BASE
#define VIRTIO_MMIO_STRIDE  VIRT_VIRTIO_MMIO_STRIDE
#define VIRTIO_MMIO_COUNT   VIRT_VIRTIO_MMIO_COUNT
#elif defined(MACHINE_VIRT_M68K)
#define VIRTIO_MMIO_BASE    0xff010000UL
#define VIRTIO_MMIO_STRIDE  0x200UL
#define VIRTIO_MMIO_COUNT   128
#endif

#if defined(MACHINE_VIRT_ARM)
extern ULONG virt_to_phys(void *va);
#define VIRTIO_BLK_PHYS(p) virt_to_phys((void *)(p))
#elif defined(MACHINE_VIRT_M68K)
#define VIRTIO_BLK_PHYS(p) ((ULONG)(p))   /* no MMU aliasing on this port's m68k boards */
#endif

#define VIRTIO_BLK_DEVICE_ID  2

#define VIRTIO_BLK_T_IN   0UL
#define VIRTIO_BLK_T_OUT  1UL

struct virtio_blk_req
{
    ULONG type;
    ULONG reserved;
    QUAD  sector;
};

static VIRTIO_DEV virtio_blk_dev[DEVICES_PER_BUS];
static ULONG virtio_blk_capacity[DEVICES_PER_BUS];   /* in 512-byte sectors */
static struct virtio_blk_req virtio_blk_hdr[DEVICES_PER_BUS];
static UBYTE virtio_blk_status[DEVICES_PER_BUS];
static WORD virtio_blk_count;

static ULONG virtio_blk_read_capacity(ULONG base)
{
    volatile ULONG *config = (volatile ULONG *)(base + 0x100);
    return le2cpu32(config[0]);   /* capacity is a 64-bit LE field; the low word is enough here */
}

static void virtio_blk_isr_common(WORD unit)
{
    virtio_handle_interrupt(&virtio_blk_dev[unit]);
}

#define VIRTIO_BLK_ISR(n) \
static void virtio_blk_isr_##n(void) { virtio_blk_isr_common(n); }

VIRTIO_BLK_ISR(0)
VIRTIO_BLK_ISR(1)
VIRTIO_BLK_ISR(2)
VIRTIO_BLK_ISR(3)
VIRTIO_BLK_ISR(4)
VIRTIO_BLK_ISR(5)
VIRTIO_BLK_ISR(6)
VIRTIO_BLK_ISR(7)

static PFVOID const virtio_blk_isr_table[DEVICES_PER_BUS] =
{
    virtio_blk_isr_0, virtio_blk_isr_1, virtio_blk_isr_2, virtio_blk_isr_3,
    virtio_blk_isr_4, virtio_blk_isr_5, virtio_blk_isr_6, virtio_blk_isr_7
};

static void virtio_blk_connect_irq(WORD slot, WORD unit)
{
#if defined(MACHINE_VIRT_ARM)
    virt_connect_irq(VIRT_VIRTIO_IRQ_BASE + slot, virtio_blk_isr_table[unit]);
#elif defined(MACHINE_VIRT_M68K)
    goldfish_pic_connect_irq((WORD)(1 + slot / 32), (WORD)(slot % 32), virtio_blk_isr_table[unit]);
#endif
}

#ifdef ENABLE_KDEBUG
/* Safe on this driver specifically: both virt boards are QEMU-only, no real
 * hardware exists for either (see the design doc), so there is no risk of
 * clobbering a real disk. Exercises the write path, which disk_init_all()'s
 * boot-time partition scan never does on its own (it only reads). */
static void virtio_blk_selftest(WORD unit)
{
    static UBYTE pattern[SECTOR_SIZE];
    static UBYTE readback[SECTOR_SIZE];
    WORD i;
    LONG ret;

    for (i = 0; i < SECTOR_SIZE; i++)
        pattern[i] = (UBYTE)(i ^ 0xa5);

    ret = virtio_blk_rw(RW_WRITE, 1, 1, pattern, unit);
    KDEBUG(("virtio_blk_selftest: write returned %ld\n", ret));

    ret = virtio_blk_rw(RW_READ, 1, 1, readback, unit);
    KDEBUG(("virtio_blk_selftest: read returned %ld\n", ret));

    if (memcmp(pattern, readback, SECTOR_SIZE) == 0)
        KDEBUG(("virtio_blk_selftest: unit %d PASS\n", unit));
    else
        KDEBUG(("virtio_blk_selftest: unit %d FAIL\n", unit));
}
#endif

void virtio_blk_init(void)
{
    WORD slot;
    ULONG base;

    virtio_blk_count = 0;

    for (slot = 0; slot < VIRTIO_MMIO_COUNT && virtio_blk_count < DEVICES_PER_BUS; slot++)
    {
        base = VIRTIO_MMIO_BASE + (ULONG)slot * VIRTIO_MMIO_STRIDE;

        if (!virtio_probe(base, VIRTIO_BLK_DEVICE_ID, &virtio_blk_dev[virtio_blk_count]))
            continue;

        if (!virtio_setup_queue(&virtio_blk_dev[virtio_blk_count]))
        {
            KDEBUG(("virtio_blk_init: slot %d found but queue setup failed\n", slot));
            continue;
        }

        virtio_blk_capacity[virtio_blk_count] = virtio_blk_read_capacity(base);
        virtio_blk_connect_irq(slot, virtio_blk_count);

        KDEBUG(("virtio_blk_init: unit %d at slot %d (base 0x%08lx, %lu sectors)\n",
                virtio_blk_count, slot, base, virtio_blk_capacity[virtio_blk_count]));

        virtio_blk_count++;   /* before the self-test: virtio_blk_rw()/_ioctl() reject any dev >= virtio_blk_count */

#ifdef ENABLE_KDEBUG
        virtio_blk_selftest(virtio_blk_count - 1);
#endif
    }

    KDEBUG(("virtio_blk_init: %d device(s) found\n", virtio_blk_count));
}

LONG virtio_blk_ioctl(UWORD drv, UWORD ctrl, void *arg)
{
    if (drv >= (UWORD)virtio_blk_count)
        return EUNDEV;

    switch (ctrl)
    {
    case GET_DISKNAME:
        strlcpy((char *)arg, "Virtio-Blk", 40);
        return 0;
    case GET_DISKINFO:
    {
        ULONG *info = (ULONG *)arg;
        info[0] = virtio_blk_capacity[drv];
        info[1] = SECTOR_SIZE;
        return 0;
    }
    case GET_MEDIACHANGE:
        return MEDIANOCHANGE;
    default:
        return EUNDEV;
    }
}

LONG virtio_blk_rw(WORD rw, LONG sector, WORD count, UBYTE *buf, WORD dev)
{
    struct virtio_blk_req *hdr;
    UBYTE *status;
    WORD i;
    LONG ret = 0;

    if (dev >= (WORD)virtio_blk_count)
        return EUNDEV;

    hdr = &virtio_blk_hdr[dev];
    status = &virtio_blk_status[dev];

    for (i = 0; i < (WORD)count; i++)
    {
        hdr->type = cpu2le32((rw & RW_RW) ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN);
        hdr->reserved = cpu2le32(0);
        hdr->sector = (QUAD)cpu2le64((UQUAD)(sector + i));
        *status = 0xff;

        virtio_desc_set(&virtio_blk_dev[dev], 0, VIRTIO_BLK_PHYS(hdr), (ULONG)sizeof(*hdr), VIRTIO_DESC_F_NEXT, 1);
        virtio_desc_set(&virtio_blk_dev[dev], 1, VIRTIO_BLK_PHYS(buf + i * SECTOR_SIZE), SECTOR_SIZE,
                         (UWORD)(VIRTIO_DESC_F_NEXT | ((rw & RW_RW) ? 0 : VIRTIO_DESC_F_WRITE)), 2);
        virtio_desc_set(&virtio_blk_dev[dev], 2, VIRTIO_BLK_PHYS(status), 1, VIRTIO_DESC_F_WRITE, 0);

#if ARCH_ARM
        {
            extern void flush_data_cache(void *start, long size);
            extern void invalidate_data_cache(void *start, long size);

            flush_data_cache(hdr, sizeof(*hdr));
            if (rw & RW_RW)
                flush_data_cache(buf + i * SECTOR_SIZE, SECTOR_SIZE);
            invalidate_data_cache(status, 1);
        }
#endif

        virtio_submit(&virtio_blk_dev[dev], 0);
        virtio_notify(&virtio_blk_dev[dev]);

        {
            /* Every other block-I/O driver in this tree bounds its hardware
             * wait with a timeout (see e.g. bios/sd.c's SD_READ_TIMEOUT_TICKS
             * idiom) rather than looping forever; a misrouted interrupt or an
             * unresponsive device must not hang the BIOS permanently. */
            LONG timeout = hz_200 + VIRTIO_BLK_TIMEOUT_TICKS;

            while (!virtio_blk_dev[dev].done)
            {
                if (hz_200 >= timeout)
                {
                    ret = (rw & RW_RW) ? EWRITF : EREADF;
                    KDEBUG(("virtio_blk_rw: unit %d timed out\n", dev));
                    return ret;
                }
#if ARCH_ARM
                __asm__ volatile("wfi");
#endif
            }
        }

#if ARCH_ARM
        {
            extern void invalidate_data_cache(void *start, long size);

            invalidate_data_cache(status, 1);
            if (!(rw & RW_RW))
                invalidate_data_cache(buf + i * SECTOR_SIZE, SECTOR_SIZE);
        }
#endif

        if (*status != 0)
        {
            ret = (rw & RW_RW) ? EWRITF : EREADF;
            break;
        }
    }

    KDEBUG(("virtio_blk_rw(%d,%ld,%d,%p,%d) rc=%ld\n", rw, sector, count, buf, dev, ret));
    return ret;
}

#endif /* CONF_WITH_VIRTIO_BLK */
```

  Note: `goldfish_pic_connect_irq` is referenced in `virtio_blk_connect_irq()` for the `MACHINE_VIRT_M68K` branch but not yet defined until Task 3 — this is fine, since that branch is `#ifdef`-dead code on the ARM build this task targets. It will start compiling for real once Task 3 provides the header/function.

- [ ] **Step 5: Build and boot-test full read/write on ARM**

```bash
make distclean
make virt-arm_defconfig
# ensure ENABLE_KDEBUG is on so the self-test in Step 5 runs -- confirm with:
grep ENABLE_KDEBUG obj/autoconf.h
make -j"$(nproc)"
truncate -s 16M /tmp/virtio-test.img
qemu-system-arm -M virt -cpu cortex-a7 -kernel <image> \
  -global virtio-mmio.force-legacy=false \
  -serial stdio -d guest_errors \
  -drive file=/tmp/virtio-test.img,if=none,format=raw,id=hd0 \
  -device virtio-blk-device,drive=hd0
```

(`-global virtio-mmio.force-legacy=false` is required — see Task 1 Step 12's note; this QEMU defaults `virt`'s virtio-mmio transports to legacy/version-1, which the transport correctly rejects.)

Expected KDEBUG output: `virtio_blk_init: unit 0 at slot N ...` (N is whatever slot QEMU assigns — not necessarily 0), `virtio_blk_selftest: write returned 0`, `virtio_blk_selftest: read returned 0`, `virtio_blk_selftest: unit 0 PASS`. A `FAIL` here means either the GIC SPI wiring (Step 3) or the queue/cache-maintenance code (Steps 4-5) is wrong — check first whether the interrupt fires at all (add a temporary `KDEBUG(("virt_int_handler: irq %lu\n", irq))` in `virt_int_handler` if the wait loop hangs) before re-checking descriptor/cache logic.

- [ ] **Step 6: `make gitready` and commit**

```bash
make gitready
git add bios/machine/virt-arm/virt_pic.h bios/machine/virt-arm/virt_pic.c \
        util/virtio.c bios/virtio_blk.c
git commit -m "virtio: ARM GIC SPI support and full virtio-blk read/write"
git push
```

---

## Task 3: m68k — Goldfish PIC shared-line dispatch + virtio-blk wiring

**Files:**
- Modify: `bios/machine/virt-m68k/goldfish_pic.h`
- Modify: `bios/machine/virt-m68k/goldfish_pic.c`
- Create: `bios/machine/virt-m68k/goldfish_pic_isr.S`
- Modify: `bios/build.mk`

**Interfaces:**
- Consumes: `VIRTIO_DEV`, `virtio_probe/_setup_queue/_desc_set/_submit/_notify/_handle_interrupt` from Tasks 1-2 (architecture-neutral, unchanged). `bios/virtio_blk.c`'s `#elif defined(MACHINE_VIRT_M68K)` branch (already written in Task 2, calling `goldfish_pic_connect_irq`) starts compiling for real once this task's header exists.
- Produces: `PFVOID goldfish_pic_connect_irq(WORD pic_index, WORD bit, PFVOID handler);` — mirrors `virt_connect_irq`'s shape.

- [ ] **Step 1: Extend `goldfish_pic.h`**

Modify `bios/machine/virt-m68k/goldfish_pic.h`:

```c
#ifndef GOLDFISH_PIC_H
#define GOLDFISH_PIC_H

#ifdef MACHINE_VIRT_M68K

void goldfish_pic_init(void);
void goldfish_pic_enable(WORD pic, WORD bit);

/* Registers handler for (pic_index, bit) and lazily installs that PIC
 * instance's autovector ISR stub on first use. pic_index must be 1-4
 * (instance 0 carries the TTY, instance 5 the RTC -- both already claimed).
 * Mirrors virt_connect_irq()'s shape on the ARM side. Returns the
 * previously registered handler, or 0. */
PFVOID goldfish_pic_connect_irq(WORD pic_index, WORD bit, PFVOID handler);

/* Called from goldfish_pic_isr1..4 (goldfish_pic_isr.S) once the CPU has
 * taken the matching autovector level. Reads which bits are pending on
 * that PIC instance and calls their registered handlers. */
void goldfish_pic_dispatch(WORD pic_index);

#endif /* MACHINE_VIRT_M68K */

#endif /* GOLDFISH_PIC_H */
```

- [ ] **Step 2: Extend `goldfish_pic.c`**

Modify `bios/machine/virt-m68k/goldfish_pic.c`, adding the `PENDING` register macro, the handler table, `goldfish_pic_connect_irq()`, and `goldfish_pic_dispatch()`. Also add `#include "vectors.h"` and `#include "kprint.h"` to the top include block.

```c
#include "config.h"
#ifndef MACHINE_VIRT_M68K
#error This file must only be compiled for the QEMU m68k virt target
#endif

#include "portab.h"
#include "vectors.h"
#include "kprint.h"
#include "goldfish_pic.h"

/*
 * 6 goldfish-pic instances at 0xff000000, 0x1000 bytes apart (see
 * hw/m68k/virt.c). Each one is wired by QEMU's m68k IRQ controller
 * (hw/intc/m68k_irqc.c) to one CPU autovector level: PIC index n (0-5)
 * drives CPU level n+1, so its interrupts are taken at vector n+25.
 */
#define GOLDFISH_PIC_BASE(n)    (0xff000000UL + (ULONG)(n) * 0x1000UL)

#define PIC_STATUS(n)           (*(volatile ULONG*)(GOLDFISH_PIC_BASE(n) + 0x00))
#define PIC_PENDING(n)          (*(volatile ULONG*)(GOLDFISH_PIC_BASE(n) + 0x04))
#define PIC_IRQ_DISABLE_ALL(n)  (*(volatile ULONG*)(GOLDFISH_PIC_BASE(n) + 0x08))
#define PIC_ENABLE(n)           (*(volatile ULONG*)(GOLDFISH_PIC_BASE(n) + 0x10))

#define GOLDFISH_PIC_COUNT  6

extern void goldfish_pic_isr1(void);   /* goldfish_pic_isr.S, PIC index 1 -> autovector level 2 */
extern void goldfish_pic_isr2(void);   /* PIC index 2 -> autovector level 3 */
extern void goldfish_pic_isr3(void);   /* PIC index 3 -> autovector level 4 */
extern void goldfish_pic_isr4(void);   /* PIC index 4 -> autovector level 5 */

static PFVOID pic_handlers[GOLDFISH_PIC_COUNT][32];
static BOOL pic_vector_installed[GOLDFISH_PIC_COUNT];

void goldfish_pic_init(void)
{
    WORD n;

    for (n = 0; n < GOLDFISH_PIC_COUNT; n++)
        PIC_IRQ_DISABLE_ALL(n) = 1;   /* value is ignored; any write disables all 32 lines */
}

void goldfish_pic_enable(WORD pic, WORD bit)
{
    PIC_ENABLE(pic) = (1UL << bit);
}

static void install_pic_vector(WORD pic_index)
{
    switch (pic_index)
    {
    case 1: VEC_LEVEL2 = goldfish_pic_isr1; break;
    case 2: VEC_LEVEL3 = goldfish_pic_isr2; break;
    case 3: VEC_LEVEL4 = goldfish_pic_isr3; break;
    case 4: VEC_LEVEL5 = goldfish_pic_isr4; break;
    }
}

PFVOID goldfish_pic_connect_irq(WORD pic_index, WORD bit, PFVOID handler)
{
    PFVOID old;

    if (pic_index < 1 || pic_index > 4 || bit < 0 || bit >= 32)
    {
        KDEBUG(("goldfish_pic_connect_irq: (%d,%d) out of range\n", pic_index, bit));
        return 0;
    }

    old = pic_handlers[pic_index][bit];
    pic_handlers[pic_index][bit] = handler;

    if (!pic_vector_installed[pic_index])
    {
        install_pic_vector(pic_index);
        pic_vector_installed[pic_index] = TRUE;
    }

    goldfish_pic_enable(pic_index, bit);
    return old;
}

void goldfish_pic_dispatch(WORD pic_index)
{
    ULONG pending = PIC_PENDING(pic_index);
    WORD bit;

    for (bit = 0; bit < 32; bit++)
    {
        if ((pending & (1UL << bit)) && pic_handlers[pic_index][bit])
            ((void (*)(void))pic_handlers[pic_index][bit])();
    }
}
```

  (`PIC_STATUS` is defined for completeness/documentation of the register layout but not used by this driver — `PIC_PENDING` is what carries the per-bit mask `goldfish_pic_dispatch()` needs. The PIC's `pending` bits are level-sensitive at the hardware level: they track the device line directly and clear themselves once the device's own interrupt-acknowledge lowers its line, exactly as `goldfish_rtc.c`'s existing `RTC_CLEAR_INTERRUPT` write already relies on for the RTC. Nothing here needs to write `PIC_ENABLE`/`PIC_IRQ_DISABLE_ALL` to "ack" — those registers mask lines, they don't acknowledge them.)

- [ ] **Step 3: Write `bios/machine/virt-m68k/goldfish_pic_isr.S`**

```asm
/*
 * goldfish_pic_isr.S - CPU exception-vector entry points for Goldfish PIC
 * instances 1-4 (virtio-mmio), on QEMU's m68k 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * Unlike goldfish_rtc_isr.S, these don't need to tail-jump into
 * int_timerc: virtio interrupts have nothing to do with EmuTOS's system
 * timer tick, so a plain register-save / dispatch / restore / rte is
 * enough.
 */

#include "asmdefs.h"

        .globl  _goldfish_pic_isr1
        .globl  _goldfish_pic_isr2
        .globl  _goldfish_pic_isr3
        .globl  _goldfish_pic_isr4
        .extern _goldfish_pic_dispatch

        .text

_goldfish_pic_isr1:
        movem.l d0-d1/a0-a1,-(sp)
        move.w  #1,-(sp)
        jsr     _goldfish_pic_dispatch
        addq.l  #2,sp
        movem.l (sp)+,d0-d1/a0-a1
        rte

_goldfish_pic_isr2:
        movem.l d0-d1/a0-a1,-(sp)
        move.w  #2,-(sp)
        jsr     _goldfish_pic_dispatch
        addq.l  #2,sp
        movem.l (sp)+,d0-d1/a0-a1
        rte

_goldfish_pic_isr3:
        movem.l d0-d1/a0-a1,-(sp)
        move.w  #3,-(sp)
        jsr     _goldfish_pic_dispatch
        addq.l  #2,sp
        movem.l (sp)+,d0-d1/a0-a1
        rte

_goldfish_pic_isr4:
        movem.l d0-d1/a0-a1,-(sp)
        move.w  #4,-(sp)
        jsr     _goldfish_pic_dispatch
        addq.l  #2,sp
        movem.l (sp)+,d0-d1/a0-a1
        rte
```

- [ ] **Step 4: Wire the new object into `bios/build.mk`**

Modify `bios/build.mk`, changing the existing `MACHINE_VIRT_M68K` line:

```make
obj-$(MACHINE_VIRT_M68K) += goldfish_tty.o goldfish_pic.o goldfish_rtc.o goldfish_rtc_isr.o goldfish_pic_isr.o
```

- [ ] **Step 5: Build and boot-test full read/write on m68k**

```bash
make distclean
make virt-m68k_defconfig
grep ENABLE_KDEBUG obj/autoconf.h
make -j"$(nproc)"
truncate -s 16M /tmp/virtio-test-m68k.img
qemu-system-m68k -M virt -kernel <image> \
  -global virtio-mmio.force-legacy=false \
  -serial stdio -d guest_errors \
  -drive file=/tmp/virtio-test-m68k.img,if=none,format=raw,id=hd0 \
  -device virtio-blk-device,drive=hd0
```

(Same `-global virtio-mmio.force-legacy=false` requirement as Task 1/2's ARM tests — verify it's still needed on this QEMU's m68k `virt` board before assuming it isn't; if `0 device(s) found` appears without it, add it.)

Expected KDEBUG output, same shape as Task 2's ARM test: `virtio_blk_init: unit 0 at slot N ...` (N not necessarily 0), `virtio_blk_selftest: unit 0 PASS`. If the wait loop in `virtio_blk_rw()` hangs, first confirm the autovector actually landed (temporary `KDEBUG` at the top of `goldfish_pic_dispatch()`) before re-checking descriptor/PIC-index math — slot 0 must map to `pic_index=1, bit=0` per `virtio_blk_connect_irq()` in Task 2's `bios/virtio_blk.c`.

- [ ] **Step 6: `make gitready` and commit**

```bash
make gitready
git add bios/machine/virt-m68k/goldfish_pic.h bios/machine/virt-m68k/goldfish_pic.c \
        bios/machine/virt-m68k/goldfish_pic_isr.S bios/build.mk
git commit -m "virtio: m68k Goldfish PIC shared-line dispatch and virtio-blk wiring"
git push
```

---

## Task 4: Docs, defconfig verification, full build matrix

**Files:**
- Modify: `readme.md`
- Verify (no changes expected): `configs/virt-arm_defconfig`, `configs/virt-m68k_defconfig`

**Interfaces:** None — this task only documents and verifies work from Tasks 1-3.

- [ ] **Step 1: Add virtio-blk QEMU invocations to `readme.md`**

Find the existing `qemu-system-arm -M virt ...` and `qemu-system-m68k -M virt ...` invocations in `readme.md` (added when `virt-arm`/`virt-m68k` boot support landed) and add a `-drive`/`-device virtio-blk-device` variant directly below each, e.g.:

```markdown
To also attach a virtio-blk disk:

    qemu-system-arm -M virt -cpu cortex-a7 -kernel kernel.elf -serial stdio \
      -global virtio-mmio.force-legacy=false \
      -drive file=disk.img,if=none,format=raw,id=hd0 -device virtio-blk-device,drive=hd0
```

(and the m68k equivalent, dropping `-cpu cortex-a7`). The `-global virtio-mmio.force-legacy=false` flag is required on QEMU versions that default `virt`'s virtio-mmio transports to legacy/version-1 (confirmed necessary on QEMU 10.1 during Task 1/2/3's testing) — this driver only speaks modern/version-2 virtio-mmio. Match the existing invocation's exact flags/formatting rather than the ones written here, since this step is describing an addition, not a replacement.

- [ ] **Step 2: Regenerate and diff the defconfigs**

```bash
make virt-arm_defconfig
make menuconfig   # confirm CONF_WITH_VIRTIO / CONF_WITH_VIRTIO_BLK show 'y' without touching them
make savedefconfig
diff defconfig configs/virt-arm_defconfig
```

Expected: no diff (both options default to `y` under `MACHINE_VIRT_ARM`). Repeat for `virt-m68k_defconfig`. If either diff is non-empty, copy `defconfig` over the tracked file, since that means the new options' Kconfig defaults didn't come out the way Task 1 intended.

- [ ] **Step 3: Full build matrix**

```bash
for cfg in configs/*_defconfig; do
  name=$(basename "$cfg" _defconfig)
  echo "=== $name ==="
  make distclean >/dev/null
  make "${name}_defconfig" >/dev/null
  make -j"$(nproc)" 2>&1 | tail -5
done
```

Expected: every configuration in `configs/` builds cleanly. This is the final portability check CLAUDE.md requires for any change touching shared code (`disk.c`/`disk.h`/`Kconfig`/`build.mk` here).

- [ ] **Step 4: `make gitready` and commit**

```bash
make gitready
git add readme.md configs/virt-arm_defconfig configs/virt-m68k_defconfig
git commit -m "virtio: document QEMU virtio-blk invocation, verify defconfigs"
git push
```

- [ ] **Step 5: Mark the PR ready for review**

```bash
gh pr ready
```
