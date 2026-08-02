# Shared virtio-input keyboard/mouse driver Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a virtio-input consumer on top of the existing virtio-mmio transport (`util/virtio.c`) so both QEMU `virt` boards (ARM and m68k) get a working keyboard and mouse/tablet, feeding the same `kbd_int()`/`call_mousevec()` BIOS entry points the real IKBD ACIA path and the raspi USB HID mouse driver already use.

**Architecture:** `bios/virtio_input.c` discovers virtio-input devices (device ID 18, shared by keyboard/mouse/tablet) by probing every virtio-mmio slot and reading config-space capability bits to tell the roles apart. Each registered device gets its own eventq (queue 0) pre-loaded with 8 device-writable receive buffers; a per-role interrupt handler drains completed buffers via a new transport primitive, `virtio_pop_used()`, translates each `struct virtio_input_event`, and dispatches it — keyboard events through `kbd_int()`, pointer events (both `EV_REL` mice and `EV_ABS` tablets) through `call_mousevec()`/`mousexvec()` — then immediately resubmits the buffer. `bios/virtio_input_keytbl.c` holds the evdev-KEY_*-to-Atari-scancode table.

**Tech Stack:** C90/gnu90 freestanding (no libc), m68k and ARM GNU cross assemblers. No host-side test framework exists for this codebase — verification is build success plus booting the actual image under QEMU and reading `KDEBUG` serial output and, for the pointer, an actual human moving the mouse over the QEMU display window (there is no way to inject input events from inside the guest, so unlike `virtio_blk`'s self-contained read/write self-test, the pointer path cannot be verified by an agent alone — see Task 3's testing note).

## Global Constraints

- Design doc: `docs/superpowers/specs/2026-08-02-virtio-input-driver-design.md`. Follow it; this plan only fills in the exact code.
- C90 with GNU extensions (`-std=gnu90`): all declarations at the top of a block. Match the surrounding file's comment style (`/* */`).
- 4-space indentation, no tabs, in `.c`/`.h`. Run `make gitready` before every commit.
- Use `portab.h` types (`WORD`, `LONG`, `UBYTE`, `UWORD`, `ULONG`, `BOOL`, `PFVOID`) — never bare `int`/`long`.
- Every multi-byte virtio-mmio register, virtqueue field, or `struct virtio_input_event` field is little-endian by spec, regardless of guest endianness. Always go through `include/endian.h` (`le2cpu32`/`cpu2le32`/`le2cpu16`/`cpu2le16`); never read/write a multi-byte field directly.
- Every virtio-input device shares device ID **18** (unlike virtio-blk's ID 2) — `virtio_probe()` alone cannot tell keyboard/mouse/tablet apart; role comes from querying config space (offset `0x100` from the device's mmio base: `select`/`subsel`/`size` bytes at `+0x00`/`+0x01`/`+0x02`, data at `+0x08`).
- `VIRTIO_QUEUE_SIZE` is 8 (from `util/virtio.h`) — the eventq is fully pre-populated with 8 device-writable receive buffers at init and kept full by resubmitting each buffer right after it's drained.
- `KEY_RELEASED` (`0x80`) is `#define`d privately inside `bios/ikbd.c` and not exported via `ikbd.h` — `virtio_input.c` must define its own copy of this bit, it cannot `#include` ikbd.c's.
- No `Initmous()` call and no IKBD hardware command of any kind — bypass the real chip entirely and call `call_mousevec()`/`mousexvec()` directly, exactly as `usb/udd_mouse.c` already does for the raspi USB mouse.
- ARM `virt` boots with D-cache on; every eventq receive buffer needs `invalidate_data_cache()` (from `bios/processor.h`) after the device completes it and before the CPU reads it. m68k `virt` boots with caches off — those calls must stay `#if ARCH_ARM`-only (`virtio_notify()` already flushes the descriptor table/avail ring itself; consumers only need to invalidate their own data buffers after completion, exactly as `virtio_blk_rw()` does for `status`/`sectbuf`).
- Any shared-tree file this plan touches (`bios/bios.c`, `bios/Kconfig`, `bios/build.mk`, `util/virtio.c`, `util/virtio.h`) compiles for **every** machine, not just the two virt boards — new code must be `#if CONF_WITH_VIRTIO_INPUT`-gated so it fully disappears on Atari/Amiga/ColdFire/raspi builds.
- `util/` code must not `#include` any `bios/` header — `virtio_pop_used()` only touches `VIRTIO_DEV`'s own ring fields, no bios dependency needed.

---

## Task 1: Transport extension + build wiring + device discovery

**Files:**
- Modify: `util/virtio.h` — add `pop_idx` field to `VIRTIO_DEV`, declare `virtio_pop_used()`
- Modify: `util/virtio.c` — initialize `pop_idx`, implement `virtio_pop_used()`
- Create: `bios/virtio_input.h`
- Create: `bios/virtio_input.c` (discovery only for this task — no queue setup, no interrupts, no event dispatch)
- Modify: `bios/Kconfig` — add `CONF_WITH_VIRTIO_INPUT`
- Modify: `bios/build.mk` — add `obj-$(CONF_WITH_VIRTIO_INPUT) += virtio_input.o`
- Modify: `bios/bios.c` — call `virtio_input_init()` right after the existing `usb_init()` call

**Interfaces:**
- Produces (from `util/virtio.h`, used by Task 2):
  ```c
  BOOL virtio_pop_used(VIRTIO_DEV *dev, UWORD *out_index, ULONG *out_len);
  ```
- Produces (from `bios/virtio_input.h`, used by `bios/bios.c` and Tasks 2-3): `void virtio_input_init(void);`

- [ ] **Step 1: Add `pop_idx` and `virtio_pop_used()` to `util/virtio.h`**

Modify the `VIRTIO_DEV` struct (currently ending with `last_used_idx`/`done`):

