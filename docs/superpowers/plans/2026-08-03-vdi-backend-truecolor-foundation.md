# VDI Backend and Truecolor Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `CONF_CHUNKY_PIXELS`'s compile-time forking of VDI drawing primitives with a runtime-dispatch backend table, and move Raspberry Pi from 8bpp packed-indexed to 16bpp packed-truecolor on top of it.

**Architecture:** A new `SCREEN_MODE_DESC` (width, height, pitch, bpp, layout, color model, truecolor pixel format) becomes what screen drivers report. `vdi_backend_select()` picks a `vdi_backend_ops` function-pointer table from a descriptor; the physical screen workstation (`virt_work` in `vdi/vdi_control.c`) stores both the descriptor and the selected table. Four primitives — pixel address calculation, pixel read, pixel write, rectangle fill — get converted from `#if CONF_CHUNKY_PIXELS` forks into dispatch through that table. Everything else (raster ops, text effects, mouse cursor, full palette) is untouched this round.

**Tech Stack:** C90 with GNU extensions (`-std=gnu90`), Kconfig/Kbuild-style `build.mk`, pTOS fixed-width types (`WORD`/`UWORD`/`ULONG`/`UBYTE`/`BOOL`). No host-side test framework exists or is being added for this codebase — pTOS is freestanding with no libc and no host runtime, and a prior attempt at this exact feature was aborted specifically because it linked kernel source files against the host compiler with a hand-faked `autoconf.h`. Verification here is exclusively: cross-compiled builds across the configuration matrix, `make gitready`, and booting the real image under QEMU while reading `KDEBUG` serial output (per `CLAUDE.md`'s smoke-test convention). Raspberry Pi is the only machine with a QEMU target that has actual video output (`qemu-system-arm -M raspi2`); `virt-arm`/`virt-m68k` boot headless with no framebuffer, so they're used only as no-crash/no-regression checks for the code paths that run on every machine, not for visually verifying drawing.

## Global Constraints

- Design doc: `docs/superpowers/specs/2026-08-03-vdi-backend-truecolor-design.md`. Follow it; this plan only fills in the exact code.
- C90 with GNU extensions: declarations at the top of a block. Match the surrounding file's `/* */` comment style.
- 4-space indentation, never a tab, in `.c`/`.h`. Run `make gitready` before every commit.
- Use `portab.h` types (`WORD`, `LONG`, `UBYTE`, `UWORD`, `ULONG`, `BOOL`) — never bare `int`/`long` in new code, except loop counters in code mechanically copied from an existing function that already uses `int` (leave those untouched to keep the copy exact).
- No `NULL` backend op is ever a "fall back to planar" signal — it means "not implemented," and this slice does not add any call site that treats it otherwise.
- No host-compiled test programs, no fabricated `autoconf.h`, no linking kernel `.c` files against anything but the real cross toolchain.
- `bios/` code must never `#include` anything from `vdi/` — layering stays one-directional (VDI depends on BIOS screen state, not the reverse). `SCREEN_MODE_DESC` therefore lives in `include/` (visible everywhere) rather than under `vdi/`.
- Cross-directory includes in this tree use explicit relative paths (e.g. `vdi/vdi_misc.c` already has `#include "../bios/lineavars.h"`) because only `include/`, `include/<arch>`, and `obj/` are global include directories (see `Makefile`'s `include_dirs`). Match this — don't invent a new global include directory.
- The physical/screen workstation is the file-static `virt_work` in `vdi/vdi_control.c`, populated once in `vdi_v_opnwk()`. There is currently exactly one screen; nothing in this plan adds multi-screen support, so all new "current screen" accessors resolve to `virt_work` directly rather than taking a `Vwk *` parameter — this matches how `get_start_addr()` and friends already work today (no `Vwk *` parameter, implicit global state).

---

## Task 1: Screen mode descriptor type

**Files:**
- Create: `include/screen_mode.h`
- Create: `bios/screen_mode.c`
- Modify: `bios/build.mk` — add `screen_mode.o` to `obj-y`

**Interfaces:**
- Produces (used by Tasks 2-5):
  ```c
  #define SCREEN_LAYOUT_PLANAR   0
  #define SCREEN_LAYOUT_PACKED   1
  #define SCREEN_COLOR_INDEXED   0
  #define SCREEN_COLOR_TRUECOLOR 1
  #define SCREEN_PIXEL_NONE      0
  #define SCREEN_PIXEL_RGB565    1

  typedef struct {
      UWORD width;
      UWORD height;
      ULONG pitch;
      UWORD bits_per_pixel;
      UBYTE layout;        /* SCREEN_LAYOUT_* */
      UBYTE color_model;   /* SCREEN_COLOR_* */
      UBYTE pixel_format;  /* SCREEN_PIXEL_*, meaningful only when color_model == SCREEN_COLOR_TRUECOLOR */
  } SCREEN_MODE_DESC;

  BOOL screen_mode_desc_valid(const SCREEN_MODE_DESC *desc);
  ```

- [ ] **Step 1: Write `include/screen_mode.h`**

```c
/*
 * screen_mode.h - describes a screen's pixel layout and color model
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef SCREEN_MODE_H
#define SCREEN_MODE_H

#include "portab.h"

#define SCREEN_LAYOUT_PLANAR   0   /* interleaved Atari-style bitplanes */
#define SCREEN_LAYOUT_PACKED   1   /* packed pixels, one pixel per unit */

#define SCREEN_COLOR_INDEXED   0   /* pixel value is a CLUT index */
#define SCREEN_COLOR_TRUECOLOR 1   /* pixel value directly encodes color */

#define SCREEN_PIXEL_NONE      0   /* not applicable: indexed color */
#define SCREEN_PIXEL_RGB565    1   /* 5 red / 6 green / 5 blue bits, packed into a UWORD */

typedef struct {
    UWORD width;          /* visible width, in pixels */
    UWORD height;         /* visible height, in pixels */
    ULONG pitch;           /* bytes per scan line */
    UWORD bits_per_pixel;
    UBYTE layout;          /* SCREEN_LAYOUT_* */
    UBYTE color_model;     /* SCREEN_COLOR_* */
    UBYTE pixel_format;    /* SCREEN_PIXEL_*, meaningful only for SCREEN_COLOR_TRUECOLOR */
} SCREEN_MODE_DESC;

/*
 * Rejects a descriptor whose fields can't describe a real framebuffer:
 * zero pitch, a pitch too small to hold one scan line at the stated
 * width/depth, or an unrecognized layout/color-model/pixel-format
 * combination.
 */
BOOL screen_mode_desc_valid(const SCREEN_MODE_DESC *desc);

#endif /* SCREEN_MODE_H */
```

- [ ] **Step 2: Write `bios/screen_mode.c`**

```c
/*
 * screen_mode.c - screen mode descriptor validation
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "screen_mode.h"

BOOL screen_mode_desc_valid(const SCREEN_MODE_DESC *desc)
{
    ULONG min_pitch;

    if (desc->pitch == 0)
        return FALSE;

    min_pitch = ((ULONG)desc->width * desc->bits_per_pixel + 7) / 8;
    if (desc->pitch < min_pitch)
        return FALSE;

    switch (desc->layout) {
    case SCREEN_LAYOUT_PLANAR:
    case SCREEN_LAYOUT_PACKED:
        break;
    default:
        return FALSE;
    }

    switch (desc->color_model) {
    case SCREEN_COLOR_INDEXED:
        break;
    case SCREEN_COLOR_TRUECOLOR:
        switch (desc->pixel_format) {
        case SCREEN_PIXEL_RGB565:
            break;
        default:
            return FALSE;
        }
        break;
    default:
        return FALSE;
    }

    return TRUE;
}
```

- [ ] **Step 3: Wire it into the build**

Modify `bios/build.mk`. In the big `obj-y +=` list (the one starting `memory.o processor.o ...`), add `screen_mode.o` next to `screen.o`:

```make
obj-y += memory.o processor.o intmask.o vectors.o bios.o xbios.o acsi.o biosmem.o \
	 blkdev.o chardev.o clock.o conout.o cookie.o country.o disk.o \
	 dma.o dmasound.o floppy.o font.o ide.o ikbd.o initinfo.o kprint.o \
	 lineainit.o machine.o mfp.o midi.o mouse.o nvram.o panicasm.o \
	 parport.o screen.o screen_mode.o serport.o sound.o videl.o vt52.o xhdi.o delay.o \
	 sd.o memory2.o bootparams.o scsi.o
```

- [ ] **Step 4: Verify it builds everywhere**

Run:
```sh
make rpi2_defconfig && make
make virt-arm_defconfig && make
```

Expected: both build with no new warnings or errors. `screen_mode.o` is unused so far (nothing calls `screen_mode_desc_valid()` yet) — that's fine, it's a non-`static` function, so `-Wall`/`-Wextra` won't flag it as unused, unlike an unused `static`.

- [ ] **Step 5: `make gitready` and commit**

```sh
make gitready
git add include/screen_mode.h bios/screen_mode.c bios/build.mk
git commit -m "Add screen mode descriptor type"
git push
```

---

## Task 2: Wire descriptor reporting into every screen driver

**Files:**
- Modify: `bios/screen.h` — declare `screen_get_current_mode_desc()`
- Modify: `bios/screen.c` — implement it for the Atari/Amiga (planar) and Raspberry Pi paths
- Modify: `bios/raspi_screen.h` — declare `raspi_get_current_mode_desc()`
- Modify: `bios/raspi_screen.c` — implement it, still reporting the *current* 8bpp packed-indexed mode (depth is not changed in this task)
- Modify: `bios/lineainit.c` — source `v_planes`/`v_lin_wr`/`BYTES_LIN`/`V_REZ_HZ`/`V_REZ_VT` from the descriptor instead of the old per-machine `screen_get_current_mode_info()` + hand-rolled pitch formula

**Interfaces:**
- Consumes: `SCREEN_MODE_DESC` and its `SCREEN_LAYOUT_*`/`SCREEN_COLOR_*`/`SCREEN_PIXEL_*` constants from Task 1.
- Produces (used by Task 3 via `vdi_v_opnwk()`, and by Task 5):
  ```c
  void screen_get_current_mode_desc(SCREEN_MODE_DESC *desc);   /* bios/screen.h */
  void raspi_get_current_mode_desc(SCREEN_MODE_DESC *desc);    /* bios/raspi_screen.h, MACHINE_RPI only */
  ```

- [ ] **Step 1: Declare the new call in `bios/screen.h`**

Add `#include "screen_mode.h"` near the top (next to the existing `#include "portab.h"` / `#include "tosvars.h"`), and add the declaration right after the existing one:

```c
void screen_get_current_mode_info(UWORD *planes, UWORD *hz_rez, UWORD *vt_rez);
void screen_get_current_mode_desc(SCREEN_MODE_DESC *desc);
```

- [ ] **Step 2: Implement it in `bios/screen.c`**

Add this right after the existing `screen_get_current_mode_info()` function (which stays untouched — it's still used elsewhere):

```c
/*
 * Fills in a descriptor for a planar (interleaved-bitplane) mode from
 * the legacy planes/resolution triple. v_lin_wr's classic formula
 * (V_REZ_HZ/8*planes) is only correct for planar layouts -- that's
 * exactly why it can't be reused as-is for packed depths, which is why
 * this helper is planar-only.
 */
static void planar_mode_desc(SCREEN_MODE_DESC *desc, UWORD planes, UWORD hz_rez, UWORD vt_rez)
{
    desc->width = hz_rez;
    desc->height = vt_rez;
    desc->bits_per_pixel = planes;
    desc->layout = SCREEN_LAYOUT_PLANAR;
    desc->color_model = SCREEN_COLOR_INDEXED;
    desc->pixel_format = SCREEN_PIXEL_NONE;
    desc->pitch = (ULONG)hz_rez / 8 * planes;
}

void screen_get_current_mode_desc(SCREEN_MODE_DESC *desc)
{
    UWORD planes, hz_rez, vt_rez;

    MAYBE_UNUSED(atari_get_current_mode_info);

#if defined(MACHINE_RPI)
    raspi_get_current_mode_desc(desc);
#elif defined(MACHINE_AMIGA)
    amiga_get_current_mode_info(&planes, &hz_rez, &vt_rez);
    planar_mode_desc(desc, planes, hz_rez, vt_rez);
#else
    atari_get_current_mode_info(&planes, &hz_rez, &vt_rez);
    planar_mode_desc(desc, planes, hz_rez, vt_rez);
#endif
}
```

This mirrors `screen_get_current_mode_info()`'s own `#ifdef MACHINE_AMIGA` / `#elif defined(MACHINE_RPI)` / `#else` dispatch immediately above it, just funneling the non-RPi cases through the new `planar_mode_desc()` helper instead of returning three separate out-parameters.

- [ ] **Step 3: Declare `raspi_get_current_mode_desc()` in `bios/raspi_screen.h`**

Add next to the existing `raspi_get_current_mode_info()` declaration:

```c
void raspi_get_current_mode_info(UWORD *planes, UWORD *hz_rez, UWORD *vt_rez);
void raspi_get_current_mode_desc(SCREEN_MODE_DESC *desc);
```

- [ ] **Step 4: Implement it in `bios/raspi_screen.c`**

Add `#include "screen_mode.h"` to the top `#include` block, then add this right after the existing `raspi_get_current_mode_info()`:

```c
void raspi_get_current_mode_desc(SCREEN_MODE_DESC *desc)
{
    desc->width = raspi_screen_width;
    desc->height = raspi_screen_height;
    desc->pitch = raspi_screen_width_in_bytes;
    desc->bits_per_pixel = 8;                  /* Task 5 raises this to 16 */
    desc->layout = SCREEN_LAYOUT_PACKED;
    desc->color_model = SCREEN_COLOR_INDEXED;  /* Task 5 changes this to TRUECOLOR */
    desc->pixel_format = SCREEN_PIXEL_NONE;
}
```

Note: `raspi_screen_width_in_bytes` is already the pitch the VideoCore firmware actually returned (`init_tags.get_pitch.value` in `raspi_screen_init()`), not a hand-computed value — using it here is more accurate than the old `V_REZ_HZ/8*planes` formula would have been if firmware padding ever made the real pitch wider than the raw width.

- [ ] **Step 5: Source `linea_init()` from the descriptor**

Modify `bios/lineainit.c`. Add `#include "screen_mode.h"` to the top `#include` block. Replace the body of `linea_init()`:

```c
void linea_init(void)
{
    SCREEN_MODE_DESC desc;

    screen_get_current_mode_desc(&desc);

    linea_vars.v_planes = desc.bits_per_pixel;
    linea_vars.V_REZ_HZ = desc.width;
    linea_vars.V_REZ_VT = desc.height;
    linea_vars.v_lin_wr = desc.pitch;         /* bytes per line */
    linea_vars.BYTES_LIN = linea_vars.v_lin_wr;       /* I think BYTES_LIN = v_lin_wr (PES) */

    mcs_ptr = (linea_vars.v_planes <= 4) ? (MCS *)&linea_vars.mouse_cursor_save : &ext_mouse_cursor_save;

    /*
     * this is a convenient place to update the workstation xres/yres which
     * may have been changed by a Setscreen()
     */
    linea_vars.DEV_TAB[0] = linea_vars.V_REZ_HZ - 1;
    linea_vars.DEV_TAB[1] = linea_vars.V_REZ_VT - 1;

#if DBG_LINEA
    kprintf("planes: %d\n", linea_vars.v_planes);
    kprintf("lin_wr: %d\n", linea_vars.v_lin_wr);
    kprintf("hz_rez: %d\n", linea_vars.V_REZ_HZ);
    kprintf("vt_rez: %d\n", linea_vars.V_REZ_VT);
#endif
}
```

(Only the first block changed — the `#if DBG_LINEA` debug print and everything below it stays exactly as it was.)

For every machine currently in the tree, this produces the same `v_lin_wr` value as before: for planar machines the formula is identical (`planar_mode_desc()` uses the exact same `V_REZ_HZ/8*planes` expression `screen_get_current_mode_info()`'s callers used to compute by hand), and for Raspberry Pi the previous code never computed `v_lin_wr` from a formula at all — the old `linea_init()` always used `screen_get_current_mode_info()`, which never set `v_lin_wr`, so there is no prior "packed pitch formula" being replaced for RPi in this codebase; this task is what gives RPi a correct `v_lin_wr` sourced from real hardware for the first time.

- [ ] **Step 6: Verify behavior is unchanged, build everywhere**

Temporarily flip `#define DBG_LINEA 0` to `#define DBG_LINEA 1` at the top of `bios/lineainit.c`, then:

```sh
make rpi2_defconfig && make
timeout 8 qemu-system-arm -M raspi2 -bios kernel7.img -d guest_errors -serial stdio
```

Expected: `KDEBUG`/`kprintf` output includes `planes: 8`, `lin_wr:` matching the framebuffer's actual byte width (should equal `hz_rez` for an unpadded 8bpp mode — check it's not producing a garbled/zero value), `hz_rez: 1280`, `vt_rez: 720` (the fixed physical dimensions `raspi_screen_init()` requests). No `guest_errors` output, screen displays exactly as before (still 8bpp indexed — nothing about drawing changed yet).

Then also build the machines with no QEMU display target, to confirm the shared `linea_init()`/`planar_mode_desc()` path doesn't crash headless:

```sh
make virt-arm_defconfig && make
timeout 8 qemu-system-arm -M virt,highmem=off -cpu cortex-a7 -m 128 -kernel virt-arm.elf -d guest_errors -display none -serial stdio
```

Expected: boots as far as it did before this change (BIOS runs, AES launch attempted) with no new `guest_errors` output. `virt-arm` has no real video hardware, so its descriptor values are not meaningful, but the code path must not crash.

Also build one Atari/m68k configuration if a cross toolchain is available locally (e.g. `make falcon_defconfig && make` or whichever `m68k-atari-mint-*`/`m68k-elf-*` config matches the installed toolchain — see `doc/install.txt`) — this is the only check that exercises `planar_mode_desc()` on hardware descriptors that aren't just a stand-in.

Revert `DBG_LINEA` back to `0` before committing.

- [ ] **Step 7: `make gitready` and commit**

```sh
make gitready
git add bios/screen.h bios/screen.c bios/raspi_screen.h bios/raspi_screen.c bios/lineainit.c
git commit -m "Report screen mode descriptors from every screen driver"
git push
```

---

## Task 3: VDI backend interface + planar backend

**Files:**
- Modify: `vdi/vdi_defs.h` — forward-declare `struct vdi_backend_ops`, add `mode`/`backend` fields to `struct Vwk_`, declare the four extracted `planar_*` functions
- Create: `vdi/vdi_backend.h` — the ops-table type, `vdi_backend_select()`, `vdi_screen_backend()`, extern declarations for both ops tables
- Create: `vdi/vdi_backend.c` — `vdi_backend_select()`
- Create: `vdi/vdi_backend_planar.c` — `const vdi_backend_ops planar_backend_ops`
- Modify: `vdi/vdi_misc.c` — extract `get_start_addr()`'s existing non-chunky body into a new `planar_get_start_addr()`, alongside (not replacing) the original
- Modify: `vdi/vdi_fill.c` — extract `pixelread()`'s non-chunky body (plus its `get_color()` helper) into `planar_get_pixel()`, and `put_pix()`'s non-chunky body into `planar_put_pixel()`, alongside the originals
- Modify: `vdi/vdi_line.c` — extract `draw_rect_common()`'s non-chunky (`#else`) body into `planar_fill_rect()`, alongside the original
- Modify: `vdi/vdi_control.c` — populate `virt_work.mode`/`virt_work.backend` in `vdi_v_opnwk()`, add `vdi_screen_backend()`
- Modify: `vdi/Kconfig` — add `CONF_WITH_VDI_TRUECOLOR` (default `n` — Task 4 makes it real)
- Modify: `vdi/build.mk` — add `vdi_backend.o vdi_backend_planar.o` to `obj-y`

**Interfaces:**
- Consumes: `SCREEN_MODE_DESC` (Task 1), `screen_get_current_mode_desc()` (Task 2).
- Produces (used by Task 4 and Task 5):
  ```c
  typedef struct vdi_backend_ops {
      BOOL (*open)(Vwk *vwk);
      void (*close)(Vwk *vwk);
      UWORD *(*get_start_addr)(WORD x, WORD y);
      UWORD (*get_pixel)(WORD x, WORD y);
      void (*put_pixel)(WORD x, WORD y, UWORD color);
      void (*fill_rect)(const VwkAttrib *attr, const Rect *rect);
  } vdi_backend_ops;

  const vdi_backend_ops *vdi_backend_select(const SCREEN_MODE_DESC *mode);
  const vdi_backend_ops *vdi_screen_backend(void);

  extern const vdi_backend_ops planar_backend_ops;
  ```
- Note on scope: this table intentionally covers only the four primitives this slice converts (open/close plus the pixel/fill quartet). Line/vertical-line, seed-fill scanning, raster copy, text blit, mouse cursor, and palette hooks are deliberately *not* added as `NULL` slots here — they get added to the table in the follow-up slices that actually implement them (issue #35 parts 2b/5), rather than guessed at now.

### Important: temporary duplication in this task

The non-chunky bodies of `get_start_addr()`, `pixelread()`, `put_pix()`, and `draw_rect_common()` cannot simply be *moved* into new files: `draw_rect_common()`'s non-chunky path calls `blit_hline()`/`blit_rect_common()`, which are `static` functions private to `vdi/vdi_line.c`, and `pixelread()`'s non-chunky path calls `get_color()`, `static` and private to `vdi/vdi_fill.c`. Relocating the bodies without those helpers would not compile.

So this task extracts each body into a new, non-`static`, identically-behaving function **in the same file** as today (so it keeps access to whatever file-local helpers it needs), under a new `planar_*` name, while leaving the original `#if CONF_CHUNKY_PIXELS` / `#else` function completely untouched. For one task, the non-chunky logic exists in two places at once — deliberately, so nothing that depends on it today can regress. Task 5 deletes the original `#if`/`#else` bodies and replaces each function with a one-line dispatch, at which point the duplication disappears and `planar_*`/the new truecolor equivalents become the only implementations.

- [ ] **Step 1: Add the `mode`/`backend` fields to `Vwk`**

Modify `vdi/vdi_defs.h`. Add near the top, after the existing includes (there should already be an include of `../bios/lineavars.h` or similar bios headers — add this alongside them):

```c
#include "../bios/screen_mode.h"

struct vdi_backend_ops;   /* forward declaration -- full definition in vdi_backend.h */
```

Then add two fields at the end of `struct Vwk_`, right after the existing `bez_qual`:

```c
    WORD bez_qual;               /* actual quality for bezier curves */
    SCREEN_MODE_DESC mode;       /* backend mode descriptor for this workstation's screen */
    const struct vdi_backend_ops *backend; /* dispatch table selected for `mode`; NULL if none matched */
};
```

Also add these four prototypes near the existing `draw_rect_common()` declaration (`void draw_rect_common(const VwkAttrib * attr, const Rect * rect);`):

```c
UWORD *planar_get_start_addr(WORD x, WORD y);
UWORD planar_get_pixel(WORD x, WORD y);
void planar_put_pixel(WORD x, WORD y, UWORD color);
void planar_fill_rect(const VwkAttrib *attr, const Rect *rect);
```

- [ ] **Step 2: Extract `planar_get_start_addr()` in `vdi/vdi_misc.c`**

Add this new function right after the existing `get_start_addr()` (which is untouched):

```c
UWORD *planar_get_start_addr(WORD x, WORD y)
{
    UBYTE *addr;

    addr = v_bas_ad;                    /* start of screen */
    addr += (x&0xfff0)>>shift_offset[linea_vars.v_planes]; /* add x coordinate part of addr */
    addr += (LONG)y * linea_vars.v_lin_wr;         /* add y coordinate part of addr */
    return (UWORD*)addr;
}
```

- [ ] **Step 3: Extract `planar_get_pixel()` in `vdi/vdi_fill.c`**

Add this new function right after the existing `pixelread()` (which is untouched, including its own private `get_color()` right above it):

```c
UWORD planar_get_pixel(WORD x, WORD y)
{
    UWORD *addr;
    UWORD mask;

    addr = planar_get_start_addr(x, y);
    addr += linea_vars.v_planes;            /* start at highest-order bit_plane */
    mask = 0x8000 >> (x&0xf);               /* initial bit position in WORD */

    return get_color(mask, addr);           /* return the composed color value */
}
```

(`get_color()` is still `static` and private to this file, defined just above `pixelread()`; `planar_get_pixel()` reuses it exactly as `pixelread()`'s own non-chunky branch does.)

- [ ] **Step 4: Extract `planar_put_pixel()` in `vdi/vdi_fill.c`**

Add this new function right after `planar_get_pixel()`:

```c
void planar_put_pixel(WORD x, WORD y, UWORD color)
{
    UWORD *addr;
    UWORD mask;
    int plane;

    addr = planar_get_start_addr(x, y);
    mask = 0x8000 >> (x&0xf);   /* initial bit position in WORD */

    for (plane = linea_vars.v_planes-1; plane >= 0; plane-- ) {
        color = color >> 1| color << 15;        /* rotate color bits */
        if (color&0x8000)
            *addr++ |= mask;
        else
            *addr++ &= ~mask;
    }
}
```

- [ ] **Step 5: Extract `planar_fill_rect()` in `vdi/vdi_line.c`**

Add this new function right after the existing `draw_rect_common()` (which is untouched). It is `draw_rect_common()`'s current `#else` branch (lines 401-573 in the file as of this plan — everything between `#else` and the final `#endif` of the outer `#if CONF_CHUNKY_PIXELS`), copied verbatim with the surrounding `#if CONF_WITH_BLITTER` guards kept exactly as they are, given its own signature:

```c
void planar_fill_rect(const VwkAttrib *attr, const Rect *rect)
{
    UWORD leftmask, rightmask, *addr;
    const UWORD patmsk = attr->patmsk;
    const int yinc = (linea_vars.v_lin_wr>>1) - linea_vars.v_planes;
    int width, centre, y;
#if CONF_WITH_BLITTER
    BLITPARM b;
#endif

    leftmask = 0xffff >> (rect->x1 & 0x0f);
    rightmask = 0xffff << (15 - (rect->x2 & 0x0f));
    width = (rect->x2 >> 4) - (rect->x1 >> 4) + 1;
    if (width == 1) {           /* i.e. all bits within 1 WORD */
        leftmask &= rightmask;  /* so combine masks */
        rightmask = 0;
    }
    addr = get_start_addr(rect->x1,rect->y1);   /* init address ptr */

#if CONF_WITH_BLITTER
    if (blitter_is_enabled)
    {
        b.leftmask = leftmask;
        b.rightmask = rightmask;
        b.width = width;
        b.addr = addr;

        /*
         * special handling for common horizontal line case
         */
        if (rect->y1 == rect->y2)
        {
            if (blit_hline(attr, rect, &b))         /* if it ran ok, */
                return;                             /* we're done    */
        }
        else
        {
            if (blit_rect_common(attr, rect, &b))   /* if it ran ok, */
                return;                             /* we're done    */
        }
    }
#endif

    centre = width - 2;

    switch(attr->wrt_mode) {
    case 3:                 /* erase (reverse transparent) mode */
        for (y = rect->y1; y <= rect->y2; y++, addr += yinc) {
            int patind = patmsk & y;   /* starting pattern */
            int plane;
            UWORD color;

            for (plane = 0, color = attr->color; plane < linea_vars.v_planes; plane++, color>>=1, addr++) {
                UWORD *work = addr;
                UWORD pattern = ~attr->patptr[patind];
                int n;

                if (color & 0x0001) {
                    *work |= pattern & leftmask;    /* left section */
                    work += linea_vars.v_planes;
                    for (n = 0; n < centre; n++) {  /* centre section */
                        *work |= pattern;
                        work += linea_vars.v_planes;
                    }
                    if (rightmask) {                /* right section */
                        *work |= pattern & rightmask;
                    }
                } else {
                    *work &= ~(pattern & leftmask); /* left section */
                    work += linea_vars.v_planes;
                    for (n = 0; n < centre; n++) {  /* centre section */
                        *work &= ~pattern;
                        work += linea_vars.v_planes;
                    }
                    if (rightmask) {                /* right section */
                        *work &= ~(pattern & rightmask);
                    }
                }
                if (attr->multifill)
                    patind += 16;                   /* advance pattern data */
            }
        }
        break;
    case 2:                 /* xor mode */
        for (y = rect->y1; y <= rect->y2; y++, addr += yinc) {
            int patind = patmsk & y;   /* starting pattern */
            int plane;
            UWORD color;

            for (plane = 0, color = attr->color; plane < linea_vars.v_planes; plane++, color>>=1, addr++) {
                UWORD *work = addr;
                UWORD pattern = attr->patptr[patind];
                int n;

                *work ^= pattern & leftmask;        /* left section */
                work += linea_vars.v_planes;
                for (n = 0; n < centre; n++) {      /* centre section */
                    *work ^= pattern;
                    work += linea_vars.v_planes;
                }
                if (rightmask) {                    /* right section */
                    *work ^= pattern & rightmask;
                }
                if (attr->multifill)
                    patind += 16;                   /* advance pattern data */
            }
        }
        break;
    case 1:                 /* transparent mode */
        for (y = rect->y1; y <= rect->y2; y++, addr += yinc) {
            int patind = patmsk & y;   /* starting pattern */
            int plane;
            UWORD color;

            for (plane = 0, color = attr->color; plane < linea_vars.v_planes; plane++, color>>=1, addr++) {
                UWORD *work = addr;
                UWORD pattern = attr->patptr[patind];
                int n;

                if (color & 0x0001) {
                    *work |= pattern & leftmask;    /* left section */
                    work += linea_vars.v_planes;
                    for (n = 0; n < centre; n++) {  /* centre section */
                        *work |= pattern;
                        work += linea_vars.v_planes;
                    }
                    if (rightmask) {                /* right section */
                        *work |= pattern & rightmask;
                    }
                } else {
                    *work &= ~(pattern & leftmask); /* left section */
                    work += linea_vars.v_planes;
                    for (n = 0; n < centre; n++) {  /* centre section */
                        *work &= ~pattern;
                        work += linea_vars.v_planes;
                    }
                    if (rightmask) {                /* right section */
                        *work &= ~(pattern & rightmask);
                    }
                }
                if (attr->multifill)
                    patind += 16;                   /* advance pattern data */
            }
        }
        break;
    default:                /* replace mode */
        for (y = rect->y1; y <= rect->y2; y++, addr += yinc) {
            int patind = patmsk & y;   /* starting pattern */
            int plane;
            UWORD color;

            for (plane = 0, color = attr->color; plane < linea_vars.v_planes; plane++, color>>=1, addr++) {
                UWORD data, *work = addr;
                UWORD pattern = (color & 0x0001) ? attr->patptr[patind] : 0x0000;
                int n;

                data = *work & ~leftmask;           /* left section */
                data |= pattern & leftmask;
                *work = data;
                work += linea_vars.v_planes;
                for (n = 0; n < centre; n++) {      /* centre section */
                    *work = pattern;
                    work += linea_vars.v_planes;
                }
                if (rightmask) {                    /* right section */
                    data = *work & ~rightmask;
                    data |= pattern & rightmask;
                    *work = data;
                }
                if (attr->multifill)
                    patind += 16;                   /* advance pattern data */
            }
        }
        break;
    }
}
```

- [ ] **Step 6: Write `vdi/vdi_backend.h`**

```c
/*
 * vdi_backend.h - runtime-dispatch VDI drawing backend
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef VDI_BACKEND_H
#define VDI_BACKEND_H

#include "portab.h"
#include "../bios/screen_mode.h"
#include "vdi_defs.h"

/*
 * A NULL slot means "this backend does not implement this primitive" --
 * never "fall back to another backend." A backend is only ever selected
 * for descriptors whose layout/color-model/bpp combination it actually
 * supports (see vdi_backend_select()), so an incompatible fallback can
 * never happen by construction.
 *
 * This table currently only covers the primitives this slice converts.
 * Follow-up slices (line/vline, raster copy, text blit, mouse cursor,
 * full palette -- issue #35 parts 2b/5) add their own slots when they
 * actually implement them.
 */
typedef struct vdi_backend_ops {
    BOOL (*open)(Vwk *vwk);
    void (*close)(Vwk *vwk);

    UWORD *(*get_start_addr)(WORD x, WORD y);
    UWORD (*get_pixel)(WORD x, WORD y);
    void (*put_pixel)(WORD x, WORD y, UWORD color);
    void (*fill_rect)(const VwkAttrib *attr, const Rect *rect);
} vdi_backend_ops;

/*
 * Picks a backend ops table for a mode descriptor, or NULL if no backend
 * supports that layout/color-model/pixel-format combination.
 */
const vdi_backend_ops *vdi_backend_select(const SCREEN_MODE_DESC *mode);

/*
 * The backend ops table for the current screen workstation, or NULL if
 * none was selected (e.g. the screen's mode descriptor doesn't match
 * any backend yet). There is currently exactly one screen.
 */
const vdi_backend_ops *vdi_screen_backend(void);

extern const vdi_backend_ops planar_backend_ops;
#if CONF_WITH_VDI_TRUECOLOR
extern const vdi_backend_ops packed_truecolor_backend_ops;
#endif

#endif /* VDI_BACKEND_H */
```

- [ ] **Step 7: Write `vdi/vdi_backend.c`**

```c
/*
 * vdi_backend.c - VDI drawing backend selection
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "vdi_defs.h"
#include "vdi_backend.h"

const vdi_backend_ops *vdi_backend_select(const SCREEN_MODE_DESC *mode)
{
    if (!screen_mode_desc_valid(mode))
        return NULL;

    if (mode->layout == SCREEN_LAYOUT_PLANAR && mode->color_model == SCREEN_COLOR_INDEXED)
        return &planar_backend_ops;

#if CONF_WITH_VDI_TRUECOLOR
    if (mode->layout == SCREEN_LAYOUT_PACKED
        && mode->color_model == SCREEN_COLOR_TRUECOLOR
        && mode->pixel_format == SCREEN_PIXEL_RGB565)
        return &packed_truecolor_backend_ops;
#endif

    return NULL;
}
```

- [ ] **Step 8: Write `vdi/vdi_backend_planar.c`**

```c
/*
 * vdi_backend_planar.c - planar (interleaved-bitplane) VDI backend
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "vdi_defs.h"
#include "vdi_backend.h"

static BOOL planar_open(Vwk *vwk)
{
    (void)vwk;
    return TRUE;
}

static void planar_close(Vwk *vwk)
{
    (void)vwk;
}

const vdi_backend_ops planar_backend_ops = {
    planar_open,
    planar_close,
    planar_get_start_addr,
    planar_get_pixel,
    planar_put_pixel,
    planar_fill_rect,
};
```

- [ ] **Step 9: Wire the two new files into `vdi/build.mk`**

```make
obj-y += vdi_entry.o vdi_bezier.o vdi_col.o vdi_control.o vdi_esc.o \
	 vdi_fill.o vdi_gdp.o vdi_input.o vdi_line.o vdi_main.o \
	 vdi_marker.o vdi_misc.o vdi_mouse.o vdi_raster.o vdi_text.o \
	 vdi_textblit.o vdi_backend.o vdi_backend_planar.o
```

- [ ] **Step 10: Add the `CONF_WITH_VDI_TRUECOLOR` Kconfig option**

Modify `vdi/Kconfig`, adding a new option right after `CONF_CHUNKY_PIXELS`:

```
config CONF_WITH_VDI_TRUECOLOR
	bool "Packed truecolor VDI backend"
	default n
	help
	  Build the packed-truecolor VDI backend (vdi_backend_truecolor.o).
	  Not yet functional on its own -- see CONF_CHUNKY_PIXELS and issue
	  #35. Left off by default until a screen driver actually reports a
	  truecolor descriptor.
```

(Default stays `n` in this task: `vdi_backend_truecolor.c` doesn't exist yet, so turning this on would try to link `packed_truecolor_backend_ops` and fail. Task 4 both writes the file and changes the default.)

- [ ] **Step 11: Populate `mode`/`backend` in `vdi_v_opnwk()`**

Modify `vdi/vdi_control.c`. Add `#include "vdi_backend.h"` and `#include "kprint.h"` to the top `#include` block (there's already a commented-out `/* #include "kprint.h" */` there — replace it with a real, uncommented include).

In `vdi_v_opnwk()`, right after the existing `vwk->next_work = NULL;` line, add:

```c
    vwk = &virt_work;
    CONTRL->handle = vwk->handle = 1;
    vwk->next_work = NULL;

    screen_get_current_mode_desc(&vwk->mode);
    vwk->backend = vdi_backend_select(&vwk->mode);
    KDEBUG(("vdi_v_opnwk: mode layout=%d color_model=%d bpp=%d backend=%s\n",
            vwk->mode.layout, vwk->mode.color_model, vwk->mode.bits_per_pixel,
            vwk->backend ? "selected" : "none"));
```

`vwk->backend` legitimately stays `NULL` here for Raspberry Pi through Task 4 — its descriptor still reports 8bpp packed-indexed at this point in the plan, and this table deliberately has no backend for that combination (packed-indexed 8bpp is being retired, not preserved, per the design's non-goals). Nothing dereferences `vwk->backend` yet, so this is safe: it's pure plumbing and a trace line, with zero effect on drawing until Task 5.

Add `vdi_screen_backend()` at the end of the file:

```c
const vdi_backend_ops *vdi_screen_backend(void)
{
    return virt_work.backend;
}
```

- [ ] **Step 12: Verify it builds and boots everywhere, with the expected trace**

```sh
make rpi2_defconfig && make
timeout 8 qemu-system-arm -M raspi2 -bios kernel7.img -d guest_errors -serial stdio
```

Expected: `vdi_v_opnwk: mode layout=1 color_model=0 bpp=8 backend=none` in the serial log (layout `1` = `SCREEN_LAYOUT_PACKED`, color_model `0` = `SCREEN_COLOR_INDEXED` — matches Task 2's still-8bpp-indexed RPi descriptor). No `guest_errors`. Screen still draws exactly as before — nothing that runs today calls through `vwk->backend`.

```sh
make virt-arm_defconfig && make
timeout 8 qemu-system-arm -M virt,highmem=off -cpu cortex-a7 -m 128 -kernel virt-arm.elf -d guest_errors -display none -serial stdio
```

Expected: `vdi_v_opnwk: mode layout=1 color_model=0 backend=none` also appears (or `backend=selected` if `planar_mode_desc()`'s stand-in values happen to validate — either is fine, the point is no crash), boots as far as before.

Build one Atari/m68k configuration if available locally; expect a clean build (no QEMU display target to boot-test against, per the Tech Stack note).

- [ ] **Step 13: `make gitready` and commit**

```sh
make gitready
git add vdi/vdi_defs.h vdi/vdi_backend.h vdi/vdi_backend.c vdi/vdi_backend_planar.c \
    vdi/vdi_misc.c vdi/vdi_fill.c vdi/vdi_line.c vdi/vdi_control.c vdi/Kconfig vdi/build.mk
git commit -m "Add VDI backend dispatch interface and planar backend"
git push
```

---

## Task 4: Packed truecolor backend

**Files:**
- Create: `vdi/vdi_backend_truecolor.c`
- Modify: `vdi/Kconfig` — change `CONF_WITH_VDI_TRUECOLOR`'s default to `y if MACHINE_RPI`
- Modify: `vdi/build.mk` — add `obj-$(CONF_WITH_VDI_TRUECOLOR) += vdi_backend_truecolor.o`

**Interfaces:**
- Consumes: `vdi_backend_ops` (Task 3), `SCREEN_MODE_DESC`/`SCREEN_PIXEL_RGB565` (Task 1), `linea_vars.v_bas_ad`/`v_lin_wr` (existing).
- Produces: `extern const vdi_backend_ops packed_truecolor_backend_ops;` (declared in Task 3's `vdi_backend.h`, defined here). Used by Task 5.

This backend is entirely new code (unlike the planar backend, nothing here is extracted from an existing function), so it's self-contained in one file with no cross-file `static`-visibility concerns.

The minimal pseudo-palette maps VDI color indexes 0-15 to RGB565 using the same 16 colors already in `raspi_dflt_palette[0..15]` (`bios/raspi_screen.c`) — copied locally rather than shared, since `vdi/` must not depend on a `bios/raspi_screen.c` internal, and because this palette is a VDI-level default, not an RPi-specific one.

- [ ] **Step 1: Write `vdi/vdi_backend_truecolor.c`**

```c
/*
 * vdi_backend_truecolor.c - packed 16bpp RGB565 truecolor VDI backend
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "../bios/lineavars.h"
#include "vdi_defs.h"
#include "vdi_backend.h"

/*
 * Default VDI palette, indexes 0-15, as packed 0x00BBGGRR values --
 * mirrors raspi_dflt_palette[0..15] in bios/raspi_screen.c (the
 * standard 16-color VDI palette: white, red, green, yellow, blue,
 * magenta, cyan, ltgray, gray, ltred, ltgreen, ltyellow, ltblue,
 * ltmagenta, ltcyan, black). This is the "default mapping needed by
 * colors already in use" the design calls for -- full vs_color()/
 * vq_color() truecolor semantics are a follow-up.
 */
static const ULONG default_prgb_palette[16] = {
    0x00ffffff, 0x000000ff, 0x0000ff00, 0x0000ffff,
    0x00ff0000, 0x00ff00ff, 0x00ffff00, 0x00bbbbbb,
    0x00888888, 0x000000aa, 0x0000aa00, 0x0000aaaa,
    0x00aa0000, 0x00aa00aa, 0x00aaaa00, 0x00000000
};

static UWORD rgb565_from_prgb(ULONG prgb)
{
    UBYTE r = (UBYTE)(prgb & 0xffUL);
    UBYTE g = (UBYTE)((prgb >> 8) & 0xffUL);
    UBYTE b = (UBYTE)((prgb >> 16) & 0xffUL);

    return (UWORD)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static UWORD truecolor_pixel_for_index(WORD index)
{
    if (index < 0 || index > 15)
        index = 0;

    return rgb565_from_prgb(default_prgb_palette[index]);
}

/*
 * Address calculation for a packed 16bpp (2 bytes/pixel) framebuffer.
 * Fixed at 2 bytes/pixel because this backend is only ever selected for
 * SCREEN_PIXEL_RGB565 (see vdi_backend_select()).
 */
static UWORD *truecolor_get_start_addr(WORD x, WORD y)
{
    UBYTE *addr;

    addr = v_bas_ad;
    addr += (LONG)x * 2;
    addr += (LONG)y * linea_vars.v_lin_wr;
    return (UWORD *)addr;
}

static UWORD truecolor_get_pixel(WORD x, WORD y)
{
    UWORD raw = *truecolor_get_start_addr(x, y);
    WORD i;

    for (i = 0; i < 16; i++) {
        if (rgb565_from_prgb(default_prgb_palette[i]) == raw)
            return (UWORD)i;
    }

    return 0;   /* not one of the default 16 -- report black rather than guess */
}

static void truecolor_put_pixel(WORD x, WORD y, UWORD color)
{
    UWORD *addr = truecolor_get_start_addr(x, y);

    *addr = truecolor_pixel_for_index((WORD)color);
}

static void truecolor_fill_rect(const VwkAttrib *attr, const Rect *rect)
{
    const UWORD patmsk = attr->patmsk;
    UWORD pixel = truecolor_pixel_for_index((WORD)attr->color);
    UBYTE *row = (UBYTE *)truecolor_get_start_addr(0, rect->y1);
    WORD x, y, i;

    for (y = rect->y1; y <= rect->y2; y++, row += linea_vars.v_lin_wr) {
        int patind = patmsk & y;   /* starting pattern */
        UWORD pattern = attr->patptr[patind];
        UWORD *dst = (UWORD *)row;

        for (x = rect->x1, i = 0; x <= rect->x2; x++, i++) {
            BOOL set = (pattern & ((1<<15)>>(i & 15))) != 0;

            switch (attr->wrt_mode) {
            case 3:                 /* erase (reverse transparent) mode */
                if (!set)
                    dst[x] = pixel;
                break;
            case 2:                 /* xor mode */
                if (set)
                    dst[x] ^= pixel;
                break;
            case 1:                 /* transparent mode */
                if (set)
                    dst[x] = pixel;
                break;
            default:                /* replace mode */
                dst[x] = set ? pixel : 0;
            }
        }
    }
}

static BOOL truecolor_open(Vwk *vwk)
{
    (void)vwk;
    return TRUE;
}

static void truecolor_close(Vwk *vwk)
{
    (void)vwk;
}

const vdi_backend_ops packed_truecolor_backend_ops = {
    truecolor_open,
    truecolor_close,
    truecolor_get_start_addr,
    truecolor_get_pixel,
    truecolor_put_pixel,
    truecolor_fill_rect,
};
```

- [ ] **Step 2: Wire it into the build, and enable it for Raspberry Pi**

Modify `vdi/build.mk`:

```make
obj-y += vdi_backend.o vdi_backend_planar.o
obj-$(CONF_WITH_VDI_TRUECOLOR) += vdi_backend_truecolor.o
```

Modify `vdi/Kconfig`'s `CONF_WITH_VDI_TRUECOLOR` entry from Task 3:

```
config CONF_WITH_VDI_TRUECOLOR
	bool "Packed truecolor VDI backend"
	default y if MACHINE_RPI
	default n
	help
	  Build the packed-truecolor VDI backend (vdi_backend_truecolor.o).
	  Raspberry Pi is the first machine that uses it -- see issue #35.
```

At this point `vwk->mode` for Raspberry Pi still reports 8bpp packed-indexed (Task 5 changes that), so `vdi_backend_select()` still returns `&planar_backend_ops`... no -- it returns `NULL` for RPi's packed-indexed descriptor, exactly as in Task 3, since packed-indexed matches neither backend. The truecolor backend is now *available* to be selected, just not yet *selected*, because nothing reports a matching descriptor yet.

- [ ] **Step 3: Verify it builds and still boots identically to Task 3**

```sh
make rpi2_defconfig && make
timeout 8 qemu-system-arm -M raspi2 -bios kernel7.img -d guest_errors -serial stdio
```

Expected: identical to Task 3's check — `vdi_v_opnwk: mode layout=1 color_model=0 bpp=8 backend=none`, no `guest_errors`, screen unchanged. The new backend compiles and links (proving the ops table itself is well-formed) but nothing selects it yet.

```sh
make virt-arm_defconfig && make
```

Expected: builds cleanly. `CONF_WITH_VDI_TRUECOLOR` is off here (`MACHINE_RPI` isn't set), so `vdi_backend_truecolor.o` isn't even compiled in for this target — confirms the Kconfig gating works.

Build one Atari/m68k configuration if available; expect the same result (option off, file not compiled).

- [ ] **Step 4: `make gitready` and commit**

```sh
make gitready
git add vdi/vdi_backend_truecolor.c vdi/Kconfig vdi/build.mk
git commit -m "Add packed truecolor VDI backend"
git push
```

---

## Task 5: Switch Raspberry Pi to 16bpp and route the converted primitives

**Files:**
- Modify: `bios/raspi_screen.c` — request a 16bpp framebuffer instead of 8bpp; report a truecolor descriptor from `raspi_get_current_mode_desc()`
- Modify: `vdi/vdi_misc.c` — `get_start_addr()` becomes a one-line dispatch; delete its old `#if CONF_CHUNKY_PIXELS`/`#else` bodies
- Modify: `vdi/vdi_fill.c` — `pixelread()` and `put_pix()` become one-line dispatches; delete their old chunky/non-chunky bodies (and the now-unused `get_color()` moves — see note below)
- Modify: `vdi/vdi_line.c` — `draw_rect_common()` becomes a one-line dispatch; delete its old `#if`/`#else` bodies

This is the task where Raspberry Pi's actual drawing behavior changes, so it's the one to boot-test visually.

- [ ] **Step 1: Request a 16bpp framebuffer in `bios/raspi_screen.c`**

In `raspi_screen_init()`, change the depth tag's value from `8` to `16`:

```c
    init_tags =
    {
        {{PROPTAG_SET_PHYS_WIDTH_HEIGHT, 8, 8}},
        {{PROPTAG_SET_VIRT_WIDTH_HEIGHT, 8, 8}},
        {{PROPTAG_SET_DEPTH,             4, 4}, 16},
        {{PROPTAG_SET_VIRTUAL_OFFSET,    8, 8}, 0, 0},
        {{PROPTAG_ALLOCATE_BUFFER,       8, 4}, 0},
        {{PROPTAG_GET_PITCH,             4, 0}}
    };
```

(Only the `set_depth` initializer's trailing `8` becomes `16` — everything else in this struct literal is unchanged. `raspi_screen_width_in_bytes` still gets whatever pitch the firmware reports for the new depth, exactly as before.)

- [ ] **Step 2: Report the new mode from `raspi_get_current_mode_desc()`**

```c
void raspi_get_current_mode_desc(SCREEN_MODE_DESC *desc)
{
    desc->width = raspi_screen_width;
    desc->height = raspi_screen_height;
    desc->pitch = raspi_screen_width_in_bytes;
    desc->bits_per_pixel = 16;
    desc->layout = SCREEN_LAYOUT_PACKED;
    desc->color_model = SCREEN_COLOR_TRUECOLOR;
    desc->pixel_format = SCREEN_PIXEL_RGB565;
}
```

Note: `raspi_get_current_mode_info()` (the older, still-used call) still hardcodes `*planes = 8` — leave it untouched. It has other callers this plan doesn't audit (e.g. `INQ_TAB`/`DEV_TAB` setup in `vdi_v_opnwk()` goes through `linea_vars.v_planes`, which by this task comes from the *descriptor* via `linea_init()`, not from `raspi_get_current_mode_info()` — so this stale value doesn't reach anything this plan's converted primitives touch. Fixing `raspi_get_current_mode_info()` itself, or removing it if it becomes fully dead, is a follow-up, not part of this task.)

- [ ] **Step 3: Convert `get_start_addr()` to dispatch, in `vdi/vdi_misc.c`**

Replace the whole function:

```c
UWORD * get_start_addr(const WORD x, const WORD y)
{
    return vdi_screen_backend()->get_start_addr(x, y);
}
```

Add `#include "vdi_backend.h"` to this file's includes if not already present via `vdi_defs.h` (it isn't — add it explicitly).

- [ ] **Step 4: Convert `pixelread()` and `put_pix()` to dispatch, in `vdi/vdi_fill.c`**

Add `#include "vdi_backend.h"` to this file's includes.

Delete the now-superseded `get_color()` static helper and the old `pixelread()` body (both fully absorbed into `planar_get_pixel()` in Task 3), replacing `pixelread()` with:

```c
static UWORD
pixelread(const WORD x, const WORD y)
{
    return vdi_screen_backend()->get_pixel(x, y);
}
```

Replace `put_pix()`'s body. It still needs its own bounds check (unchanged from before — `get_start_addr()` itself now dispatches, so this check works unmodified for both backends), but the actual pixel write moves to the backend:

```c
void
put_pix(void)
{
    UWORD *addr;
    const WORD x = PTSIN[0];
    const WORD y = PTSIN[1];
    UWORD color;

    /* convert x,y to start address */
    addr = get_start_addr(x, y);
    /* co-ordinates can wrap, but cannot write outside screen,
     * alternatively this could check against v_bas_ad+vram_size()
     */
    if (addr < (UWORD*)v_bas_ad || addr >= get_start_addr(linea_vars.V_REZ_HZ, linea_vars.V_REZ_VT)) {
        return;
    }
    color = INTIN[0];           /* device dependent encoded color bits */

    vdi_screen_backend()->put_pixel(x, y, color);
}
```

- [ ] **Step 5: Convert `draw_rect_common()` to dispatch, in `vdi/vdi_line.c`**

Add `#include "vdi_backend.h"` to this file's includes.

Replace the whole function:

```c
void draw_rect_common(const VwkAttrib *attr, const Rect *rect)
{
    vdi_screen_backend()->fill_rect(attr, rect);
}
```

`CONF_CHUNKY_PIXELS` no longer forks any function body after this step — it still exists as a Kconfig option (removing it outright is an open follow-up per the design doc), but nothing in `vdi/vdi_misc.c`, `vdi/vdi_fill.c`, or `vdi/vdi_line.c` tests it anymore. Confirm this:

```sh
grep -rn CONF_CHUNKY_PIXELS vdi/vdi_misc.c vdi/vdi_fill.c vdi/vdi_line.c
```

Expected: no output.

- [ ] **Step 6: Note the known gap this doesn't fix (no code change — just confirm the existing behavior)**

`vdi/arch/arm/vdi_tblit.c`'s `normal_blit()` only implements its `CONF_CHUNKY_PIXELS` text-blit path `if (vars->nbrplane == 8 ...)`; anything else already falls through to an empty `{ /* TODO implement bitplane blit here */ }` block — i.e. it already fails safe (draws nothing) rather than corrupting memory. Once Raspberry Pi reports 16bpp, `nbrplane` will be 16, so this pre-existing no-op path is what runs — text rendering does not draw on Raspberry Pi after this task, exactly as it already silently didn't for any non-8bpp chunky configuration before this change. This is expected and tracked as follow-up work (design doc's Non-Goals / issue #35 part 2a), not a regression to fix here. Confirm nothing crashes by watching for `guest_errors` in Step 7's boot test, not by trying to fix text rendering.

- [ ] **Step 7: Build and boot-test on Raspberry Pi**

```sh
make rpi2_defconfig && make
timeout 8 qemu-system-arm -M raspi2 -bios kernel7.img -d guest_errors -serial stdio
```

Expected: `vdi_v_opnwk: mode layout=1 color_model=1 bpp=16 backend=selected` in the serial log (color_model `1` = `SCREEN_COLOR_TRUECOLOR`). No `guest_errors`. The display should show recognizable (if crude — no text, no icons, no mouse cursor, since none of those are converted) rectangle fills and pixel-level drawing in truecolor rather than a corrupted or blank screen. If the screen shows diagonal tearing or a shifted/garbled image, the likely cause is a pitch mismatch — check the `raspi_get_current_mode_desc()` KDEBUG trace's implied pitch against `raspi_screen_width_in_bytes` at the point `raspi_screen_init()` sets it.

- [ ] **Step 8: Verify the other machines still build (planar path fully unaffected)**

```sh
make virt-arm_defconfig && make
timeout 8 qemu-system-arm -M virt,highmem=off -cpu cortex-a7 -m 128 -kernel virt-arm.elf -d guest_errors -display none -serial stdio
```

Expected: builds and boots exactly as in Task 4 — this machine's descriptor and backend selection are untouched by this task, only Raspberry Pi's are.

Build one Atari/m68k configuration if available; expect an unchanged, clean build — `vdi/vdi_misc.c`/`vdi/vdi_fill.c`/`vdi/vdi_line.c`'s dispatch calls resolve to `planar_backend_ops` there exactly as they did implicitly (via the old `#else` branches) before this task, since `planar_fill_rect()`/`planar_get_pixel()`/etc. are byte-for-byte the same logic that used to live inline.

- [ ] **Step 9: `make gitready` and commit**

```sh
make gitready
git add bios/raspi_screen.c vdi/vdi_misc.c vdi/vdi_fill.c vdi/vdi_line.c
git commit -m "Switch Raspberry Pi to 16bpp truecolor and route pixel/fill primitives through the VDI backend"
git push
```

---

## Task 6: Full verification pass and PR readiness

**Files:** none (verification only).

- [ ] **Step 1: Build every configuration in `configs/`**

```sh
for cfg in configs/*_defconfig; do
    name=$(basename "$cfg" _defconfig)
    echo "=== $name ==="
    make "${name}_defconfig" && make || echo "FAILED: $name"
done
```

Expected: every configuration builds. If any m68k/Amiga/ColdFire configuration fails because the local toolchain isn't installed, that's an environment limitation, not a code problem — note which ones couldn't be tried locally rather than skipping the ones that can be.

- [ ] **Step 2: `make gitready` and `git diff --check` on the full branch**

```sh
make gitready
git diff --check master...HEAD
```

Expected: no output from either — no trailing whitespace, no formatting drift across the whole branch.

- [ ] **Step 3: Re-run the Raspberry Pi and virt-arm QEMU smoke tests one more time against the final tree**

```sh
make rpi2_defconfig && make
timeout 8 qemu-system-arm -M raspi2 -bios kernel7.img -d guest_errors -serial stdio
make virt-arm_defconfig && make
timeout 8 qemu-system-arm -M virt,highmem=off -cpu cortex-a7 -m 128 -kernel virt-arm.elf -d guest_errors -display none -serial stdio
```

Expected: same results as Task 5 Steps 7-8 — no `guest_errors`, RPi shows truecolor fills, virt-arm boots as far as before.

- [ ] **Step 4: Update the PR description and mark it ready for review**

```sh
gh pr ready 71
```

Before doing so, edit the PR body (`gh pr edit 71 --body ...`) to summarize what actually landed across the six tasks and explicitly call out the known gaps this slice leaves (per the design doc's Open Follow-Ups): text rendering, mouse cursor, raster ops (`vro_cpyfm`/`vrt_cpyfm`/`vr_trnfm`), and full `vs_color()`/`vq_color()` palette semantics are all still unconverted on Raspberry Pi, and real-hardware visual validation (as opposed to QEMU) hasn't happened.

Also call out one deliberate deviation from the design doc's Error Handling section: the design says a descriptor with no matching backend should "fail mode selection outright." This plan only ever logs it via `KDEBUG` (`vdi_v_opnwk`'s trace in Task 3 Step 11) and never turns it into a hard failure, because promoting it to a real error would need every `configs/*_defconfig` variant's descriptor confirmed to resolve successfully first — this plan only build-tests one Atari/m68k config and boot-tests raspi2/virt-arm, not the full Amiga/ColdFire/TT/Falcon matrix. Turning an unverified case into a boot-time hard failure risked bricking a configuration this plan never actually exercised, which is worse than leaving the (currently unreachable in every machine this plan does verify) trace-only path in place. Enforcing the hard failure is a follow-up once broader hardware/config coverage confirms it's safe.

- [ ] **Step 5: Real-hardware note for the user**

This plan's QEMU checks confirm the boot path and that drawing doesn't crash or trip `guest_errors`, but the design doc is explicit that real Raspberry Pi hardware validation is required before claiming visual correctness — QEMU doesn't model all framebuffer/timing behavior. Flag this to the user as an outstanding manual step rather than treating Task 6's QEMU pass as full sign-off.
