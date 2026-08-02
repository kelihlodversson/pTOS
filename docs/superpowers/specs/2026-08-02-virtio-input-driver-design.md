# Shared virtio-input keyboard/mouse driver for the QEMU virt ports — design

GitHub tracking issue: #38. Depends on #29 (shared virtio-mmio transport
driver, landed). Reference: `docs/superpowers/specs/2026-07-31-virtio-mmio-driver-design.md`
for the transport this builds on, and
`docs/superpowers/specs/2026-07-30-qemu-virt-support-design.md` for the two
boards' overall shape.

## Scope & goal

QEMU's `virt` machine (both ARM and m68k) has no PS/2 controller or
ACIA-attached keyboard: input arrives as virtio-input devices
(`virtio-keyboard-device`, `virtio-tablet-device`, `virtio-mouse-device`) on
the virtio-mmio bus. This adds a driver that:

- Discovers virtio-input devices on the virtio-mmio transport and identifies
  their role (keyboard vs. pointer) from config-space capability bits.
- Parses `struct virtio_input_event` off the device's eventq.
- Translates evdev key/button/motion codes into Atari IKBD scancodes and
  mouse packets.
- Feeds those into the existing, already machine-independent IKBD entry
  points in `bios/ikbd.c`: `kbd_int(UBYTE scancode)` and
  `call_mousevec(UBYTE *packet)` — the same integration pattern
  `usb/udd_mouse.c` already uses for the raspi port's USB HID mouse.

In scope: one keyboard, one pointer device (virtio-tablet-device preferred,
virtio-mouse-device also supported — see "Mouse event handling"). Out of
scope: LED/status feedback (statusq), multiple keyboards or pointers,
joysticks, any device role beyond keyboard/mouse/tablet.

## Layout

```
bios/virtio_input.c, bios/virtio_input.h        -- new: device discovery,
                                                    event translation, IKBD glue
bios/virtio_input_keytbl.c, bios/virtio_input_keytbl.h
                                                 -- new: evdev KEY_* -> Atari
                                                    scancode table
util/virtio.c, util/virtio.h                    -- extended: virtio_pop_used()
```

Placed in `bios/`, not `util/` as issue #38's text loosely suggested.
`virtio_blk.c` sets the actual precedent here: it too is CPU-independent, but
lives in `bios/` because its glue (`disk.c` bus integration) is
bios-specific. The same applies here, more so — `kbd_int()`/`call_mousevec()`
(`bios/ikbd.h`) and the screen-resolution globals
(`linea_vars.V_REZ_HZ`/`V_REZ_VT`, `include/lineavars.h`) this driver needs
are all bios-only, and nothing under `util/` today includes any bios header.
`util/virtio.c` remains the shared, machine-neutral transport; only the one
small addition described below goes there.

The keyboard translation table is split into its own file/pair
(`virtio_input_keytbl.c`/`.h`) purely to keep `virtio_input.c` (device
discovery, event loop, packet building) readable — the table itself is a
large, static, mostly-mechanical array with no logic.

## Kconfig

In `bios/Kconfig`, alongside `CONF_WITH_VIRTIO_BLK`:

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

Defaults to `y` under `CONF_WITH_VIRTIO`, same reasoning as
`CONF_WITH_VIRTIO_BLK`: if nothing matching is on the command line, the probe
loop simply finds no devices. No `configs/*_defconfig` changes expected.

Called from `bios/bios.c`, right after the existing `usb_init()` call (same
structural spot: after `kbd_init()` has set up the scancode/ASCII tables,
before interrupts are enabled), guarded by `#if CONF_WITH_VIRTIO_INPUT`.

## Device discovery

Unlike virtio-blk (device ID 2), every virtio-input device — keyboard, mouse,
*and* tablet — shares device ID **18**. `virtio_probe()` can't distinguish
them by ID, so `virtio_input_init()`:

1. Scans every virtio-mmio slot (same `VIRTIO_MMIO_BASE`/`_STRIDE`/`_COUNT`
   per-board constants `virtio_blk.c` already defines, reused here) calling
   `virtio_probe(base, 18, &dev)`.
