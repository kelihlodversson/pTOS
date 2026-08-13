# Async USB HID Interrupt Transfers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Raspberry Pi Timer-C HID polling with DWC2 IRQ-driven asynchronous interrupt-IN transfers for boot mouse and keyboard devices.

**Architecture:** Keep DWC2 host channel 0 and all existing enumeration, control, and bulk transfers synchronous. Add a narrow async interrupt-IN message API and a fixed DWC2 async-slot pool on channels 1–4; the USB IRQ handler re-arms NAKed requests and dispatches completed reports to mouse and keyboard callbacks. Full/low-speed devices behind the LAN951x high-speed hub retain start-split/complete-split handling.

**Tech Stack:** C90 with GNU extensions, pTOS USB UCD/UDD APIs, BCM2835/BCM2836 DWC2 host controller, ARM IRQs, DMA cache maintenance, QEMU Raspberry Pi USB HID devices.

## Global Constraints

- Keep host channel 0 exclusively for the current synchronous `chunk_msg()` path.
- Use only DWC2 channels 1–4 for a fixed async-slot pool; do not add a dynamic request scheduler.
- Keep synchronous enumeration, control, and bulk transfers unchanged.
- Process HID reports in ARM IRQ context; this matches the existing Timer-C report-dispatch context.
- Re-arm NAKed async IN transfers internally without invoking the UDD callback.
- Preserve DATA toggles over successful transfers and leave them unchanged for NAKs.
- Support start-split/complete-split transfers for full/low-speed devices behind a high-speed hub.
- Stop requests on real transport/protocol errors; never create a persistent error IRQ loop.
- Preserve boot mouse packet translation, wheel/extra-button behavior, keyboard rollover filtering, and release-before-press ordering.
- Suppress truly identical mouse reports because some devices send them despite `SET_IDLE(0)`.
- Remove USB I/O from `bios/arch/arm/vectors.c::int_timerc()`.
- Update `.claude/skills/ptos-smoketest/SKILL.md` so rpi1/rpi2 QEMU commands require `-device usb-mouse -device usb-kbd`.
- Keep all C changes C90-compatible and use `/* */` comments.

---

## File Structure

- Modify `usb/usb_api.h`: add async interrupt-IN UCD ioctl opcodes, callback type, and request structure.
- Modify `usb/usb.h`: declare generic submit/cancel wrappers for persistent async interrupt-IN requests.
- Modify `usb/usb.c`: route the new wrappers to the owning UCD's ioctl method.
- Modify `usb/ucd_dwc2.c`: add fixed async slots, USB IRQ setup/handler, DWC2 channel state machine, DMA handling, submit/cancel ioctl cases, and safe low-level teardown.
- Modify `usb/udd_mouse.c`: replace Timer-C polling and unused legacy irq stub with an async completion callback and probe/disconnect submission lifecycle.
- Modify `usb/udd_keyboard.c`: replace Timer-C polling and unused legacy irq stub with an async completion callback and probe/disconnect submission lifecycle.
- Modify `bios/arch/arm/vectors.c`: remove HID polling declarations and calls from Timer-C.
- Modify `.claude/skills/ptos-smoketest/SKILL.md`: attach QEMU USB mouse and keyboard to rpi1/rpi2 smoke-test commands and require their enumeration as the input-path signal.

## Task 1: Add the Narrow Async Interrupt-IN API

**Files:**
- Modify: `usb/usb_api.h:39-74`
- Modify: `usb/usb.h` near the existing `usb_submit_int_msg()` declaration
- Modify: `usb/usb.c:119-140`

**Interfaces:**
- Consumes: `struct usb_device`, `struct ucdif`, and the existing UCD ioctl dispatch.
- Produces:
  ```c
  typedef void (*usb_async_int_callback_t)(struct usb_async_int_msg *msg,
                                           LONG status, LONG actual_length);

  struct usb_async_int_msg {
      struct usb_device *dev;
      ULONG pipe;
      void *buffer;
      LONG transfer_len;
      LONG interval;
      usb_async_int_callback_t callback;
      void *context;
  };

  LONG usb_submit_async_int_msg(struct usb_async_int_msg *msg);
  LONG usb_cancel_async_int_msg(struct usb_async_int_msg *msg);
  ```
