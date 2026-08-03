# VDI Backend and Truecolor Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add descriptor-driven VDI backends, preserve Atari planar behavior, and move Raspberry Pi to a safe 16bpp packed truecolor foundation with working pixel/fill/line primitives.

**Architecture:** Screen drivers report the real framebuffer through a shared descriptor. Every VDI workstation owns a backend state and full operations table; Line-A resolves the physical screen workstation and uses its backend. This plan routes pixel, rectangle, and line operations, makes the Raspberry Pi BIOS console 16bpp-safe, and blocks unsupported packed-truecolor paths instead of falling back to planar code.

**Tech Stack:** Freestanding C90 with GNU extensions, Kconfig, Kbuild-style `build.mk`, pTOS fixed-width types, native host test programs, ARM and m68k cross builds.

## Global Constraints

- Planar and packed/truecolor layout are runtime mode facts, never inferred from platform or `v_planes` alone.
- Packed truecolor is platform-neutral and must support future Falcon VIDEL and graphics-card modes.
- A planar implementation may run only when the screen descriptor reports a planar framebuffer.
- Backend state lives directly in `Vwk`; the physical handle-1 workstation is the current screen workstation.
- Line-A is a compatibility layer over the current screen workstation and owns no backend state.
- `linea_vars.v_lin_wr` and `BYTES_LIN` come from descriptor pitch.
- Missing packed operations never fall back to planar code.
- Keep `Vwk.fill_color` and all fields before it at their current offsets because applications may provide the documented fake 16-word `CUR_WORK` object.
- C code remains C90-compatible, uses pTOS fixed-width types, and keeps declarations at block starts.
- Raspberry Pi requests 16bpp and an explicit pixel order; returned mailbox values must be validated.
- Runtime visual correctness requires real Raspberry Pi hardware validation.

---

## File Structure

- Create `include/screen_mode.h`: shared descriptor constants, `SCREEN_MODE_DESC`, and descriptor validation API.
- Create `bios/screen_mode.c`: target-independent descriptor validation.
- Create `tools/test-screen-mode.c`: host tests for valid and invalid descriptors.
- Create `tools/test-screen-mode.sh`: compile and run the descriptor tests without a target configuration.
- Modify `bios/screen.h`, `bios/screen.c`, `bios/videl.[ch]`, `bios/amiga.[ch]`, `bios/raspi_screen.[ch]`: produce current-mode descriptors.
- Modify `bios/lineainit.c`: populate legacy Line-A geometry from the descriptor.
- Create `vdi/vdi_backend.h`: full backend operations interface and lifecycle declarations.
- Create `include/vdi_backend_api.h`: narrow BIOS-to-VDI mode-change notification declaration.
- Create `vdi/vdi_backend.c`: exact backend selection, physical-workstation lookup, lifecycle, and unsupported dispatch helpers.
- Create `vdi/vdi_backend_planar.c`: moved planar pixel/fill/line implementations.
- Create `vdi/vdi_backend_truecolor.c`: packed-16 pixel/fill/line implementations and pseudo-palette conversion.
- Create `tools/test-vdi-truecolor.c`: guarded-buffer host tests for packed-16 primitives.
- Create `tools/test-vdi-truecolor.sh`: compile and run packed-16 host tests.
- Modify `vdi/vdi_defs.h`, `vdi/vdi_control.c`: store and manage per-workstation backend state.
- Modify `vdi/vdi_misc.c`, `vdi/vdi_fill.c`, `vdi/vdi_line.c`, `vdi/vdi_text.c`, `vdi/vdi_bezier.c`: route initial primitives through workstation backends.
- Modify `vdi/vdi_raster.c`, `vdi/vdi_textblit.c`, `vdi/vdi_mouse.c`: reject unsupported packed paths safely.
- Modify `bios/raspi_screen.c`: request/validate 16bpp and make the BIOS console operate on 16-bit pixels.
- Modify `vdi/Kconfig`, `vdi/build.mk`, `bios/build.mk`, `Makefile`: select and test the backend implementation.

---

### Task 1: Add the Shared Screen Mode Descriptor

**Files:**
- Create: `include/screen_mode.h`
- Create: `bios/screen_mode.c`
- Create: `tools/test-screen-mode.c`
- Create: `tools/test-screen-mode.sh`
- Modify: `bios/build.mk:18-23`
- Modify: `Makefile:49-52`

**Interfaces:**
- Produces: `SCREEN_MODE_DESC`, layout/color/pixel-format constants, `BOOL screen_mode_validate(const SCREEN_MODE_DESC *mode)`, and `UWORD screen_mode_pack_color(const SCREEN_MODE_DESC *mode, WORD red, WORD green, WORD blue)` for 0-1000 VDI components.
- Consumes: only `portab.h`; the validator must remain host-testable and independent of hardware globals.

- [ ] **Step 1: Write the failing descriptor test**

Create `tools/test-screen-mode.c` with cases for planar indexed, packed RGB565, zero pitch, undersized packed pitch, unsupported format, and pitch above the Line-A `UWORD` limit:

```c
#include <assert.h>
#include <string.h>
#include "screen_mode.h"

static SCREEN_MODE_DESC make_mode(void)
{
    SCREEN_MODE_DESC mode;

    memset(&mode, 0, sizeof(mode));
    mode.width = 640;
    mode.height = 480;
    mode.pitch = 1280UL;
    mode.bits_per_pixel = 16;
    mode.layout = SCREEN_LAYOUT_PACKED;
    mode.color_model = SCREEN_COLOR_TRUECOLOR;
    mode.pixel_format = SCREEN_PIXEL_RGB565;
    return mode;
}

int main(void)
{
    SCREEN_MODE_DESC mode;

    mode = make_mode();
    assert(screen_mode_validate(&mode));
    assert(screen_mode_pack_color(&mode, 1000, 0, 0) == 0xf800U);
    assert(screen_mode_pack_color(&mode, 0, 1000, 0) == 0x07e0U);
    assert(screen_mode_pack_color(&mode, 0, 0, 1000) == 0x001fU);

    mode.pitch = 1279UL;
    assert(!screen_mode_validate(&mode));

    mode = make_mode();
    mode.pixel_format = SCREEN_PIXEL_NONE;
    assert(!screen_mode_validate(&mode));

    mode = make_mode();
    mode.pitch = 0x10000UL;
    assert(!screen_mode_validate(&mode));

    mode = make_mode();
    mode.bits_per_pixel = 4;
    mode.pitch = 320UL;
    mode.layout = SCREEN_LAYOUT_PLANAR;
    mode.color_model = SCREEN_COLOR_INDEXED;
    mode.pixel_format = SCREEN_PIXEL_NONE;
    assert(screen_mode_validate(&mode));

    mode.pitch = 0;
    assert(!screen_mode_validate(&mode));

    mode = make_mode();
    mode.bits_per_pixel = 8;
    mode.pitch = 640UL;
    mode.color_model = SCREEN_COLOR_INDEXED;
    mode.pixel_format = SCREEN_PIXEL_NONE;
    assert(screen_mode_validate(&mode));
    return 0;
}
```

Create `tools/test-screen-mode.sh`:

```sh
#!/bin/sh
set -eu
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
${CC:-cc} -std=gnu90 -Wall -Wextra -Werror \
    -Iinclude tools/test-screen-mode.c bios/screen_mode.c \
    -o "$tmpdir/test-screen-mode"
"$tmpdir/test-screen-mode"
```

- [ ] **Step 2: Run the test and verify it fails**

Run: `sh tools/test-screen-mode.sh`

Expected: compilation fails because `screen_mode.h` and `screen_mode_validate()` do not exist.

- [ ] **Step 3: Define the descriptor and validator**

Create `include/screen_mode.h` with fixed-width stored fields, not enums:

```c
#ifndef SCREEN_MODE_H
#define SCREEN_MODE_H

#include "portab.h"

#define SCREEN_LAYOUT_PLANAR  1U
#define SCREEN_LAYOUT_PACKED  2U

#define SCREEN_COLOR_INDEXED    1U
#define SCREEN_COLOR_TRUECOLOR  2U

#define SCREEN_PIXEL_NONE    0U
#define SCREEN_PIXEL_RGB555  1U
#define SCREEN_PIXEL_RGB565  2U
#define SCREEN_PIXEL_BGR555  3U
#define SCREEN_PIXEL_BGR565  4U

typedef struct screen_mode_desc {
    UWORD width;
    UWORD height;
    ULONG pitch;
    UWORD bits_per_pixel;
    UWORD layout;
    UWORD color_model;
    UWORD pixel_format;
    ULONG flags;
} SCREEN_MODE_DESC;

BOOL screen_mode_validate(const SCREEN_MODE_DESC *mode);
UWORD screen_mode_pack_color(const SCREEN_MODE_DESC *mode,
                             WORD red, WORD green, WORD blue);

#endif
```

Implement `bios/screen_mode.c` so it rejects zero dimensions/depth/pitch, pitch above `0xffffUL`, mismatched layout/color combinations, unsupported truecolor formats, and packed pitch below `width * ((bits_per_pixel + 7U) / 8U)`. Accept planar indexed and packed indexed factual descriptors with `SCREEN_PIXEL_NONE`; the VDI selector in Task 3 rejects packed indexed because no backend supports it. Implement RGB555/RGB565/BGR555/BGR565 component packing with clamped 0-1000 inputs.

Add `screen_mode.o` to the generic BIOS object list. Add `test-screen-mode` to `UNCONFIGURED_GOALS` and this Makefile target (with a recipe tab):

```make
.PHONY: test-screen-mode
test-screen-mode:
	tools/test-screen-mode.sh
```

- [ ] **Step 4: Run the descriptor test**

Run: `make test-screen-mode`

Expected: exit 0 with no assertion failures.

- [ ] **Step 5: Commit the descriptor foundation**

```bash
git add include/screen_mode.h bios/screen_mode.c bios/build.mk \
    tools/test-screen-mode.c tools/test-screen-mode.sh Makefile
git commit -m "Add screen mode descriptors"
git push
```

---

### Task 2: Make Screen Drivers Report Actual Layouts

**Files:**
- Modify: `bios/screen.h:112-123`
- Modify: `bios/screen.c:667-750`
- Modify: `bios/videl.h:92-98`
- Modify: `bios/videl.c:270-301,827-832`
- Modify: `bios/amiga.h:44-64`
- Modify: `bios/amiga.c:348-353`
- Modify: `bios/raspi_screen.h:10-35`
- Modify: `bios/raspi_screen.c:115-149,237-242`
- Modify: `bios/lineainit.c:39-61`