```c
    UWORD last_used_idx;    /* used->idx last consumed by virtio_handle_interrupt() */
    UWORD pop_idx;          /* used->idx last consumed by virtio_pop_used() -- independent
                             * of last_used_idx: that one tracks "did anything complete"
                             * for a single synchronous waiter (virtio_blk's model), this
                             * one tracks "which entries has the caller actually drained"
                             * for a queue that keeps several buffers in flight at once
                             * (virtio-input's eventq). */
    volatile BOOL done;     /* set by virtio_handle_interrupt(), cleared by virtio_submit() */
} __attribute__((aligned(16))) VIRTIO_DEV;
```

Add the declaration after `virtio_handle_interrupt()`'s:

```c
/* Drains one not-yet-consumed used-ring entry: returns TRUE and fills
 * *out_index (the descriptor index the device completed) and *out_len
 * (bytes the device wrote/read), advancing past it; returns FALSE once
 * the caller has caught up with dev->used.idx. Call this after
 * virtio_handle_interrupt() has run (so dev->used is fresh). Unlike
 * dev->done, which a single synchronous waiter clears by re-submitting,
 * this lets a caller that keeps several buffers in flight (like
 * virtio-input's eventq) drain them all in one interrupt. */
BOOL virtio_pop_used(VIRTIO_DEV *dev, UWORD *out_index, ULONG *out_len);
```

- [ ] **Step 2: Implement `virtio_pop_used()` in `util/virtio.c`**

In `virtio_probe()`, next to the existing `dev->last_used_idx = 0;`:

```c
    dev->base = base;
    dev->phys_offset = 0;
    dev->last_used_idx = 0;
    dev->pop_idx = 0;
    dev->done = FALSE;
    return TRUE;
```

Add the new function after `virtio_handle_interrupt()`:

```c
BOOL virtio_pop_used(VIRTIO_DEV *dev, UWORD *out_index, ULONG *out_len)
{
    UWORD slot;

    if (dev->pop_idx == le2cpu16(dev->used.idx))
        return FALSE;

    slot = dev->pop_idx % VIRTIO_QUEUE_SIZE;
    *out_index = (UWORD)le2cpu32(dev->used.ring[slot].id);
    *out_len = le2cpu32(dev->used.ring[slot].len);
    dev->pop_idx++;

    return TRUE;
}
```

- [ ] **Step 3: Add the Kconfig option**

Modify `bios/Kconfig`, inserting right after the `CONF_WITH_VIRTIO_BLK` block (after its closing blank line, before `config CONF_WITH_FDC`):

```
config CONF_WITH_VIRTIO_INPUT
	bool "virtio-input keyboard/mouse driver"
	depends on CONF_WITH_VIRTIO
	default y
	help
	  Keyboard and mouse/tablet driver for virtio-input devices found on
	  the virtio-mmio transport, for the QEMU virt-arm/virt-m68k boards.
	  No effect without a virtio-keyboard-device / virtio-tablet-device /
	  virtio-mouse-device on the QEMU command line.
```

- [ ] **Step 4: Write `bios/virtio_input.h`**

```c
/*
 * virtio_input.h - virtio-input keyboard/mouse driver for the QEMU
 * virt-arm/virt-m68k boards
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef VIRTIO_INPUT_H
#define VIRTIO_INPUT_H

#include "portab.h"

#if CONF_WITH_VIRTIO_INPUT

void virtio_input_init(void);

#endif /* CONF_WITH_VIRTIO_INPUT */

#endif /* VIRTIO_INPUT_H */
```

- [ ] **Step 5: Write `bios/virtio_input.c` (discovery only)**