- The UCD ioctl opcodes are `SUBMIT_ASYNC_INT_MSG` and `CANCEL_ASYNC_INT_MSG`, allocated after `SUBMIT_INT_MSG`.

- [ ] **Step 1: Add the request type and ioctl opcodes**

In `usb/usb_api.h`, add the two opcodes after `SUBMIT_INT_MSG` and define the callback/request structure after `struct int_msg`:

```c
#define SUBMIT_ASYNC_INT_MSG  (('U' << 8) | 5)
#define CANCEL_ASYNC_INT_MSG  (('U' << 8) | 6)

struct usb_async_int_msg;
typedef void (*usb_async_int_callback_t)(struct usb_async_int_msg *msg,
                                         LONG status, LONG actual_length);

struct usb_async_int_msg
{
    struct usb_device *dev;
    ULONG pipe;
    void *buffer;
    LONG transfer_len;
    LONG interval;
    usb_async_int_callback_t callback;
    void *context;
};
```

- [ ] **Step 2: Add the generic wrapper declarations**

In `usb/usb.h`, place these declarations beside the existing synchronous interrupt-message API:

```c
LONG usb_submit_async_int_msg(struct usb_async_int_msg *msg);
LONG usb_cancel_async_int_msg(struct usb_async_int_msg *msg);
```

- [ ] **Step 3: Implement thin UCD-routing wrappers**

Add these functions in `usb/usb.c` after `usb_submit_int_msg()`:

```c
LONG usb_submit_async_int_msg(struct usb_async_int_msg *msg)
{
    struct ucdif *ucd;

    if (!msg || !msg->dev || !msg->buffer || msg->transfer_len <= 0
        || !msg->callback)
        return -1;

    ucd = msg->dev->controller;
    return (*ucd->ioctl)(ucd, SUBMIT_ASYNC_INT_MSG, (LONG)msg);
}

LONG usb_cancel_async_int_msg(struct usb_async_int_msg *msg)
{
    struct ucdif *ucd;

    if (!msg || !msg->dev)
        return -1;

    ucd = msg->dev->controller;
    return (*ucd->ioctl)(ucd, CANCEL_ASYNC_INT_MSG, (LONG)msg);
}
```

- [ ] **Step 4: Build a Raspberry Pi configuration to validate the API is wired without callers**

Run:

```bash
make rpi2_defconfig && make
```

Expected: compilation and linking complete; no code outside the new wrappers calls the API yet.

- [ ] **Step 5: Commit the API boundary**

```bash
git add usb/usb_api.h usb/usb.h usb/usb.c
git commit -m "Add async USB interrupt message API"
```

## Task 2: Add DWC2 IRQ-Driven Async Interrupt-IN Slots

**Files:**
- Modify: `usb/ucd_dwc2.c:18-90,456-478,818-875,1185-1225,1242-1291`

**Interfaces:**
- Consumes: `struct usb_async_int_msg`, `ARM_IRQ_USB`, `raspi_connect_irq()`, `phys_to_bus()`, cache-maintenance functions, and DWC2 register definitions from `usb/ucd_dwc2.h`.
- Produces: support for `SUBMIT_ASYNC_INT_MSG` and `CANCEL_ASYNC_INT_MSG` in `dwc2_ioctl()`; callbacks receive `0` and a positive actual length for a successful transfer, or `-1` and `0` for a stopped transport/protocol error.

- [ ] **Step 1: Define slot limits and per-slot state**

Keep `DWC2_HC_CHANNEL` at `0` and add these constants and types near it:

```c
#define DWC2_ASYNC_FIRST_CHANNEL  1
#define DWC2_ASYNC_SLOT_COUNT     4
#define DWC2_ASYNC_DMA_SIZE       8

typedef enum {
    DWC2_ASYNC_SPLIT_NONE,
    DWC2_ASYNC_SPLIT_START,
    DWC2_ASYNC_SPLIT_COMPLETE
} DWC2_ASYNC_SPLIT_STATE;

struct dwc2_async_slot
{
    struct usb_async_int_msg *msg;
    UBYTE pid;
    BOOL active;
    BOOL cancelled;
    BOOL split;
    BOOL split_complete;
    DWC2_ASYNC_SPLIT_STATE split_state;
    UBYTE hub_addr;
    UBYTE hub_port;
    UBYTE *dma_buffer;
};
```