**Interfaces:**
- Consumes: `SCREEN_MODE_DESC` and `screen_mode_validate()` from Task 1.
- Produces: `BOOL screen_get_current_mode_desc(SCREEN_MODE_DESC *mode)` plus machine-specific descriptor producers; preserves `screen_get_current_mode_info()` as a compatibility wrapper.

- [ ] **Step 1: Add a failing build expectation for descriptor producers**

Change declarations first so each machine header exposes its producer:

```c
BOOL raspi_get_current_mode_desc(SCREEN_MODE_DESC *mode);
BOOL amiga_get_current_mode_desc(SCREEN_MODE_DESC *mode);
BOOL videl_get_current_mode_desc(SCREEN_MODE_DESC *mode);
```

Run: `make rpi4_defconfig && make obj/screen.o obj/raspi_screen.o obj/lineainit.o`

Expected: link/compile failure because the new producers are not implemented.

- [ ] **Step 2: Implement descriptor producers**

Implement these exact rules:

- ST/STE/TT shifter: planar indexed, `SCREEN_PIXEL_NONE`, mode-table dimensions/depth, pitch `(ULONG)width / 8UL * bits_per_pixel`.
- VIDEL 1/2/4/8bpp: planar indexed; VIDEL 16bpp: packed truecolor with the actual Falcon pixel format; pitch from the hardware line-width register multiplied by two bytes.
- Amiga: planar indexed, current dimensions, stored byte pitch.
- Raspberry Pi reports the accepted current 8bpp packed-indexed framebuffer and pitch during this staged task; Task 3 deliberately rejects that descriptor as a VDI backend, and Task 9 atomically switches the hardware request and descriptor to supported packed truecolor.

Implement `screen_get_current_mode_desc()` as the machine dispatcher and make `screen_get_current_mode_info()` a wrapper that copies `bits_per_pixel`, width, and height from the validated descriptor.

- [ ] **Step 3: Make Line-A geometry descriptor-driven**

Change `linea_init()` to declare `SCREEN_MODE_DESC mode` at the block start, call `screen_get_current_mode_desc(&mode)`, panic if it fails validation, then assign:

```c
linea_vars.v_planes = mode.bits_per_pixel;
linea_vars.V_REZ_HZ = mode.width;
linea_vars.V_REZ_VT = mode.height;
linea_vars.v_lin_wr = (UWORD)mode.pitch;
linea_vars.BYTES_LIN = (UWORD)mode.pitch;
```

Keep the existing cursor-save selection and `DEV_TAB` updates after these assignments.

- [ ] **Step 4: Build representative current modes**

Run sequentially:

```bash
make rpi4_defconfig && make obj/screen.o obj/raspi_screen.o obj/lineainit.o
make amiga_defconfig && make obj/screen.o obj/amiga.o obj/lineainit.o
make floppy_defconfig && make obj/screen.o obj/videl.o obj/lineainit.o
```

Expected: all object sets compile; existing modes retain their current pitch and layout.

- [ ] **Step 5: Commit mode reporting**

```bash
git add bios/screen.h bios/screen.c bios/videl.h bios/videl.c \
    bios/amiga.h bios/amiga.c bios/raspi_screen.h bios/raspi_screen.c \
    bios/lineainit.c
git commit -m "Report framebuffer layouts at runtime"
git push
```

---

### Task 3: Define and Test the Full VDI Backend Boundary

**Files:**
- Create: `vdi/vdi_backend.h`
- Create: `vdi/vdi_backend.c`
- Create: `vdi/vdi_backend_planar.c`
- Create: `vdi/vdi_backend_truecolor.c`
- Create: `include/vdi_backend_api.h`
- Create: `tools/test-vdi-backend.c`
- Create: `tools/test-vdi-backend.sh`
- Modify: `vdi/vdi_defs.h:124-170`
- Modify: `vdi/build.mk:5-18`
- Modify: `vdi/Kconfig:12-20`
- Modify: `Makefile:49-52`

**Interfaces:**
- Consumes: `SCREEN_MODE_DESC`.
- Produces: `VDI_BACKEND_OPS`, `VDI_BACKEND_STATE`, `vdi_backend_bind()`, `vdi_backend_clone()`, `vdi_backend_close()`, and `vdi_get_screen_vwk()`.

- [ ] **Step 1: Write selector tests**

Create a native test that constructs descriptors and verifies the pure selector:

```c
assert(vdi_backend_kind(&planar_mode) == VDI_BACKEND_PLANAR);
assert(vdi_backend_kind(&rgb565_mode) == VDI_BACKEND_TRUECOLOR16);
assert(vdi_backend_kind(&packed8_mode) == VDI_BACKEND_UNSUPPORTED);
assert(vdi_backend_kind(&bad_format_mode) == VDI_BACKEND_UNSUPPORTED);
```

Declare this exact selector API in `vdi_backend.h`:

```c
#define VDI_BACKEND_UNSUPPORTED  0U
#define VDI_BACKEND_PLANAR       1U
#define VDI_BACKEND_TRUECOLOR16  2U

UWORD vdi_backend_kind(const SCREEN_MODE_DESC *mode);
```