```c
/*
 * virtio_input.c - virtio-input keyboard/mouse driver for the QEMU
 * virt-arm/virt-m68k boards
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*#define ENABLE_KDEBUG*/

#include "config.h"

#if CONF_WITH_VIRTIO_INPUT

#include "portab.h"
#include "kprint.h"
#include "endian.h"
#include "virtio.h"
#include "virtio_input.h"

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

#define VIRTIO_INPUT_DEVICE_ID  18

/* Config space, at offset 0x100 from the device's mmio base (virtio-input
 * spec 5.8.5): select/subsel pick a query, size says how many bytes of
 * the union at +0x08 the device filled in (0 == "not supported"). */
#define VIRTIO_INPUT_CFG_SELECT  0x100
#define VIRTIO_INPUT_CFG_SUBSEL  0x101
#define VIRTIO_INPUT_CFG_SIZE    0x102
#define VIRTIO_INPUT_CFG_DATA    0x108

#define VIRTIO_INPUT_CFG_EV_BITS   0x11
#define VIRTIO_INPUT_CFG_ABS_INFO  0x12

#define EV_KEY  0x01
#define EV_REL  0x02
#define EV_ABS  0x03

#define ABS_X  0x00
#define ABS_Y  0x01

static VIRTIO_DEV virtio_input_kbd_dev;
static VIRTIO_DEV virtio_input_ptr_dev;
static BOOL virtio_input_kbd_present;
static BOOL virtio_input_ptr_present;
static BOOL virtio_input_ptr_is_abs;      /* TRUE: tablet (EV_ABS), FALSE: mouse (EV_REL) */
static LONG virtio_input_abs_min_x, virtio_input_abs_max_x;
static LONG virtio_input_abs_min_y, virtio_input_abs_max_y;

/* Writes select/subsel, returns the "size" byte -- nonzero means the
 * device supports that (select, subsel) query. */
static UBYTE virtio_input_cfg_query(ULONG base, UBYTE select, UBYTE subsel)
{
    volatile UBYTE *cfg = (volatile UBYTE *)base;

    cfg[VIRTIO_INPUT_CFG_SELECT] = select;
    cfg[VIRTIO_INPUT_CFG_SUBSEL] = subsel;
    return cfg[VIRTIO_INPUT_CFG_SIZE];
}

static void virtio_input_read_absinfo(ULONG base, UBYTE axis, LONG *out_min, LONG *out_max)
{
    volatile ULONG *data = (volatile ULONG *)(base + VIRTIO_INPUT_CFG_DATA);

    virtio_input_cfg_query(base, VIRTIO_INPUT_CFG_ABS_INFO, axis);
    *out_min = (LONG)le2cpu32(data[0]);
    *out_max = (LONG)le2cpu32(data[1]);
}

void virtio_input_init(void)
{
    WORD slot;
    ULONG base;
    VIRTIO_DEV probe_dev;

    virtio_input_kbd_present = FALSE;
    virtio_input_ptr_present = FALSE;

    for (slot = 0; slot < VIRTIO_MMIO_COUNT; slot++)
    {
        base = VIRTIO_MMIO_BASE + (ULONG)slot * VIRTIO_MMIO_STRIDE;

        if (!virtio_probe(base, VIRTIO_INPUT_DEVICE_ID, &probe_dev))
            continue;

        if (virtio_input_cfg_query(base, VIRTIO_INPUT_CFG_EV_BITS, EV_KEY) != 0)
        {
            if (virtio_input_kbd_present)
            {
                KDEBUG(("virtio_input_init: slot %d is another keyboard, ignored\n", slot));
                continue;
            }
            virtio_input_kbd_dev = probe_dev;
            virtio_input_kbd_present = TRUE;
            KDEBUG(("virtio_input_init: keyboard at slot %d (base 0x%08lx)\n", slot, base));
        }
        else if (virtio_input_cfg_query(base, VIRTIO_INPUT_CFG_EV_BITS, EV_ABS) != 0)
        {
            if (virtio_input_ptr_present)
            {
                KDEBUG(("virtio_input_init: slot %d is another pointer, ignored\n", slot));
                continue;
            }
            virtio_input_ptr_dev = probe_dev;
            virtio_input_ptr_is_abs = TRUE;
            virtio_input_read_absinfo(base, ABS_X, &virtio_input_abs_min_x, &virtio_input_abs_max_x);
            virtio_input_read_absinfo(base, ABS_Y, &virtio_input_abs_min_y, &virtio_input_abs_max_y);
            virtio_input_ptr_present = TRUE;
            KDEBUG(("virtio_input_init: tablet at slot %d (base 0x%08lx, x %ld..%ld, y %ld..%ld)\n",
                    slot, base, virtio_input_abs_min_x, virtio_input_abs_max_x,
                    virtio_input_abs_min_y, virtio_input_abs_max_y));
        }
        else if (virtio_input_cfg_query(base, VIRTIO_INPUT_CFG_EV_BITS, EV_REL) != 0)
        {
            if (virtio_input_ptr_present)
            {
                KDEBUG(("virtio_input_init: slot %d is another pointer, ignored\n", slot));
                continue;
            }
            virtio_input_ptr_dev = probe_dev;
            virtio_input_ptr_is_abs = FALSE;
            virtio_input_ptr_present = TRUE;
            KDEBUG(("virtio_input_init: mouse at slot %d (base 0x%08lx)\n", slot, base));
        }
        else
        {
            KDEBUG(("virtio_input_init: slot %d has no recognized role, ignored\n", slot));
        }
    }

    KDEBUG(("virtio_input_init: keyboard %s, pointer %s\n",
            virtio_input_kbd_present ? "present" : "absent",
            virtio_input_ptr_present ? (virtio_input_ptr_is_abs ? "present (tablet)" : "present (mouse)") : "absent"));
}

#endif /* CONF_WITH_VIRTIO_INPUT */
```

- [ ] **Step 6: Wire `bios/build.mk`**

Modify `bios/build.mk`, adding a new line right after `obj-$(CONF_WITH_VIRTIO_BLK) += virtio_blk.o`:

```make
obj-$(CONF_WITH_VIRTIO_INPUT) += virtio_input.o
```

- [ ] **Step 7: Call `virtio_input_init()` from `bios/bios.c`**

Modify `bios/bios.c`. Add the extern declaration right after the existing `CONF_WITH_USB` block (after its `#endif`, around line 112):

```c
#if CONF_WITH_VIRTIO_INPUT
extern void virtio_input_init(void); /* found in virtio_input.h */
#endif
```

Add the call right after the existing `usb_init()` block (matching its exact indentation):

```c
#if CONF_WITH_USB
        KDEBUG(("usb_init()\n"));
        usb_init();
    KDEBUG(("after usb_init()\n"));
#endif
#if CONF_WITH_VIRTIO_INPUT
        KDEBUG(("virtio_input_init()\n"));
        virtio_input_init();
    KDEBUG(("after virtio_input_init()\n"));
#endif
```

- [ ] **Step 8: Verify portability — build a non-virt config**

```bash
make distclean
make rpi2_defconfig && make -j"$(nproc)" 2>&1 | tail -20
```

Expected: builds with no new warnings/errors (`CONF_WITH_VIRTIO_INPUT` is `0` here, since `rpi2` sets neither `MACHINE_VIRT_ARM` nor `MACHINE_VIRT_M68K`, so none of the new code compiles in).

- [ ] **Step 9: Build and boot-test discovery on ARM**

```bash
make distclean
make virt-arm_defconfig
grep ENABLE_KDEBUG obj/autoconf.h   # confirm it's off by default; edit bios/virtio_input.c's
                                    # "/*#define ENABLE_KDEBUG*/" to enable tracing for this test
make -j"$(nproc)"
qemu-system-arm -M virt -cpu cortex-a7 -kernel <image-from-the-"is-ready"-line> \
  -global virtio-mmio.force-legacy=false \
  -serial stdio -d guest_errors \
  -device virtio-keyboard-device -device virtio-tablet-device
```

(`-global virtio-mmio.force-legacy=false` is required — this QEMU's `virt` board defaults virtio-mmio transports to legacy/version-1, which `virtio_probe()` correctly rejects since it's modern/version-2 only.)