Add this standalone storage after `dwc2_local`:

```c
static UBYTE dwc2_async_dma[DWC2_ASYNC_SLOT_COUNT]
    [roundup(DWC2_ASYNC_DMA_SIZE, ARCH_DMA_MINALIGN)]
    __attribute__((aligned(ARCH_DMA_MINALIGN)));
```

Assign `slot->dma_buffer = dwc2_async_dma[index]` when allocating a slot. Do
not use the synchronous `aligned_buffer_addr`, because an IRQ transfer may run
while channel 0 is enumerating a device.

Extend `struct dwc2_priv` with:

```c
struct dwc2_async_slot async[DWC2_ASYNC_SLOT_COUNT];
BOOL irq_connected;
```

- [ ] **Step 2: Add slot lookup, start, stop, and DMA helpers**

Add static helpers with these exact responsibilities:

```c
static struct dwc2_async_slot *dwc2_async_find(struct dwc2_priv *priv,
                                                struct usb_async_int_msg *msg);
static struct dwc2_async_slot *dwc2_async_alloc(struct dwc2_priv *priv,
                                                 struct usb_async_int_msg *msg);
static void dwc2_async_start(struct dwc2_priv *priv,
                             struct dwc2_async_slot *slot);
static void dwc2_async_stop(struct dwc2_priv *priv,
                            struct dwc2_async_slot *slot);
static void dwc2_async_complete(struct dwc2_priv *priv,
                                struct dwc2_async_slot *slot,
                                LONG status, LONG actual_length);
static void dwc2_async_finish_success(struct dwc2_priv *priv,
                                      struct dwc2_async_slot *slot,
                                      struct dwc2_hc_regs *hc);
static void dwc2_async_finish_error(struct dwc2_priv *priv,
                                    struct dwc2_async_slot *slot);
```

`dwc2_async_start()` must:

1. Derive the host channel as `DWC2_ASYNC_FIRST_CHANNEL + slot_index`.
2. Call `dwc_otg_hc_init()` for `slot->msg->dev`, its IN pipe endpoint, and
   `DWC2_HCCHAR_EPTYPE_INTR`.
3. Restore hub split information when `slot->split` is true; use
   `dwc_otg_hc_init_split()` and set `DWC2_HCSPLT_COMPSPLT` only for
   `DWC2_ASYNC_SPLIT_COMPLETE`.
4. Program one packet in `HCTSIZ` with `slot->pid`.
5. Invalidate the DMA buffer, program `HCDMA` with `phys_to_bus()`, clear all
   pending channel interrupts, and unmask `XFERCOMP`, `CHHLTD`, `STALL`,
   `AHBERR`, `XACTERR`, `BBLERR`, `DATATGLERR`, `NAK`, `ACK`, and `NYET`.
6. Set the channel bit in `HAINTMSK`.
7. Read `HFNUM`; select the opposite parity for `HCCHAR.ODDFRM` when the
   current frame is even, matching the existing synchronous interrupt path.
8. Set `HCCHAR.CHENA` with `CHDIS` clear.

`dwc2_async_stop()` must clear the slot's `HAINTMSK` bit, request channel
disable if `HCCHAR.CHENA` is set, clear its interrupt mask/status, and reset
`active`, `cancelled`, and `msg` only after no re-arm can occur.

`dwc2_async_complete()` must invoke `msg->callback(msg, status, actual_length)`
while the slot stays active; it must not call a callback after cancellation.

`dwc2_async_finish_success()` must read `HCTSIZ`, calculate:

```c
actual_length = slot->msg->transfer_len
                - ((hctsiz & DWC2_HCTSIZ_XFERSIZE_MASK)
                   >> DWC2_HCTSIZ_XFERSIZE_OFFSET);
slot->pid = (hctsiz & DWC2_HCTSIZ_PID_MASK) >> DWC2_HCTSIZ_PID_OFFSET;
```

It then invalidates `slot->dma_buffer` for
`roundup(actual_length, ARCH_DMA_MINALIGN)`, copies `actual_length` bytes into
`slot->msg->buffer`, calls `dwc2_async_complete(priv, slot, 0, actual_length)`,
and calls `dwc2_async_start(priv, slot)` unless cancellation occurred during
the callback.