2. For each match, reads config space to determine role, via the
   select/subsel/size/data window at offset `0x100` from the device's mmio
   base (per the virtio-input spec):
   - Write `select = 0x11` (`EV_BITS`), `subsel = 0x01` (`EV_KEY`), read
     `size`. Nonzero → this device has keys → keyboard role.
   - Otherwise write `subsel = 0x03` (`EV_ABS`), read `size`. Nonzero → this
     device reports absolute position → tablet role (preferred pointer).
   - Otherwise write `subsel = 0x02` (`EV_REL`), read `size`. Nonzero → this
     device reports relative motion → mouse role.
   - Zero size on all three → unrecognized role; the slot is probed but not
     registered (`KDEBUG`-logged and skipped).
3. The first match of each role is registered; a second device of an
   already-filled role is `KDEBUG`-logged and left unregistered — no
   multi-keyboard/multi-pointer support (YAGNI: QEMU command lines for this
   port specify at most one of each).
4. For a registered tablet, `ABS_INFO` (`select = 0x12`, `subsel = 0x00` for
   `ABS_X`, `0x01` for `ABS_Y`) is queried once at init for each axis's
   `min`/`max`, stored for use when scaling motion (see below).

Each registered device gets `virtio_setup_queue()` called on queue 0 (the
eventq) exactly as `virtio_blk_init()` does; virtio-input's second queue
(statusq, queue 1) is out of scope (no LED feedback) and is never configured.

## Transport extension: draining the eventq

`virtio_blk`'s use of the transport is request/response: one descriptor
chain in flight at a time, waited on synchronously, so `dev->done` as a
single boolean is sufficient. virtio-input's eventq is the opposite shape:
the driver pre-populates all `VIRTIO_QUEUE_SIZE` (8) descriptors as
device-writable receive buffers up front, and the device fills them
asynchronously as events occur — potentially several between one interrupt
and the next. A boolean can't say how many or which.

Rather than have `virtio_input.c` reach into `VIRTIO_DEV`'s ring fields
directly (breaking the encapsulation `virtio_blk.c` already respects), add
one primitive to `util/virtio.c`/`.h`:

```c
/* Returns the descriptor index of the next unconsumed used-ring entry (and
 * its length in *out_len) and advances past it, or returns FALSE if the
 * driver has caught up with the device. Companion to virtio_submit(): the
 * caller resubmits the same (or another) descriptor once it has drained the
 * buffer's contents. */
BOOL virtio_pop_used(VIRTIO_DEV *dev, UWORD *out_index, ULONG *out_len);
```

This is a genuinely shared transport-level addition, not input-specific —
any future streaming virtio device (e.g. virtio-net) would need the same
capability — so it belongs alongside the existing transport API rather than
being duplicated per-consumer.

### Eventq buffer lifecycle

At init, `virtio_input.c` allocates 8 static `struct virtio_input_event`
buffers (8 bytes each: `UWORD type; UWORD code; ULONG value;`, all
little-endian on the wire) per registered device, submits all 8 descriptors
(device-writable, `VIRTIO_DESC_F_WRITE`), and notifies once.

On interrupt: `virtio_handle_interrupt()` (unchanged) sets `dev->done`. The
device's interrupt handler then loops `virtio_pop_used()`, and for each
completed buffer: decodes the event (`le2cpu16`/`le2cpu32`), translates and
dispatches it (see below), then immediately resubmits that same descriptor
(same buffer, still device-writable) so the device always has empty slots
available. No dynamic allocation — matches this port's boot-time-only
initialization model.

IRQ wiring reuses `virt_connect_irq()`/`goldfish_pic_connect_irq()` exactly
as `virtio_blk_connect_irq()` does today, one ISR per registered role
(keyboard, pointer).

## Keyboard event handling

`virtio_input_keytbl.c` provides a static table indexed by evdev `KEY_*`
code, mapping to the corresponding Atari ST make scancode. Coverage: standard
alpha/numeric/function/modifier/cursor keys — matching what this port's
existing keyboard language tables (`bios/lang/`) support; no multimedia or
exotic keys.