Expected KDEBUG output includes `virtio_input_init: keyboard at slot N ...`, `virtio_input_init: tablet at slot M ... x 0..32767, y 0..32767` (exact min/max come from QEMU's own virtio-tablet-device emulation), and `virtio_input_init: keyboard present, pointer present (tablet)`. This confirms the config-space role-detection logic works against real QEMU device emulation, independent of the queue/interrupt code Task 2 adds.

- [ ] **Step 10: Repeat on m68k**

```bash
make distclean
make virt-m68k_defconfig && make -j"$(nproc)"
qemu-system-m68k -M virt -m 128 -cpu m68020 -kernel <image> \
  -serial stdio -d guest_errors \
  -device virtio-keyboard-device -device virtio-tablet-device
```

Expected: same KDEBUG output pattern as Step 9 (slot numbers may differ).

- [ ] **Step 11: `make gitready` and commit**

```bash
make gitready
git add util/virtio.h util/virtio.c bios/virtio_input.h bios/virtio_input.c \
        bios/Kconfig bios/build.mk bios/bios.c
git commit -m "virtio-input: add transport draining primitive and device discovery"
git push
```

---

## Task 2: Eventq lifecycle + keyboard event pipeline

**Files:**
- Create: `bios/virtio_input_keytbl.h`, `bios/virtio_input_keytbl.c`
- Modify: `bios/build.mk` — add `virtio_input_keytbl.o` to the existing line
- Modify: `bios/virtio_input.c` — add eventq setup/draining, IRQ wiring, and `EV_KEY` dispatch; pointer events are drained but not yet translated (Task 3)

**Interfaces:**
- Consumes: `virtio_pop_used()` (Task 1); `virt_connect_irq(int irq, PFVOID handler)` (`bios/machine/virt-arm/virt_pic.h`, existing); `goldfish_pic_connect_irq(WORD pic_index, WORD bit, PFVOID handler)` (`bios/machine/virt-m68k/goldfish_pic.h`, existing); `kbd_int(UBYTE scancode)` (`bios/ikbd.h`, existing).
- Produces (from `bios/virtio_input_keytbl.h`, used by `virtio_input.c` only):
  ```c
  #define VIRTIO_INPUT_KEYTBL_SIZE 256
  extern UBYTE virtio_input_keytbl[VIRTIO_INPUT_KEYTBL_SIZE];
  void virtio_input_keytbl_init(void);
  ```

- [ ] **Step 1: Write `bios/virtio_input_keytbl.h`**

```c
/*
 * virtio_input_keytbl.h - evdev KEY_* to Atari IKBD scancode table
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef VIRTIO_INPUT_KEYTBL_H
#define VIRTIO_INPUT_KEYTBL_H

#include "portab.h"

#define VIRTIO_INPUT_KEYTBL_SIZE 256

/* Indexed by evdev KEY_* code (linux/input-event-codes.h). 0 means "no
 * Atari scancode for this key" -- callers must treat that as a miss, not
 * press the resulting scancode 0 (not a valid IKBD code anyway).
 * Populated once by virtio_input_keytbl_init(); read-only after that. */
extern UBYTE virtio_input_keytbl[VIRTIO_INPUT_KEYTBL_SIZE];

void virtio_input_keytbl_init(void);

#endif /* VIRTIO_INPUT_KEYTBL_H */
```

- [ ] **Step 2: Write `bios/virtio_input_keytbl.c`**

```c
/*
 * virtio_input_keytbl.c - evdev KEY_* to Atari IKBD scancode table
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#include "config.h"

#if CONF_WITH_VIRTIO_INPUT

#include "portab.h"
#include "string.h"
#include "virtio_input_keytbl.h"

UBYTE virtio_input_keytbl[VIRTIO_INPUT_KEYTBL_SIZE];

/* evdev KEY_* codes 1 (KEY_ESC) through 68 (KEY_F10) number the entire
 * main alphanumeric block -- letters, digits, punctuation, Tab, Enter,
 * Backspace, Space, both Shifts, Ctrl, Alt, Caps Lock, F1-F10 -- using
 * the exact same IBM PC XT Scan Code Set 1 numbering the Atari ST's own
 * IKBD scancodes are built on (confirmed against bios/keyb_us.h's
 * keytbl_us_norm[], which is indexed by Atari scancode: e.g. index 30 is
 * 'a', and evdev's KEY_A is also 30). So this range is a straight
 * identity mapping; only keys outside it need an explicit override. */
#define VIRTIO_INPUT_KEYTBL_IDENTITY_MAX 68

void virtio_input_keytbl_init(void)
{
    WORD i;

    memset(virtio_input_keytbl, 0, sizeof(virtio_input_keytbl));

    for (i = 1; i <= VIRTIO_INPUT_KEYTBL_IDENTITY_MAX; i++)
        virtio_input_keytbl[i] = (UBYTE)i;

    /* Navigation cluster: evdev numbers these from the AT "E0-prefixed"
     * extended set, which doesn't line up with the identity block above.
     * Atari's IKBD has its own fixed codes for the same keys (see
     * bios/ikbd.c's private KEY_HOME/KEY_UPARROW/KEY_LTARROW/KEY_RTARROW/
     * KEY_DNARROW/KEY_INSERT/KEY_DELETE #defines -- duplicated here as
     * literals since those macros aren't exported via ikbd.h). */
    virtio_input_keytbl[102] = 0x47;   /* KEY_HOME   -> KEY_HOME    */
    virtio_input_keytbl[103] = 0x48;   /* KEY_UP     -> KEY_UPARROW */
    virtio_input_keytbl[105] = 0x4b;   /* KEY_LEFT   -> KEY_LTARROW */
    virtio_input_keytbl[106] = 0x4d;   /* KEY_RIGHT  -> KEY_RTARROW */
    virtio_input_keytbl[108] = 0x50;   /* KEY_DOWN   -> KEY_DNARROW */
    virtio_input_keytbl[110] = 0x52;   /* KEY_INSERT -> KEY_INSERT  */
    virtio_input_keytbl[111] = 0x53;   /* KEY_DELETE -> KEY_DELETE  */

    /* The ST keyboard has one physical Ctrl and one Alt key; map both
     * evdev left/right variants onto them (the identity loop above
     * already covered KEY_LEFTCTRL==29 and KEY_LEFTALT==56). */
    virtio_input_keytbl[97]  = 0x1d;   /* KEY_RIGHTCTRL -> KEY_CTRL */
    virtio_input_keytbl[100] = 0x38;   /* KEY_RIGHTALT  -> KEY_ALT  */

    /* Out of scope for this driver (see design doc): numeric keypad,
     * F11+, multimedia keys, Meta/Super. Left at 0 (unmapped, i.e.
     * KDEBUG-logged and dropped by virtio_input.c, not "scancode 0"). */
}

#endif /* CONF_WITH_VIRTIO_INPUT */
```

- [ ] **Step 3: Wire the new file into `bios/build.mk`**

Change the line added in Task 1:

```make
obj-$(CONF_WITH_VIRTIO_INPUT) += virtio_input.o virtio_input_keytbl.o
```

- [ ] **Step 4: Extend `bios/virtio_input.c` — includes, arch-specific IRQ setup, eventq buffers**

Add to the includes (after `#include "virtio_input.h"`):

```c
#include "ikbd.h"
#include "virtio_input_keytbl.h"
```

Replace the arch-specific block (from Task 1) to also pull in the PIC headers, since IRQ connect is now used:

```c
#if defined(MACHINE_VIRT_ARM)
#include "virt_memmap.h"
#include "virt_pic.h"
#define VIRTIO_MMIO_BASE    VIRT_VIRTIO_MMIO_BASE
#define VIRTIO_MMIO_STRIDE  VIRT_VIRTIO_MMIO_STRIDE
#define VIRTIO_MMIO_COUNT   VIRT_VIRTIO_MMIO_COUNT
#elif defined(MACHINE_VIRT_M68K)
#include "goldfish_pic.h"
#define VIRTIO_MMIO_BASE    0xff010000UL
#define VIRTIO_MMIO_STRIDE  0x200UL
#define VIRTIO_MMIO_COUNT   128
#endif

#if ARCH_ARM
extern void invalidate_data_cache(void *start, long size);
#endif
```

Add the wire-format event struct and per-device receive buffers, right after the existing `static LONG virtio_input_abs_min_y, virtio_input_abs_max_y;` line:

```c
/* Wire format of one eventq entry (virtio-input spec 5.8.6.2): 8 bytes,
 * every field little-endian regardless of guest endianness. */
struct virtio_input_event
{
    UWORD type;
    UWORD code;
    ULONG value;
};

static struct virtio_input_event virtio_input_kbd_buf[VIRTIO_QUEUE_SIZE];
static struct virtio_input_event virtio_input_ptr_buf[VIRTIO_QUEUE_SIZE];

#define VIRTIO_INPUT_KEY_AUTOREPEAT  2
#define VIRTIO_INPUT_KEY_RELEASED    0x80   /* mirrors ikbd.c's private
                                              * KEY_RELEASED bit, which
                                              * isn't exported via ikbd.h */

typedef enum { VIRTIO_INPUT_ROLE_KEYBOARD, VIRTIO_INPUT_ROLE_POINTER } VIRTIO_INPUT_ROLE;
```

- [ ] **Step 5: Add the keyboard dispatch function**

Add right after the `struct virtio_input_event`/buffer declarations:

```c
static void virtio_input_handle_key(UWORD code, ULONG value)
{
    UBYTE scancode;

    if (value == VIRTIO_INPUT_KEY_AUTOREPEAT)
        return;   /* bios/ikbd.c's kb_timerc_int() already owns repeat timing */

    if (code >= VIRTIO_INPUT_KEYTBL_SIZE || virtio_input_keytbl[code] == 0)
    {
        KDEBUG(("virtio_input: no scancode for evdev KEY code %u\n", code));
        return;
    }

    scancode = virtio_input_keytbl[code];
    if (value == 0)
        scancode |= VIRTIO_INPUT_KEY_RELEASED;

    kbd_int(scancode);
}
```

(Pointer dispatch is added in Task 3; for now the drain loop below just skips those events.)

- [ ] **Step 6: Add eventq setup and the shared drain routine**

```c
static void virtio_input_setup_eventq(VIRTIO_DEV *dev, struct virtio_input_event *buf)
{
    UWORD i;

    for (i = 0; i < VIRTIO_QUEUE_SIZE; i++)
    {
        virtio_desc_set(dev, i, (ULONG)&buf[i] + dev->phys_offset,
                         (ULONG)sizeof(buf[i]), VIRTIO_DESC_F_WRITE, 0);
        virtio_submit(dev, i);
    }
    virtio_notify(dev);
}

static void virtio_input_drain(VIRTIO_DEV *dev, struct virtio_input_event *buf, VIRTIO_INPUT_ROLE role)
{
    UWORD idx;
    ULONG len;
    UWORD type, code;
    ULONG value;

    virtio_handle_interrupt(dev);

    while (virtio_pop_used(dev, &idx, &len))
    {
        (void)len;
#if ARCH_ARM
        invalidate_data_cache(&buf[idx], sizeof(buf[idx]));
#endif
        type  = le2cpu16(buf[idx].type);
        code  = le2cpu16(buf[idx].code);
        value = le2cpu32(buf[idx].value);

        if (role == VIRTIO_INPUT_ROLE_KEYBOARD)
        {
            if (type == EV_KEY)
                virtio_input_handle_key(code, value);
        }
        /* VIRTIO_INPUT_ROLE_POINTER: dispatch added in Task 3 */

        virtio_desc_set(dev, idx, (ULONG)&buf[idx] + dev->phys_offset,
                         (ULONG)sizeof(buf[idx]), VIRTIO_DESC_F_WRITE, 0);
        virtio_submit(dev, idx);
    }

    virtio_notify(dev);
}

static void virtio_input_kbd_isr(void)
{
    virtio_input_drain(&virtio_input_kbd_dev, virtio_input_kbd_buf, VIRTIO_INPUT_ROLE_KEYBOARD);
}

static void virtio_input_ptr_isr(void)
{
    virtio_input_drain(&virtio_input_ptr_dev, virtio_input_ptr_buf, VIRTIO_INPUT_ROLE_POINTER);
}

static void virtio_input_connect_irq(WORD slot, PFVOID handler)
{
#if defined(MACHINE_VIRT_ARM)
    virt_connect_irq(VIRT_VIRTIO_IRQ_BASE + slot, handler);
#elif defined(MACHINE_VIRT_M68K)
    goldfish_pic_connect_irq((WORD)(1 + slot / 32), (WORD)(slot % 32), handler);
#endif
}
```

- [ ] **Step 7: Extend `virtio_input_init()` to set up queues and IRQs**

Replace the whole function body with (this supersedes Task 1's version — the discovery logic is unchanged, each branch now also configures the queue, connects the IRQ, and fills the eventq):

```c
void virtio_input_init(void)
{
    WORD slot;
    ULONG base;
    VIRTIO_DEV probe_dev;

    virtio_input_keytbl_init();

    virtio_input_kbd_present = FALSE;
    virtio_input_ptr_present = FALSE;

    for (slot = 0; slot < VIRTIO_MMIO_COUNT; slot++)
    {
        base = VIRTIO_MMIO_BASE + (ULONG)slot * VIRTIO_MMIO_STRIDE;

        if (!virtio_probe(base, VIRTIO_INPUT_DEVICE_ID, &probe_dev))
            continue;

        if (virtio_input_cfg_query(base, VIRTIO_INPUT_CFG_EV_BITS, EV_KEY) != 0)
        {
            if (virtio_input_kbd_present)
            {
                KDEBUG(("virtio_input_init: slot %d is another keyboard, ignored\n", slot));
                continue;
            }

            virtio_input_kbd_dev = probe_dev;
#if defined(MACHINE_VIRT_ARM)
            virtio_input_kbd_dev.phys_offset = VIRT_RAM_BASE;
#elif defined(MACHINE_VIRT_M68K)
            virtio_input_kbd_dev.phys_offset = 0;
#endif
            if (!virtio_setup_queue(&virtio_input_kbd_dev))
            {
                KDEBUG(("virtio_input_init: slot %d keyboard queue setup failed\n", slot));
                continue;
            }
            virtio_input_connect_irq(slot, virtio_input_kbd_isr);
            virtio_input_setup_eventq(&virtio_input_kbd_dev, virtio_input_kbd_buf);
            virtio_input_kbd_present = TRUE;
            KDEBUG(("virtio_input_init: keyboard at slot %d (base 0x%08lx)\n", slot, base));
        }
        else if (virtio_input_cfg_query(base, VIRTIO_INPUT_CFG_EV_BITS, EV_ABS) != 0)
        {
            if (virtio_input_ptr_present)
            {
                KDEBUG(("virtio_input_init: slot %d is another pointer, ignored\n", slot));
                continue;
            }

            virtio_input_ptr_dev = probe_dev;
            virtio_input_ptr_is_abs = TRUE;
#if defined(MACHINE_VIRT_ARM)
            virtio_input_ptr_dev.phys_offset = VIRT_RAM_BASE;
#elif defined(MACHINE_VIRT_M68K)
            virtio_input_ptr_dev.phys_offset = 0;
#endif
            if (!virtio_setup_queue(&virtio_input_ptr_dev))
            {
                KDEBUG(("virtio_input_init: slot %d tablet queue setup failed\n", slot));
                continue;
            }
            virtio_input_read_absinfo(base, ABS_X, &virtio_input_abs_min_x, &virtio_input_abs_max_x);
            virtio_input_read_absinfo(base, ABS_Y, &virtio_input_abs_min_y, &virtio_input_abs_max_y);
            virtio_input_connect_irq(slot, virtio_input_ptr_isr);
            virtio_input_setup_eventq(&virtio_input_ptr_dev, virtio_input_ptr_buf);
            virtio_input_ptr_present = TRUE;
            KDEBUG(("virtio_input_init: tablet at slot %d (base 0x%08lx, x %ld..%ld, y %ld..%ld)\n",
                    slot, base, virtio_input_abs_min_x, virtio_input_abs_max_x,
                    virtio_input_abs_min_y, virtio_input_abs_max_y));
        }
        else if (virtio_input_cfg_query(base, VIRTIO_INPUT_CFG_EV_BITS, EV_REL) != 0)
        {
            if (virtio_input_ptr_present)
            {
                KDEBUG(("virtio_input_init: slot %d is another pointer, ignored\n", slot));
                continue;
            }

            virtio_input_ptr_dev = probe_dev;
            virtio_input_ptr_is_abs = FALSE;
#if defined(MACHINE_VIRT_ARM)
            virtio_input_ptr_dev.phys_offset = VIRT_RAM_BASE;
#elif defined(MACHINE_VIRT_M68K)
            virtio_input_ptr_dev.phys_offset = 0;
#endif
            if (!virtio_setup_queue(&virtio_input_ptr_dev))
            {
                KDEBUG(("virtio_input_init: slot %d mouse queue setup failed\n", slot));
                continue;
            }
            virtio_input_connect_irq(slot, virtio_input_ptr_isr);
            virtio_input_setup_eventq(&virtio_input_ptr_dev, virtio_input_ptr_buf);
            virtio_input_ptr_present = TRUE;
            KDEBUG(("virtio_input_init: mouse at slot %d (base 0x%08lx)\n", slot, base));
        }
        else
        {
            KDEBUG(("virtio_input_init: slot %d has no recognized role, ignored\n", slot));
        }
    }

    KDEBUG(("virtio_input_init: keyboard %s, pointer %s\n",
            virtio_input_kbd_present ? "present" : "absent",
            virtio_input_ptr_present ? (virtio_input_ptr_is_abs ? "present (tablet)" : "present (mouse)") : "absent"));
}
```

- [ ] **Step 8: Verify portability — build a non-virt config**

```bash
make distclean
make rpi2_defconfig && make -j"$(nproc)" 2>&1 | tail -20
```

Expected: builds cleanly, same as Task 1 Step 8.

- [ ] **Step 9: Build and interactively test typing on ARM**

```bash
make distclean
make virt-arm_defconfig && make -j"$(nproc)"
qemu-system-arm -M virt -cpu cortex-a7 -kernel <image> \
  -global virtio-mmio.force-legacy=false \
  -serial stdio -d guest_errors \
  -device virtio-keyboard-device -device virtio-tablet-device
```

Once booted to the desktop, use the QEMU monitor (`Ctrl-Alt-2`, or `-monitor stdio` if you'd rather keep the serial console on `stdio`) to send a key:

```
(qemu) sendkey a
```

Expected: the letter appears wherever keyboard input is expected in the running desktop (e.g. typed into a text field), and if `ENABLE_KDEBUG` is on, no `no scancode for evdev KEY code` line appears for ordinary letters/digits. This part of the test does not need a human at the display — `sendkey` is scriptable — so it can be driven by an agent.

- [ ] **Step 10: Repeat on m68k**

```bash
make distclean
make virt-m68k_defconfig && make -j"$(nproc)"
qemu-system-m68k -M virt -m 128 -cpu m68020 -kernel <image> \
  -serial stdio -d guest_errors -monitor stdio \
  -device virtio-keyboard-device -device virtio-tablet-device
```

Same `sendkey` check as Step 9.

- [ ] **Step 11: `make gitready` and commit**

```bash
make gitready
git add bios/virtio_input_keytbl.h bios/virtio_input_keytbl.c bios/build.mk bios/virtio_input.c
git commit -m "virtio-input: add eventq draining and keyboard event dispatch"
git push
```

---

## Task 3: Pointer event pipeline (mouse + tablet) + docs

**Files:**
- Modify: `bios/virtio_input.c` — add `EV_REL`/`EV_ABS`/button dispatch, replacing the "dispatch added in Task 3" placeholder in `virtio_input_drain()`
- Modify: `readme.md` — add virtio-input examples to both `virt` boards' QEMU invocations

**Interfaces:**
- Consumes: `call_mousevec(UBYTE *packet)`, `mousexvec` (`bios/ikbd.h`, existing); `linea_vars.V_REZ_HZ`/`V_REZ_VT` (`bios/lineavars.h`, existing).

- [ ] **Step 1: Add pointer includes and constants**

Add to `bios/virtio_input.c`'s includes (after `#include "virtio_input_keytbl.h"`):

```c
#include "lineavars.h"
```

Add near the other evdev constants (`EV_KEY`/`EV_REL`/`EV_ABS`/`ABS_X`/`ABS_Y`):

```c
#define REL_X  0x00
#define REL_Y  0x01

#define BTN_LEFT    0x110
#define BTN_RIGHT   0x111
#define BTN_MIDDLE  0x112

#define VIRTIO_INPUT_MOUSE_LEFT   0x02   /* MOUSE_REL_POS_REPORT button bits -- see
                                           * bios/ikbd.c's private LEFT_BUTTON_DOWN/
                                           * RIGHT_BUTTON_DOWN #defines, confirmed also
                                           * by usb/udd_mouse.c's identical packet[0]
                                           * construction */
#define VIRTIO_INPUT_MOUSE_RIGHT  0x01
```

- [ ] **Step 2: Add pointer state and the packet-sending helper**

Add right after Task 2's `typedef enum { VIRTIO_INPUT_ROLE_KEYBOARD, VIRTIO_INPUT_ROLE_POINTER } VIRTIO_INPUT_ROLE;` line:

```c
static UBYTE virtio_input_ptr_buttons;
static BOOL virtio_input_x_valid, virtio_input_y_valid;   /* have we seen a baseline EV_ABS sample yet? */
static LONG virtio_input_last_scaled_x, virtio_input_last_scaled_y;

/* Sends one or more 3-byte IKBD relative-mouse packets covering (dx, dy),
 * chunking into signed-byte-range steps if either component overflows
 * it. Always sends at least one packet (even (0, 0), for button-only
 * changes), since the ST always reports current button state alongside
 * whatever motion happened. */
static void virtio_input_send_mouse_delta(WORD dx, WORD dy)
{
    UBYTE packet[3];
    WORD step_x, step_y;

    do
    {
        step_x = (dx > 127) ? 127 : (dx < -128) ? -128 : dx;
        step_y = (dy > 127) ? 127 : (dy < -128) ? -128 : dy;

        packet[0] = (UBYTE)(0xf8 | virtio_input_ptr_buttons);
        packet[1] = (UBYTE)step_x;
        packet[2] = (UBYTE)step_y;
        call_mousevec(packet);

        dx = (WORD)(dx - step_x);
        dy = (WORD)(dy - step_y);
    } while (dx != 0 || dy != 0);
}

static LONG virtio_input_scale_abs(LONG value, LONG min, LONG max, WORD screen_max)
{
    if (max <= min)
        return 0;
    return (value - min) * (LONG)screen_max / (max - min);
}
```

- [ ] **Step 3: Add the pointer dispatch function**

Add right after `virtio_input_scale_abs()`:

```c
static void virtio_input_handle_pointer(UWORD type, UWORD code, ULONG raw_value)
{
    LONG value = (LONG)raw_value;

    switch (type)
    {
    case EV_KEY:
        switch (code)
        {
        case BTN_LEFT:
            if (value)
                virtio_input_ptr_buttons |= VIRTIO_INPUT_MOUSE_LEFT;
            else
                virtio_input_ptr_buttons &= ~VIRTIO_INPUT_MOUSE_LEFT;
            virtio_input_send_mouse_delta(0, 0);
            break;
        case BTN_RIGHT:
            if (value)
                virtio_input_ptr_buttons |= VIRTIO_INPUT_MOUSE_RIGHT;
            else
                virtio_input_ptr_buttons &= ~VIRTIO_INPUT_MOUSE_RIGHT;
            virtio_input_send_mouse_delta(0, 0);
            break;
        case BTN_MIDDLE:
            /* No 3rd button bit in the relative-mouse packet; matches
             * usb/udd_mouse.c's handling of its own 3rd button. */
            mousexvec(value ? 0x37 : 0xb7);
            break;
        default:
            break;
        }
        break;

    case EV_REL:
        if (code == REL_X)
            virtio_input_send_mouse_delta((WORD)value, 0);
        else if (code == REL_Y)
            virtio_input_send_mouse_delta(0, (WORD)value);
        break;

    case EV_ABS:
        if (code == ABS_X)
        {
            LONG scaled = virtio_input_scale_abs(value, virtio_input_abs_min_x, virtio_input_abs_max_x,
                                                  (WORD)(linea_vars.V_REZ_HZ - 1));
            if (virtio_input_x_valid)
                virtio_input_send_mouse_delta((WORD)(scaled - virtio_input_last_scaled_x), 0);
            virtio_input_last_scaled_x = scaled;
            virtio_input_x_valid = TRUE;
        }
        else if (code == ABS_Y)
        {
            LONG scaled = virtio_input_scale_abs(value, virtio_input_abs_min_y, virtio_input_abs_max_y,
                                                  (WORD)(linea_vars.V_REZ_VT - 1));
            if (virtio_input_y_valid)
                virtio_input_send_mouse_delta(0, (WORD)(scaled - virtio_input_last_scaled_y));
            virtio_input_last_scaled_y = scaled;
            virtio_input_y_valid = TRUE;
        }
        break;

    default:
        break;   /* EV_SYN and anything else: nothing to do per-event */
    }
}
```

- [ ] **Step 4: Wire the dispatch into `virtio_input_drain()`**

In `bios/virtio_input.c`, replace the comment placeholder inside `virtio_input_drain()`:

```c
        if (role == VIRTIO_INPUT_ROLE_KEYBOARD)
        {
            if (type == EV_KEY)
                virtio_input_handle_key(code, value);
        }
        /* VIRTIO_INPUT_ROLE_POINTER: dispatch added in Task 3 */
```

with:

```c
        if (role == VIRTIO_INPUT_ROLE_KEYBOARD)
        {
            if (type == EV_KEY)
                virtio_input_handle_key(code, value);
        }
        else
        {
            virtio_input_handle_pointer(type, code, value);
        }
```

- [ ] **Step 5: Update `readme.md`**

Modify `readme.md`. After the existing ARM `virt` "To also attach a virtio-blk disk" example, add:

```
To also attach virtio-input keyboard and tablet devices:

    qemu-system-arm -M virt -cpu cortex-a7 -m 128 -kernel virt-arm.elf -d guest_errors -serial stdio \
      -global virtio-mmio.force-legacy=false \
      -device virtio-keyboard-device -device virtio-tablet-device
```

After the equivalent m68k `virt` example, add:

```
To also attach virtio-input keyboard and tablet devices:

    qemu-system-m68k -M virt -m 128 -cpu m68020 -kernel virt-m68k.elf -d guest_errors -serial stdio \
      -global virtio-mmio.force-legacy=false \
      -device virtio-keyboard-device -device virtio-tablet-device
```

- [ ] **Step 6: Verify portability — build a non-virt config**

```bash
make distclean
make rpi2_defconfig && make -j"$(nproc)" 2>&1 | tail -20
```

Expected: builds cleanly.

- [ ] **Step 7: Build and test the keyboard regression on ARM**

```bash
make distclean
make virt-arm_defconfig && make -j"$(nproc)"
qemu-system-arm -M virt -cpu cortex-a7 -kernel <image> \
  -global virtio-mmio.force-legacy=false \
  -serial stdio -d guest_errors -monitor stdio \
  -device virtio-keyboard-device -device virtio-tablet-device
```

`(qemu) sendkey a` — expected: same as Task 2 Step 9, confirming the pointer changes didn't regress the keyboard path.

- [ ] **Step 8: Interactive pointer test (human required)**

This step cannot be driven by an agent alone — QEMU only emits absolute tablet coordinates in response to real host mouse movement over its display window, and there is no way to inject that from inside the guest or via the monitor. Ask a human to:

1. Boot the same image with a graphical QEMU window open (not `-nographic`; a plain `qemu-system-arm -M virt ... -device virtio-tablet-device` without `-serial stdio -display none` will pop up a window).
2. Move the host mouse over the QEMU display window and confirm the guest cursor tracks it 1:1, with no drift after repeated movement across the screen (this specifically exercises the `ABS_INFO`-based scaling math in `virtio_input_scale_abs()`).
3. Click left and right mouse buttons and confirm they register in the guest (e.g. in the desktop's file selector or a running application).

Expected: cursor tracks smoothly, clicks register. If there's drift, double check `virtio_input_abs_min_x/max_x`/`min_y/max_y` (logged at boot in Task 1's KDEBUG output) against `linea_vars.V_REZ_HZ`/`V_REZ_VT` for the resolution actually in use.

- [ ] **Step 9: Repeat the keyboard regression check on m68k**

```bash
make distclean
make virt-m68k_defconfig && make -j"$(nproc)"
qemu-system-m68k -M virt -m 128 -cpu m68020 -kernel <image> \
  -serial stdio -d guest_errors -monitor stdio \
  -device virtio-keyboard-device -device virtio-tablet-device
```

`(qemu) sendkey a` as before. The interactive pointer test (Step 8) is optional to repeat here since the pointer logic is entirely shared, machine-independent code — the ARM confirmation is sufficient unless something m68k-specific (e.g. `phys_offset`) is suspected.

- [ ] **Step 10: `make gitready` and commit**

```bash
make gitready
git add bios/virtio_input.c readme.md
git commit -m "virtio-input: add mouse/tablet event dispatch"
git push
```

- [ ] **Step 11: Mark the PR ready for review**

Only after Step 8's human pointer test has actually passed:

```bash
gh pr ready
```

(Per this repo's workflow, the user merges the PR themselves once satisfied — do not merge or force-push it.)