`dwc2_async_finish_error()` must call `dwc2_async_stop(priv, slot)` before
calling `slot->msg->callback(slot->msg, -1, 0)`. Save `msg` in a local before
stopping the slot so the callback argument remains valid. It must not re-arm.

- [ ] **Step 3: Implement the DWC2 USB IRQ handler**

Add `static void dwc2_irq_handler(void)` and have it:

```c
static void dwc2_irq_handler(void)
{
    struct dwc2_priv *priv = &dwc2_local;
    struct dwc2_core_regs *regs = priv->regs;
    ULONG gintsts;
    ULONG pending;
    WORD index;

    gintsts = readl(&regs->gintsts);
    if (!(gintsts & DWC2_GINTSTS_HCINTR))
        return;

    pending = readl(&regs->haint) & readl(&regs->haintmsk);
    for (index = 0; index < DWC2_ASYNC_SLOT_COUNT; index++)
    {
        struct dwc2_async_slot *slot = &priv->async[index];
        WORD channel = DWC2_ASYNC_FIRST_CHANNEL + index;
        struct dwc2_hc_regs *hc = &regs->hc_regs[channel];
        ULONG hcint;

        if (!(pending & (1UL << channel)))
            continue;

        hcint = readl(&hc->hcint);
        writel(hcint, &hc->hcint);

        if (!slot->active || slot->cancelled)
        {
            dwc2_async_stop(priv, slot);
            continue;
        }

        if (slot->split)
        {
            if (slot->split_state == DWC2_ASYNC_SPLIT_START
                && (hcint & DWC2_HCINT_ACK))
            {
                slot->split_state = DWC2_ASYNC_SPLIT_COMPLETE;
                dwc2_async_start(priv, slot);
            }
            else if (slot->split_state == DWC2_ASYNC_SPLIT_COMPLETE
                     && (hcint & DWC2_HCINT_NYET))
            {
                dwc2_async_start(priv, slot);
            }
            else if (slot->split_state == DWC2_ASYNC_SPLIT_COMPLETE
                     && (hcint & DWC2_HCINT_NAK))
            {
                slot->split_state = DWC2_ASYNC_SPLIT_START;
                dwc2_async_start(priv, slot);
            }
            else if (hcint & DWC2_HCINT_XFERCOMP)
            {
                slot->split_state = DWC2_ASYNC_SPLIT_START;
                dwc2_async_finish_success(priv, slot, hc);
            }
            else
            {
                dwc2_async_finish_error(priv, slot);
            }
        }
        else if (hcint & DWC2_HCINT_XFERCOMP)
        {
            dwc2_async_finish_success(priv, slot, hc);
        }
        else if ((hcint & DWC2_HCINT_NAK) || hcint == DWC2_HCINT_CHHLTD)
        {
            dwc2_async_start(priv, slot);
        }
        else
        {
            dwc2_async_finish_error(priv, slot);
        }
    }
}
```

Complete the dispatch with these exact rules:

- For non-split `XFERCOMP`, compute `actual_length` as
  `msg->transfer_len - remaining_hctsiz_bytes`, update `slot->pid` from
  `HCTSIZ.PID`, invalidate/copy the DMA bytes to `msg->buffer`, callback with
  `(0, actual_length)`, then call `dwc2_async_start()` unless cancelled.
- For non-split `NAK` or bare `CHHLTD`, call `dwc2_async_start()` without a
  callback or toggle update.
- For start split `ACK`, set `split_state = DWC2_ASYNC_SPLIT_COMPLETE` and
  re-arm.
- For complete split `NYET`, retain complete-split state and re-arm.
- For complete split `NAK`, set `split_state = DWC2_ASYNC_SPLIT_START` and
  re-arm.
- For complete split `XFERCOMP`, copy/callback/re-arm from start-split state.
- For `STALL`, `AHBERR`, `XACTERR`, `BBLERR`, `DATATGLERR`, or any unhandled
  channel status, stop the slot then callback with `(-1, 0)`; do not re-arm.

Read and acknowledge `HAINT`/`GINTSTS` after servicing the channels, following
the write-back-to-clear convention used by DWC2 host interrupt registers.

- [ ] **Step 4: Wire initialization, teardown, and ioctl dispatch**