The test must also assert that packed indexed 8bpp and an unknown format never return the planar kind. Compile it through `tools/test-vdi-backend.sh` with `-std=gnu90 -Wall -Wextra -Werror`.

- [ ] **Step 2: Run the selector test and verify it fails**

Run: `sh tools/test-vdi-backend.sh`

Expected: compilation fails because the backend API does not exist.

- [ ] **Step 3: Add the full operations interface**

Define the full table with these signatures; use named initializers in every table definition:

```c
typedef struct vdi_backend_ops {
    BOOL (*open)(Vwk *vwk);
    BOOL (*clone)(Vwk *vwk, const Vwk *source);
    void (*close)(Vwk *vwk);
    BOOL (*mode_changed)(Vwk *vwk, const SCREEN_MODE_DESC *mode);
    UBYTE *(*pixel_addr)(const Vwk *vwk, WORD x, WORD y);
    ULONG (*read_pixel)(const Vwk *vwk, WORD x, WORD y);
    BOOL (*write_pixel)(Vwk *vwk, WORD x, WORD y, ULONG pixel);
    BOOL (*fill_rect)(Vwk *vwk, const VwkAttrib *attr, const Rect *rect);
    BOOL (*draw_line)(Vwk *vwk, const Line *line, WORD wrt_mode,
                      UWORD color, UWORD *linemask);
    BOOL (*draw_vertical_line)(Vwk *vwk, const Line *line, WORD wrt_mode,
                               UWORD color, UWORD *linemask);
    BOOL (*scan_seed_span)(Vwk *vwk, const VwkClip *clip, WORD x, WORD y,
                           ULONG search_color, WORD *left, WORD *right);
    BOOL (*raster_copy)(Vwk *vwk, BOOL transparent);
    BOOL (*raster_transform)(Vwk *vwk);
    BOOL (*text_blit)(Vwk *vwk, void *vars);
    BOOL (*mouse_draw)(Vwk *vwk, WORD x, WORD y);
    void (*mouse_restore)(Vwk *vwk);
    BOOL (*set_color)(Vwk *vwk, WORD index, const WORD *rgb);
    BOOL (*get_color)(Vwk *vwk, WORD index, WORD requested, WORD *rgb);
} VDI_BACKEND_OPS;
```

Append this state to the end of `Vwk`, after `bez_qual`:

```c
typedef struct vdi_backend_state {
    SCREEN_MODE_DESC mode;
    const struct vdi_backend_ops *ops;
    UBYTE *framebuffer;
    ULONG pseudo_palette[256];
    ULONG capabilities;
} VDI_BACKEND_STATE;
```

Do not insert fields before `fill_color`.

Implement exact selection in `vdi_backend.c`: planar indexed selects planar; packed truecolor 16 with RGB555/RGB565/BGR555/BGR565 selects truecolor16; every other combination is unsupported.

Create `vdi_backend_planar.c` and `vdi_backend_truecolor.c` with exported operation tables. In this task, lifecycle hooks initialize/copy/clear state and all drawing slots are explicitly `NULL`; Tasks 5-8 fill the slots. This is staged scaffolding, not a fallback: direct legacy planar calls remain in place until each primitive is routed.

Replace `CONF_CHUNKY_PIXELS` in `vdi/Kconfig` with `CONF_WITH_VDI_TRUECOLOR`, defaulting on for `MACHINE_RPI` and `CONF_WITH_VIDEL`. Add `vdi_backend.o` and `vdi_backend_planar.o` unconditionally and `vdi_backend_truecolor.o` conditionally, preserving `endvdi.o` as the last VDI object.

- [ ] **Step 4: Run selector and configuration tests**

Run:

```bash
make test-vdi-backend
make rpi4_defconfig
grep '^CONF_WITH_VDI_TRUECOLOR=y' .config
make floppy_defconfig
```

Expected: selector passes; RPi enables truecolor; Atari configuration generation succeeds.

- [ ] **Step 5: Commit the backend boundary**

```bash
git add vdi/vdi_backend.h vdi/vdi_backend.c vdi/vdi_defs.h \
    vdi/vdi_backend_planar.c vdi/vdi_backend_truecolor.c \
    vdi/build.mk vdi/Kconfig tools/test-vdi-backend.c \
    tools/test-vdi-backend.sh Makefile
git commit -m "Add VDI backend interface"
git push
```

---

### Task 4: Bind Backends to Workstations and Mode Changes

**Files:**
- Modify: `vdi/vdi_control.c:152-163,321-442`
- Modify: `vdi/vdi_backend.c`
- Modify: `bios/screen.c:954-999`
- Modify: `include/vdi_backend_api.h`
- Modify: `vdi/vdi_defs.h:225-228`

**Interfaces:**
- Consumes: backend lifecycle API from Task 3 and `screen_get_current_mode_desc()` from Task 2.
- Produces: the physical handle-1 workstation as the current screen workstation; virtual workstation cloning; `void vdi_screen_mode_changed(const SCREEN_MODE_DESC *mode)`.

- [ ] **Step 1: Add lifecycle assertions to the backend host test**