On each `EV_KEY` event (`event.type == EV_KEY`, i.e. `0x01`):

- `event.value == 2` (autorepeat): ignored. `bios/ikbd.c`'s own
  `kb_timerc_int()` already owns repeat timing; re-injecting the OS's own
  repeat events would double up with it.
- `event.value == 0` (release) or `1` (press): look up `event.code` in the
  table. A hit calls `kbd_int(scancode | (value == 0 ? KEY_RELEASED : 0))`.
  A miss is `KDEBUG`-logged and dropped — there is no sane fallback scancode
  for a key this port doesn't know about.

## Mouse event handling

Both tablet and relative-mouse devices resolve to the same output primitive:
`call_mousevec()` with a 3-byte IKBD relative-mouse packet
(`0xF8 | buttons`, `dx`, `dy`), following `usb/udd_mouse.c`'s precedent
exactly. No `Initmous()` call and no IKBD hardware commands of any kind —
there is no real IKBD chip here to configure; bypassing it (as the USB mouse
driver already does for the raspi port) is the correct integration point,
not a shortcut, since the real chip's own "absolute mode" is an internal
detail that still reports over the wire as relative deltas.

- **`EV_REL` (virtio-mouse):** `REL_X`/`REL_Y` (`event.code` `0x00`/`0x01`)
  carry small per-event deltas already. Each delta is clamped to signed-byte
  range; in the rare case a single event's value overflows it, it is split
  across multiple packets (a packet with `dx`/`dy` at the clamp limit,
  repeated) rather than silently truncated.
- **`EV_ABS` (virtio-tablet):** `ABS_X`/`ABS_Y` (`event.code` `0x00`/`0x01`)
  carry absolute positions in the device's own coordinate space (typically
  `0..32767`, from the `ABS_INFO` min/max queried at init — independent of
  screen resolution). Each new value is scaled into current screen-pixel
  space using `linea_vars.V_REZ_HZ`/`V_REZ_VT` and the queried min/max, then
  a delta is computed against the last scaled position (tracked per-device
  in a static), clamped/chunked into signed-byte range exactly as for the
  relative case, and sent the same way.
- **Buttons (`EV_KEY` with `BTN_LEFT`/`BTN_RIGHT`/`BTN_MIDDLE` codes on a
  pointer device):** map into the packet's button bits the same way
  `usb/udd_mouse.c` does. QEMU's virtio-tablet/mouse expose no more than
  these three buttons and no wheel, so — unlike the USB mouse driver, which
  also handles a wheel and a 4th/5th button via `mousexvec()` — there is
  nothing further to wire up.

## Build system wiring

```make
# bios/build.mk
obj-$(CONF_WITH_VIRTIO_INPUT) += virtio_input.o virtio_input_keytbl.o
```

`util/build.mk` needs no changes: `virtio.o` is already built under
`CONF_WITH_VIRTIO`, which `CONF_WITH_VIRTIO_INPUT` depends on.

`readme.md` gains `-device virtio-keyboard-device` / `-device
virtio-tablet-device` additions to the existing QEMU invocations for both
`virt` boards.

## Testing / definition of done

```sh
qemu-system-arm -M virt -cpu cortex-a7 -kernel <image> -serial stdio -d guest_errors \
  -device virtio-keyboard-device -device virtio-tablet-device

qemu-system-m68k -M virt -kernel <image> -serial stdio -d guest_errors \
  -device virtio-keyboard-device -device virtio-tablet-device
```

Definition of done, verified interactively (unlike `virtio_blk`'s
self-contained read/write self-test, there is no way to inject input events
from inside the guest, so this cannot be a boot-time self-test):

- Both devices are discovered and their roles logged via `KDEBUG` at boot.
- QEMU monitor `sendkey <key>` (or typing into the QEMU display window)
  produces the expected ASCII on the serial console / in a running editor.
- Moving the host mouse over the QEMU display window moves the cursor
  1:1 (no drift after repeated movement, confirming the tablet-scaling math)
  and clicks register.