In `usb_lowlevel_init()` after `dwc2_init_core()` succeeds:

```c
raspi_connect_irq(ARM_IRQ_USB, dwc2_irq_handler);
setbits_le32(&regs->gintmsk, DWC2_GINTMSK_HCINTR);
setbits_le32(&regs->gahbcfg, DWC2_GAHBCFG_GLBLINTRMSK);
priv->irq_connected = TRUE;
```

In `usb_lowlevel_stop()`, stop every active async slot, clear
`DWC2_GINTMSK_HCINTR` and `DWC2_GAHBCFG_GLBLINTRMSK`, disconnect
`ARM_IRQ_USB`, and set `irq_connected = FALSE` before calling
`dwc2_uninit_common()`.

Add `dwc2_submit_async_int_msg()` and `dwc2_cancel_async_int_msg()` and route
the two new ioctl opcodes in `dwc2_ioctl()`. Submission must reject:

```c
!msg || !msg->dev || !msg->buffer || !msg->callback
|| msg->transfer_len <= 0 || msg->transfer_len > DWC2_ASYNC_DMA_SIZE
|| !usb_pipein(msg->pipe)
|| usb_pipetype(msg->pipe) != PIPE_INTERRUPT
```

For split detection, copy the existing `chunk_msg()` policy:

```c
if (msg->dev->speed != USB_SPEED_HIGH
    && (readl(&priv->regs->hprt0) & DWC2_HPRT0_PRTSPD_MASK)
        == DWC2_HPRT0_PRTSPD_HIGH)
    usb_find_usb2_hub_address_port(msg->dev, &slot->hub_addr, &slot->hub_port);
```

Mark `slot->split` only when that lookup identifies a hub path.

- [ ] **Step 5: Build all DWC2 Raspberry Pi configurations**

Run each command serially, because each rewrites `.config`:

```bash
make rpi1_defconfig && make
make rpi2_defconfig && make
make rpi2-sparse_defconfig && make
make rpi3_defconfig && make
```

Expected: all images link. At this stage no UDD submits an async request, so
the new IRQ path is compiled but inactive.

- [ ] **Step 6: Commit the DWC2 async foundation**

```bash
git add usb/ucd_dwc2.c
git commit -m "Add async DWC2 interrupt transfer support"
```

## Task 3: Convert the USB Mouse UDD to Persistent Async Reports

**Files:**
- Modify: `usb/udd_mouse.c:25-193,201-290`

**Interfaces:**
- Consumes: `struct usb_async_int_msg`, `usb_submit_async_int_msg()`, and `usb_cancel_async_int_msg()` from Task 1.
- Consumes: successful callback signature `void mouse_report_complete(struct usb_async_int_msg *msg, LONG status, LONG actual_length)`.
- Produces: mouse reports reach `call_mousevec()` from `ARM_IRQ_USB`; `usb_mouse_timerc()` and `usb_mouse_irq()` no longer exist.

- [ ] **Step 1: Make the report buffer and request persistent**

Replace the `char new[8]` field in `struct mse_data` with a `UBYTE report[8]`
field. Add this static request next to `mse_data`:

```c
static struct usb_async_int_msg mouse_request;
```

Remove unused declarations and fields: `mouse_packet` may remain; remove
`usb_mouse_timerc()`, `usb_mouse_irq()`, `ep_out`, and `irq_handle`.

- [ ] **Step 2: Extract the report decoder into an async callback**

Replace `usb_mouse_timerc()` with:

```c
static void mouse_report_complete(struct usb_async_int_msg *msg,
                                  LONG status, LONG actual_length)
{
    UBYTE info;
    UBYTE old_info;
    BYTE delta_x;
    BYTE delta_y;
    UBYTE extra;
    BOOL changed;

    if (status || actual_length < 3 || actual_length > 8)
    {
        KDEBUG(("usb mouse interrupt transfer failed (%ld, %ld)\n",
                status, actual_length));
        return;
    }

    info = mse_data.report[0];
    old_info = mse_data.data[0];
    delta_x = mse_data.report[1];
    delta_y = mse_data.report[2];
    extra = (actual_length >= 4) ? mse_data.report[3] : 0;
    changed = (info != old_info) || delta_x || delta_y
              || ((actual_length >= 4) && (extra != mse_data.data[3]));
    if (!changed)
        return;

    mouse_packet[0] = ((info & 1) << 1) | ((info & 2) >> 1) | 0xf8;
    mouse_packet[1] = delta_x;
    mouse_packet[2] = delta_y;

    if ((info ^ old_info) & 4)
        mousexvec((info & 4) ? 0x37 : 0xb7);

    switch (extra & 0x0f)
    {
    case 0x1:
        mousexvec(0x59);
        break;
    case 0x2:
        mousexvec(0x5d);
        break;
    case 0xe:
        mousexvec(0x5c);
        break;
    case 0xf:
        mousexvec(0x5a);
        break;
    default:
        break;
    }

    call_mousevec(mouse_packet);
    mse_data.data[0] = info;
    mse_data.data[1] = delta_x;
    mse_data.data[2] = delta_y;
    if (actual_length >= 4)
        mse_data.data[3] = extra;
    else
        mse_data.data[3] = 0;
    mse_data.data[4] = 0;
    mse_data.data[5] = 0;
}
```

This callback fixes stale wheel bytes noted in the review of PR #168: it
stores byte 3 only when received and clears bytes 4–5 because boot-protocol
mouse reports have no defined values there.

Do not resubmit from the callback: Task 2's DWC2 slot re-arms successful
transfers after the callback returns.

- [ ] **Step 3: Submit at probe and cancel at disconnect**

At the end of successful `mouse_probe()` initialization, after all endpoint
metadata, report buffers, boot protocol, and idle mode are initialized, fill
the request:

```c
mouse_request.dev = dev;
mouse_request.pipe = mse_data.irqpipe;
mouse_request.buffer = mse_data.report;
mouse_request.transfer_len = mse_data.irqmaxp > 8 ? 8 : mse_data.irqmaxp;
mouse_request.interval = mse_data.irqinterval;
mouse_request.callback = mouse_report_complete;
mouse_request.context = &mse_data;
mse_data.pusb_dev = dev;

if (usb_submit_async_int_msg(&mouse_request))
{
    mse_data.pusb_dev = NULL;
    return -1;
}
```

Do not assign `dev->irq_handle`.

In `mouse_disconnect()`, cancel before clearing the device pointer:

```c
if (dev == mse_data.pusb_dev)
{
    usb_cancel_async_int_msg(&mouse_request);
    mse_data.pusb_dev = NULL;
}
```

- [ ] **Step 4: Build and smoke rpi1 with an attached HID mouse**

Run:

```bash
make rpi1_defconfig && make
qemu-system-arm -M raspi1ap -bios kernel.img -device usb-mouse -device usb-kbd \
  -d guest_errors -serial stdio
```

Expected: pTOS reaches the boot/desktop path without guest errors. Serial
output shows the USB mouse class driver probes the attached boot mouse; no
Timer-C mouse poll symbol remains.

- [ ] **Step 5: Commit the mouse migration**

```bash
git add usb/udd_mouse.c
git commit -m "Move USB mouse input to DWC2 interrupts"
```

## Task 4: Convert the USB Keyboard UDD and Remove Timer-C USB Polling

**Files:**
- Modify: `usb/udd_keyboard.c:7-14,37-48,119-287,289-375`
- Modify: `bios/arch/arm/vectors.c:109-144`

**Interfaces:**
- Consumes: the persistent async interrupt-IN API and DWC2 semantics from Tasks 1–2.
- Produces: keyboard reports reach `kbd_int()` from `ARM_IRQ_USB`; `int_timerc()` no longer references USB HID polling.

- [ ] **Step 1: Add a persistent keyboard report buffer and request**

Extend `struct kbd_data` with:

```c
UBYTE report[8];
```

Add next to `kbd_data`:

```c
static struct usb_async_int_msg keyboard_request;
```

Remove the top-file wording that says Timer C polls the endpoint. Remove
`usb_keyboard_timerc()` and the unused `usb_keyboard_irq()` stub.

- [ ] **Step 2: Move keyboard report processing into its callback**

Create:

```c
static void keyboard_report_complete(struct usb_async_int_msg *msg,
                                     LONG status, LONG actual_length)
{
    UBYTE changed_mod;
    int i;

    if (status || actual_length < 8)
    {
        KDEBUG(("usb keyboard interrupt transfer failed (%ld, %ld)\n",
                status, actual_length));
        return;
    }

    for (i = 0; i < 6; i++)
    {
        if (kbd_data.report[2 + i] >= 1 && kbd_data.report[2 + i] <= 3)
            return;
    }

    kbd_data.new_modifier = kbd_data.report[0];
    for (i = 0; i < 6; i++)
        kbd_data.new_keys[i] = kbd_data.report[2 + i];

    for (i = 0; i < 6; i++)
    {
        UBYTE key = kbd_data.keys[i];

        if (key && key < USB_KEYTBL_SIZE
            && !usb_keyboard_key_in(kbd_data.new_keys, key))
        {
            UBYTE scancode = usb_keytbl[key];
            if (scancode)
                kbd_int(scancode | KEY_RELEASED);
        }
    }

    changed_mod = kbd_data.modifier ^ kbd_data.new_modifier;
    for (i = 0; i < 8; i++)
    {
        if ((changed_mod & (1 << i)) && !(kbd_data.new_modifier & (1 << i)))
        {
            UBYTE scancode = usb_modtbl[i];
            if (scancode)
                kbd_int(scancode | KEY_RELEASED);
        }
    }

    for (i = 0; i < 8; i++)
    {
        if ((changed_mod & (1 << i)) && (kbd_data.new_modifier & (1 << i)))
        {
            UBYTE scancode = usb_modtbl[i];
            if (scancode)
                kbd_int(scancode);
        }
    }

    for (i = 0; i < 6; i++)
    {
        UBYTE key = kbd_data.new_keys[i];

        if (key && key < USB_KEYTBL_SIZE
            && !usb_keyboard_key_in(kbd_data.keys, key))
        {
            UBYTE scancode = usb_keytbl[key];
            if (scancode)
                kbd_int(scancode);
        }
    }

    kbd_data.modifier = kbd_data.new_modifier;
    for (i = 0; i < 6; i++)
        kbd_data.keys[i] = kbd_data.new_keys[i];
}
```

Do not re-submit from the callback.

- [ ] **Step 3: Submit/cancel the keyboard request in lifecycle handlers**

At the end of `keyboard_probe()`, after boot protocol and `SET_IDLE`, set:

```c
keyboard_request.dev = dev;
keyboard_request.pipe = kbd_data.irqpipe;
keyboard_request.buffer = kbd_data.report;
keyboard_request.transfer_len = kbd_data.irqmaxp > 8 ? 8 : kbd_data.irqmaxp;
keyboard_request.interval = kbd_data.irqinterval;
keyboard_request.callback = keyboard_report_complete;
keyboard_request.context = &kbd_data;
kbd_data.pusb_dev = dev;

if (usb_submit_async_int_msg(&keyboard_request))
{
    kbd_data.pusb_dev = NULL;
    return -1;
}
```

Remove `dev->irq_handle` assignment. In `keyboard_disconnect()`, call
`usb_cancel_async_int_msg(&keyboard_request)` immediately after confirming
the device matches and before synthesizing releases.

- [ ] **Step 4: Remove USB work from Timer C**

In `bios/arch/arm/vectors.c`, remove:

```c
#if CONF_WITH_USB
extern void usb_mouse_timerc (void);
extern void usb_keyboard_timerc (void);
#endif
```

and remove the entire `#if CONF_WITH_USB` block calling those functions from
`int_timerc()`. Keep the existing `kb_timerc_int()`, optional `sndirq()`, and
50 Hz `int_vbl()` structure unchanged.

- [ ] **Step 5: Compile all affected configurations**

Run:

```bash
make rpi1_defconfig && make
make rpi2_defconfig && make
make rpi2-sparse_defconfig && make
make rpi3_defconfig && make
make rpi4_defconfig && make
```

Expected: rpi1–rpi3 compile their DWC2 async path; rpi4 still compiles without
pulling in DWC2 async behavior.

- [ ] **Step 6: Commit keyboard and Timer-C migration**

```bash
git add usb/udd_keyboard.c bios/arch/arm/vectors.c
git commit -m "Move USB keyboard input to DWC2 interrupts"
```

## Task 5: Require QEMU HID Devices in the Smoke-Test Skill and Validate