Extend the test to bind a physical `Vwk`, clone a virtual `Vwk`, and assert descriptor, ops, framebuffer, and pseudo-palette state are copied. Verify unsupported rebind clears `ops` instead of retaining the old table.

- [ ] **Step 2: Run the lifecycle test and verify it fails**

Run: `make test-vdi-backend`

Expected: failure because lifecycle binding/cloning is incomplete.

- [ ] **Step 3: Wire physical and virtual workstation lifecycle**

In `vdi_v_opnwk()`, fetch and validate the descriptor, bind `&virt_work` before color initialization, set handle 1, and set `linea_vars.CUR_WORK = &virt_work`.

In `vdi_v_opnvwk()`, clone backend state from `vdi_get_screen_vwk()` before publishing the new handle. Remove the assignment that changes `CUR_WORK` to a virtual workstation.

In virtual/physical close paths, call backend close hooks before freeing state. Do not assign `CUR_WORK` to a virtual-list predecessor.

Implement `vdi_get_screen_vwk()` to return the physical static workstation, never `linea_vars.CUR_WORK`. This preserves the documented fake-`CUR_WORK` compatibility behavior.

- [ ] **Step 4: Refresh workstation backends after resolution changes**

Declare `void vdi_screen_mode_changed(const SCREEN_MODE_DESC *mode)` in `include/vdi_backend_api.h`. Implement it to no-op before physical open, then rebind the physical workstation and clone/rebind each virtual workstation. On failure, clear the affected `ops` and trace the unsupported descriptor.

Call it from the existing `setscreen()` mode-change path after hardware setup and `linea_init()` have produced the new validated descriptor.

- [ ] **Step 5: Run lifecycle tests and representative builds**

Run:

```bash
make test-vdi-backend
make rpi4_defconfig && make obj/vdi_backend.o obj/vdi_control.o obj/screen.o
make floppy_defconfig && make obj/vdi_backend.o obj/vdi_control.o obj/screen.o
```

Expected: tests pass and both object sets compile.

- [ ] **Step 6: Commit workstation lifecycle**

```bash
git add vdi/vdi_backend.c vdi/vdi_control.c vdi/vdi_defs.h bios/screen.c \
    include/vdi_backend_api.h tools/test-vdi-backend.c
git commit -m "Bind VDI backends to workstations"
git push
```

---

### Task 5: Route Pixel Address, Read, Write, and Pseudo-Palette

**Files:**
- Modify: `vdi/vdi_backend_planar.c`
- Modify: `vdi/vdi_backend_truecolor.c`
- Modify: `vdi/vdi_backend.h`
- Modify: `vdi/vdi_misc.c:179-194`
- Modify: `vdi/vdi_fill.c:690-741,1049-1127`
- Modify: `vdi/vdi_col.c:538-692`
- Create: `tools/test-vdi-truecolor.c`
- Create: `tools/test-vdi-truecolor.sh`
- Modify: `Makefile:49-52`

**Interfaces:**
- Produces: backend-dispatched `vdi_pixel_addr()`, `vdi_read_pixel()`, `vdi_write_pixel()`, and per-workstation pseudo-palette updates.
- Consumes: `Vwk.backend.framebuffer`, descriptor pitch/format, and current VDI color mappings.

- [ ] **Step 1: Write guarded-buffer packed16 tests**

Test a framebuffer with padding and canaries. Verify address `base + y*pitch + x*2`, native `UWORD` round-trip, no write outside bounds, and RGB conversion endpoints:

```c
assert(screen_mode_pack_color(&mode, 1000, 0, 0) == 0xf800U);
assert(screen_mode_pack_color(&mode, 0, 1000, 0) == 0x07e0U);
assert(screen_mode_pack_color(&mode, 0, 0, 1000) == 0x001fU);
```

Add a BGR565 case with red and blue swapped. Compile the backend source natively through `tools/test-vdi-truecolor.sh`.

- [ ] **Step 2: Run packed16 tests and verify they fail**

Run: `sh tools/test-vdi-truecolor.sh`

Expected: failures because packed16 primitives are not implemented.

- [ ] **Step 3: Move planar pixel code and implement packed16 pixels**

Move the existing planar address/read/write bodies into the planar backend. Implement packed16 address/read/write using descriptor pitch and native `UWORD` access. Bounds-check coordinates before pointer arithmetic.

Make `get_start_addr()` a compatibility wrapper that dispatches through `vdi_get_screen_vwk()`; update VDI-aware callers to pass their explicit `Vwk` through the new helpers.

For truecolor `v_get_pixel()`, return zero in `INTOUT[0]` and the raw packed pixel in `INTOUT[1]`. Keep planar PEL/reverse-map behavior unchanged.

- [ ] **Step 4: Initialize and update the pseudo-palette**

Initialize all 256 pseudo-palette entries during backend open from requested VDI colors, with at least the default 16 pens and mapped pen 1 valid. Make truecolor `vdi_vs_color()` call the backend color hook after clamping RGB values, so subsequent drawing sees the new packed value. Clone the pseudo-palette for virtual workstations.

- [ ] **Step 5: Run pixel tests and object builds**

Run:

```bash
make test-vdi-truecolor
make rpi4_defconfig && make obj/vdi_backend_truecolor.o obj/vdi_fill.o obj/vdi_misc.o obj/vdi_col.o
make floppy_defconfig && make obj/vdi_backend_planar.o obj/vdi_fill.o obj/vdi_misc.o obj/vdi_col.o
```

Expected: tests pass and both backend variants compile.

- [ ] **Step 6: Commit pixel routing**

```bash
git add vdi/vdi_backend.h vdi/vdi_backend_planar.c \
    vdi/vdi_backend_truecolor.c vdi/vdi_misc.c vdi/vdi_fill.c \
    vdi/vdi_col.c tools/test-vdi-truecolor.c \
    tools/test-vdi-truecolor.sh Makefile
git commit -m "Route VDI pixels through backends"
git push
```

---

### Task 6: Route Rectangle and Pattern Fill

**Files:**
- Modify: `vdi/vdi_backend_planar.c`
- Modify: `vdi/vdi_backend_truecolor.c`
- Modify: `vdi/vdi_line.c:343-707`
- Modify: `vdi/vdi_fill.c:480-540,950-1020`
- Modify: `vdi/vdi_defs.h:234-244`
- Modify: `tools/test-vdi-truecolor.c`

**Interfaces:**
- Changes: `draw_rect_common(Vwk *vwk, const VwkAttrib *attr, const Rect *rect)`.
- Produces: planar and packed16 rectangle callbacks with all four write modes.

- [ ] **Step 1: Add failing fill tests**

Add tests for replace, transparent, XOR, erase, pattern phase at nonzero `x1`, pitch padding, and a clipped one-row rectangle. Expected truecolor semantics:

- replace: pattern 1 writes foreground; pattern 0 writes pseudo-palette entry 0.
- transparent: pattern 1 writes foreground; pattern 0 preserves destination.
- XOR: pattern 1 complements destination; pattern 0 preserves destination.
- erase: pattern 0 writes foreground; pattern 1 preserves destination.

- [ ] **Step 2: Run fill tests and verify they fail**

Run: `make test-vdi-truecolor`

Expected: fill assertions fail because the callback is absent.

- [ ] **Step 3: Split and dispatch rectangle fill**

Move the existing planar body, including hardware blitter support, into the planar backend with behavior unchanged. Implement packed16 fill using `UWORD *`, descriptor pitch, pseudo-palette lookup, and pattern bit phase based on `rect->x1 & 15`.

Change all VDI callers to pass their `Vwk`. Line-A rectangle/hline wrappers pass `vdi_get_screen_vwk()` while retaining compatibility attributes from Line-A variables.

- [ ] **Step 4: Run fill tests and builds**

Run:

```bash
make test-vdi-truecolor
make rpi4_defconfig && make obj/vdi_line.o obj/vdi_fill.o obj/vdi_backend_truecolor.o
make floppy_defconfig && make obj/vdi_line.o obj/vdi_fill.o obj/vdi_backend_planar.o
```

Expected: tests and builds pass.

- [ ] **Step 5: Commit rectangle routing**

```bash
git add vdi/vdi_backend_planar.c vdi/vdi_backend_truecolor.c \
    vdi/vdi_line.c vdi/vdi_fill.c vdi/vdi_defs.h \
    tools/test-vdi-truecolor.c
git commit -m "Route VDI rectangle fills through backends"
git push
```

---

### Task 7: Route Arbitrary and Vertical Lines

**Files:**
- Modify: `vdi/vdi_backend_planar.c`
- Modify: `vdi/vdi_backend_truecolor.c`
- Modify: `vdi/vdi_line.c:1372-1822`
- Modify: `vdi/vdi_text.c:300-340`
- Modify: `vdi/vdi_bezier.c:180-210`
- Modify: `vdi/vdi_defs.h:230-244`
- Modify: `tools/test-vdi-truecolor.c`

**Interfaces:**
- Changes: `abline(Vwk *vwk, const Line *line, WORD wrt_mode, UWORD color)`.
- Produces: backend arbitrary/vertical line callbacks that update `linea_vars.LN_MASK` through an explicit `UWORD *linemask` argument.

- [ ] **Step 1: Add failing line tests**

Test horizontal, vertical, shallow, steep, and negative-slope lines; each endpoint; padded pitch; all write modes; and deterministic line-mask rotation.

- [ ] **Step 2: Run line tests and verify they fail**

Run: `make test-vdi-truecolor`

Expected: line assertions fail because packed16 callbacks are absent.

- [ ] **Step 3: Split planar line code and implement packed16 lines**

Move the planar Bresenham and vertical-line bodies into the planar backend without changing plane loops. Implement packed16 Bresenham and vertical loops using pseudo-palette values and descriptor pitch. Preserve horizontal optimization through the backend rectangle callback.

Update every `abline()` caller to pass its workstation. Line-A passes `vdi_get_screen_vwk()`. Text underline and Bezier callers pass their existing `Vwk`.

- [ ] **Step 4: Run line tests and builds**

Run:

```bash
make test-vdi-truecolor
make rpi4_defconfig && make obj/vdi_line.o obj/vdi_text.o obj/vdi_bezier.o
make floppy_defconfig && make obj/vdi_line.o obj/vdi_text.o obj/vdi_bezier.o
```

Expected: tests and builds pass.

- [ ] **Step 5: Commit line routing**