**Files:**
- Modify: `.claude/skills/ptos-smoketest/SKILL.md:195-206,254-255`

**Interfaces:**
- Consumes: rpi1 `kernel.img`, rpi2 `kernel7.img`, QEMU's `usb-mouse` and `usb-kbd` devices.
- Produces: documented smoke-test commands that exercise pTOS HID enumeration rather than merely registering the class drivers.

- [ ] **Step 1: Update rpi1 and rpi2 invocation examples**

Replace the rpi1 command with:

```sh
qemu-system-arm -M raspi1ap -bios kernel.img -device usb-mouse -device usb-kbd \
  -d guest_errors -serial stdio
```

Replace the rpi2 command with:

```sh
qemu-system-arm -M raspi2b -bios kernel7.img -device usb-mouse -device usb-kbd \
  -d guest_errors -serial stdio
```

Add an immediately adjacent note that these devices are required whenever
validating USB HID input; without them the class drivers register but no mouse
or keyboard is enumerated.

- [ ] **Step 2: Strengthen Raspberry Pi pass criteria**

Change the rpi1/rpi2 pass-signal bullets to require all of:

```text
- no guest_errors;
- the screen reaches the normal boot/desktop path; and
- serial traces confirm mouse and keyboard HID probes after attaching
  -device usb-mouse -device usb-kbd.
```

- [ ] **Step 3: Download CI images and run both HID-enabled QEMU smokes**

After pushing the implementation, identify the workflow run for the branch
and download its rpi artifacts:

```bash
gh run download <run-id> -D /tmp/ptos-ci-artifacts -p "ptos-rpi*"
```

Run rpi1:

```bash
qemu-system-arm -M raspi1ap \
  -bios /tmp/ptos-ci-artifacts/ptos-rpi1/kernel.img \
  -device usb-mouse -device usb-kbd -d guest_errors -serial stdio -display none
```

Run rpi2:

```bash
qemu-system-arm -M raspi2b \
  -bios /tmp/ptos-ci-artifacts/ptos-rpi2/kernel7.img \
  -device usb-mouse -device usb-kbd -d guest_errors -serial stdio -display none
```

On macOS, stop each emulator after the boot signal with a background PID plus
`sleep` and `kill`; do not rely on GNU `timeout`.

Expected: both images boot, enumerate the attached HID mouse and keyboard,
and emit no guest errors. Then test repeated short stationary clicks and rapid
keyboard input on real Raspberry Pi hardware.

- [ ] **Step 4: Commit smoke-test documentation**

```bash
git add .claude/skills/ptos-smoketest/SKILL.md
git commit -m "Document USB HID devices for Raspberry Pi smoke tests"
```

## Task 6: Final Integration Review

**Files:**
- Verify: `usb/usb_api.h`
- Verify: `usb/usb.h`
- Verify: `usb/usb.c`
- Verify: `usb/ucd_dwc2.c`
- Verify: `usb/udd_mouse.c`
- Verify: `usb/udd_keyboard.c`
- Verify: `bios/arch/arm/vectors.c`
- Verify: `.claude/skills/ptos-smoketest/SKILL.md`

**Interfaces:**
- Verifies the complete #177 contract: UDD submit/cancel lifecycle, DWC2 IRQ ownership, split transfer state handling, and no Timer-C USB polling.

- [ ] **Step 1: Confirm no legacy polling or legacy HID irq stubs remain**

Run:

```bash
rg "usb_(mouse|keyboard)_timerc|usb_(mouse|keyboard)_irq|irq_handle =" \
  usb bios/arch/arm/vectors.c
```

Expected: no mouse/keyboard Timer-C polling functions, no obsolete HID IRQ
stub assignments, and no USB HID call from `int_timerc()`.

- [ ] **Step 2: Run formatting and final builds**

Run:

```bash
make gitready
make rpi1_defconfig && make
make rpi2_defconfig && make
```

Expected: formatting checks pass and both images link.

- [ ] **Step 3: Inspect the final diff and status**

Run:

```bash
git diff master...HEAD --check
```

Expected: only the planned API, DWC2, HID UDD, Timer-C, skill, and planning
documentation changes appear.

- [ ] **Step 4: Push the completed implementation**

```bash
git push
```

Expected: PR #178 receives the complete async HID implementation and CI runs.