```bash
git add vdi/vdi_backend_planar.c vdi/vdi_backend_truecolor.c \
    vdi/vdi_line.c vdi/vdi_text.c vdi/vdi_bezier.c \
    vdi/vdi_defs.h tools/test-vdi-truecolor.c
git commit -m "Route VDI lines through backends"
git push
```

---

### Task 8: Gate Unsupported Packed Operations

**Files:**
- Modify: `vdi/vdi_raster.c:201-249,912-1104`
- Modify: `vdi/vdi_fill.c:690-850`
- Modify: `vdi/vdi_textblit.c:240-480`
- Modify: `vdi/vdi_mouse.c:873-1157`
- Modify: `vdi/vdi_backend.c`

**Interfaces:**
- Consumes: full ops table and current screen-workstation lookup.
- Produces: safe unsupported behavior for packed raster, contour fill, text blit, and software mouse operations.

- [ ] **Step 1: Add unsupported-dispatch tests**

Extend the backend host test with a packed workstation whose raster/text/mouse/seed slots are `NULL`. Assert each dispatch helper returns `FALSE` and a planar sentinel callback is never invoked.

- [ ] **Step 2: Run the test and verify it fails**

Run: `make test-vdi-backend`

Expected: unsupported helpers are missing or invoke the wrong fallback.

- [ ] **Step 3: Add explicit guards at public entry points**

Before entering format-specific internals:

- `vro_cpyfm()`, `vrt_cpyfm()`, `vr_trnfm()`, and Line-A raster paths require the selected raster op.
- contour/seed fill requires the selected seed-span op.
- text blit requires the selected text op for packed modes.
- software mouse save/draw/restore requires selected mouse ops for packed modes.

Planar descriptors retain current code. Packed descriptors with missing slots return/no-op according to the existing function contract and emit one concise `KINFO` trace per unsupported operation class. They never enter byte-packed or planar implementations.

- [ ] **Step 4: Run unsupported tests and full object compile**

Run:

```bash
make test-vdi-backend
make rpi4_defconfig && make obj/vdi_raster.o obj/vdi_fill.o obj/vdi_textblit.o obj/vdi_mouse.o
make floppy_defconfig && make obj/vdi_raster.o obj/vdi_fill.o obj/vdi_textblit.o obj/vdi_mouse.o
```

Expected: tests pass and both configurations compile.

- [ ] **Step 5: Commit safety gates**

```bash
git add vdi/vdi_backend.c vdi/vdi_raster.c vdi/vdi_fill.c \
    vdi/vdi_textblit.c vdi/vdi_mouse.c tools/test-vdi-backend.c
git commit -m "Block unsupported packed VDI operations"
git push
```

---

### Task 9: Switch Raspberry Pi to Validated 16bpp and Fix the BIOS Console

**Files:**
- Modify: `bios/raspi_mbox.h:128-140`
- Modify: `bios/raspi_screen.c:115-161,237-371`
- Modify: `bios/screen.c:410-425,560-575`
- Modify: `bios/raspi_screen.h:10-35`

**Interfaces:**
- Produces: a validated packed-16 Raspberry Pi framebuffer descriptor and 16-bit-safe cell rendering.
- Consumes: packed truecolor backend and safety gates from Tasks 5-8.

- [ ] **Step 1: Add compile-time mailbox and console expectations**

Change the framebuffer request structure to include `PROPTAG_SET_PIXEL_ORDER`, request depth 16 and RGB order 1, and change stored pitch to `ULONG`. Build before implementing response handling.

Run: `make rpi4_defconfig && make obj/raspi_screen.o`

Expected: compilation fails where old byte-oriented console code mixes the new 16-bit assumptions or new mailbox fields are undefined.

- [ ] **Step 2: Validate the accepted framebuffer**

After the mailbox call, store returned width, height, depth, pixel order, address, size, and pitch. Panic with a concise message if:

- mailbox call fails;
- address is zero;
- depth is not 16;
- returned order is not the requested RGB order;
- pitch is zero or below `width * 2UL`;
- framebuffer size is below `pitch * height`;
- descriptor validation fails.

Report `SCREEN_LAYOUT_PACKED`, `SCREEN_COLOR_TRUECOLOR`, and `SCREEN_PIXEL_RGB565`. Change `raspi_vgetmode()` from `VIDEL_8BPP` to `VIDEL_TRUECOLOR`. Stop programming the 8bpp framebuffer palette during normal screen initialization, but retain the palette table while the hardware cursor uses it.

- [ ] **Step 3: Convert BIOS console helpers to 16-bit pixels**

Use `UWORD *` for framebuffer cells. Required behavior:

- `raspi_cell_addr()`: x offset is `x * 8 * sizeof(UWORD)` bytes.
- `raspi_blank_out()`: fill each pixel with the packed background `UWORD`; do not use byte `memset()` for nonzero colors.
- `raspi_cell_xfer()`: write eight `UWORD` pixels per font row using packed foreground/background values.
- `raspi_neg_cell()`: invert all eight `UWORD` pixels on every character row, not one byte per row.

Add this static 16-entry 0-1000 VDI-component table and convert entries with `screen_mode_pack_color()`. Use it for early VT52 rendering; initialize the VDI physical workstation pseudo-palette from the same values so VT52 and VDI agree:

```c
static const UWORD default_vdi_rgb[16][3] = {
    { 1000, 1000, 1000 }, { 1000,    0,    0 },
    {    0, 1000,    0 }, { 1000, 1000,    0 },
    {    0,    0, 1000 }, { 1000,    0, 1000 },
    {    0, 1000, 1000 }, {  733,  733,  733 },
    {  533,  533,  533 }, {  667,    0,    0 },
    {    0,  667,    0 }, {  667,  667,    0 },
    {    0,    0,  667 }, {  667,    0,  667 },
    {    0,  667,  667 }, {    0,    0,    0 }
};
```

- [ ] **Step 4: Build both Raspberry Pi images**

Run:

```bash
make rpi2_defconfig && make
make rpi4_defconfig && make
```

Expected: `kernel7.img` and `kernel7l.img` are produced. Existing compiler warnings may remain; no new warnings originate in the touched files.

- [ ] **Step 5: Smoke-test RPi2 boot**

Run:

```bash
timeout 15s qemu-system-arm -M raspi2 -bios kernel7.img \
    -d guest_errors -serial stdio -display none
```

Expected: boot reaches normal serial output without a framebuffer validation panic. Timeout exit 124 is acceptable after successful boot output.

- [ ] **Step 6: Commit the Raspberry Pi mode switch**

```bash
git add bios/raspi_mbox.h bios/raspi_screen.c bios/raspi_screen.h bios/screen.c
git commit -m "Switch Raspberry Pi framebuffer to 16-bit color"
git push
```

---

### Task 10: Remove Routed Compile-Time Forks and Verify the Foundation

**Files:**
- Modify: `vdi/vdi_misc.c`
- Modify: `vdi/vdi_fill.c`
- Modify: `vdi/vdi_line.c`
- Modify: `readme.md`
- Modify: `docs/superpowers/specs/2026-08-03-vdi-backend-truecolor-design.md` only if implementation names differ from the approved spec.

**Interfaces:**
- Produces: a clean first-slice backend foundation and records remaining raster/text/mouse/palette work explicitly.

- [ ] **Step 1: Scan for stale routed branches**

Run:

```bash
rg -n 'CONF_CHUNKY_PIXELS' vdi bios include
```

Expected: no occurrences remain in pixel address/read/write, rectangle fill, or line implementations. Remaining occurrences are removed or converted to explicit backend guards in this task.

- [ ] **Step 2: Run all host tests**

Run:

```bash
make test-screen-mode
make test-vdi-backend
make test-vdi-truecolor
```

Expected: all exit 0.

- [ ] **Step 3: Build the platform matrix**

Run sequentially:

```bash
make rpi2_defconfig && make
make rpi4_defconfig && make
make amiga_defconfig && make
make floppy_defconfig && make
```

Expected: all four configured images build. RPi images use packed16; Amiga and Atari retain their actual planar descriptors; VIDEL truecolor descriptor code compiles into the Atari build.

- [ ] **Step 4: Run repository checks**

Run:

```bash
make gitready
git diff --check
git status --short --branch
```

Expected: gitready and whitespace checks pass; only intended documentation changes remain uncommitted.

- [ ] **Step 5: Update project status without overstating support**

Update `readme.md` to state that the backend foundation and initial packed16 primitives exist, while raster operations, text rendering/effects, software mouse, and complete pseudo-palette behavior remain follow-up work. Do not claim completed Raspberry Pi graphics until hardware validation succeeds.

- [ ] **Step 6: Commit verification documentation**

```bash
git add readme.md docs/superpowers/specs/2026-08-03-vdi-backend-truecolor-design.md
git commit -m "Document VDI truecolor foundation status"
git push
```

- [ ] **Step 7: Record hardware validation in PR #70**

Update the PR body with the exact build/test matrix and one of these factual statuses:

- hardware tested: board/model, returned depth/pitch/order, and observed primitive output;
- hardware not available: build/QEMU checks pass, visual correctness remains unverified.

Keep PR #70 draft until the implementation and available validation are complete.

---

## Follow-Up Plans

This plan intentionally leaves these independently reviewable projects for later plans under issue #35:

- portable C text helper backport and packed16 text rendering;
- packed truecolor raster copy/transform operations, coordinated with issue #5;
- packed truecolor software mouse save/draw/restore;
- complete per-workstation requested-color and pseudo-palette semantics;
- VIDEL truecolor mode enablement and ET4000/NOVA truecolor descriptor producers;
- multi-screen workstation selection.

## Self-Review

- Spec coverage: Tasks 1-4 implement descriptor-driven per-workstation backends and Line-A screen-workstation authority; Tasks 5-7 route the approved initial primitives; Task 8 enforces no planar fallback; Task 9 switches Raspberry Pi safely; Task 10 verifies and documents scope.
- Placeholder scan: no `TBD`, `TODO`, “similar to,” or unspecified error-handling steps remain.
- Type consistency: `SCREEN_MODE_DESC`, `VDI_BACKEND_OPS`, `VDI_BACKEND_STATE`, and lifecycle/pixel/fill/line signatures are introduced before their consumers and use pTOS fixed-width types.
- Scope: raster, text, mouse, full palette, and additional hardware modes are explicitly separated into follow-up plans rather than partially implemented here.
