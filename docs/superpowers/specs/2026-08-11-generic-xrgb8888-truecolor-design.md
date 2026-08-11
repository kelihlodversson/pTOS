# Generic XRGB8888 Truecolor Backend Design

## Context

Issue #91: the VDI truecolor backend is hardwired to one packed pixel format,
RGB565 (16 bpp). The next target machine (virt-arm framebuffer, issue #68)
wants 32 bpp XRGB8888. Today the whole chain assumes UWORD pixels:

- `vdi_backend_ops`' raw-pixel slots and the backend primitives are `UWORD`
  (vdi/vdi_backend.h:39-40); `Vwk.tc_palette[256]` is `UWORD`
  (vdi/vdi_defs.h:228); `vdi_truecolor_pixel_for_index()` returns `UWORD`
  (vdi/vdi_backend.h:142, include/gsxdefs.h:146).
- `vdi_raster.c setup_info()` hardcodes 2 bytes per pixel in its truecolor
  branches (lines 822, 836-838, 860-880, 888-890).
- The software mouse cursor has a `#ifdef MACHINE_RPI` 16-bit truecolor path
  and a planar fallback that is wrong for a packed 32 bpp screen
  (vdi/vdi_mouse.c:874-885, 900, 925-926).
- The AES colour-icon packers write UWORDs (aes/gemrslib.c:298, 318, 334).
- `bios/screen_mode.c screen_mode_desc_valid()` accepts RGB565 only;
  `vdi_backend_select()` (vdi/vdi_backend.c:17-37) selects a backend for
  `SCREEN_PIXEL_RGB565` only.

The issue asks for: a real `SCREEN_PIXEL_XRGB8888` pixel format that passes
`screen_mode_desc_valid()`, a shared truecolor backend that draws both RGB565
and XRGB8888 driven by the screen's pixel format, and — once a machine reports
an XRGB8888 descriptor — working VDI drawing with no per-machine drawing code.
`DRM_FORMAT_XRGB8888` = `fourcc_code('X','R','2','4')` = `0x34325258`; QEMU
maps it to in-memory byte order B,G,R,X on little-endian.

Prior art: the backend-dispatch architecture (PR #71), the truecolor backend
(PR #88, #89), and the colour-icon rendering fix (#106 / #88 raster_copy
dispatch). pTOS's truecolor is packed-only; it deliberately does not use
depth-based dispatch or `phys_work.ext->palette`.

## Goals

- `SCREEN_PIXEL_XRGB8888` is a real pixel format, validated in
  `screen_mode_desc_valid()`.
- The shared truecolor backend draws 16 bpp RGB565 and 32 bpp XRGB8888; the
  active format comes from the screen mode descriptor, never from per-machine
  code.
- A machine reporting an XRGB8888 descriptor gets working VDI drawing with no
  per-machine drawing code.
- No regression on m68k targets: RGB565 dispatch (atari512-dispatch), planar
  (ST/TT/Falcon), and the RPi keep working; code size unchanged when the 32 bpp
  wrapper is not enabled.
- A config-gated test hook on virt-arm (no display hardware) reports an
  XRGB8888 descriptor backed by guest RAM, so 32 bpp drawing can be verified
  under QEMU today and later replaced by the real ramfb driver (#68).

## Non-Goals

- No new pixel formats beyond XRGB8888. A future format (e.g. 24 bpp) will
  revisit the packing dispatch.
- No mode switching or Setscreen support for 32 bpp; there is exactly one
  screen, as today.
- No per-machine drawing code. The virt-arm test hook only supplies the
  descriptor and a RAM framebuffer.
- No change to planar rendering paths.
- No change to the existing single-renderer direct-call structure: with
  exactly one renderer the primitives are still called directly (see
  vdi/build.mk comment), and RGB565-only builds do not pull in the 32 bpp
  wrapper.

## Architecture

### Part A: pixel-format interface

**A1. `include/screen_mode.h`: add `SCREEN_PIXEL_XRGB8888` (= 2).**

**A2. `bios/screen_mode.c screen_mode_desc_valid()`:** XRGB8888 requires 32
bpp and `pitch == width * 4`, mirroring the RGB565 check.

**A3. Widen the raw-pixel interface to `ULONG`:**

- `vdi_backend_ops` `get_raw_pixel`/`put_raw_pixel` become `ULONG`
  (vdi_backend.h:39-40).
- `Vwk.tc_palette[256]` becomes `ULONG` (vdi_defs.h:228): it stores the
  active-format packed value; RGB565 occupies the low 16 bits. Cost: +512 B
  per workstation.
- `vdi_truecolor_pixel_for_index()` returns `ULONG` (vdi_backend.h:142,
  include/gsxdefs.h:146). It reads `tc_palette[index]` and is therefore
  format-agnostic once seeded.
- Add `UWORD pixel_size` (2 or 4) to `vdi_backend_ops`. `vdi_backend_ops_init()`
  uses it for the generic-defaults raw XOR mask, replacing the hardcoded
  `^ 0xffff` with `^ ((1UL << (ops->pixel_size * 8)) - 1)` (vdi/vdi_backend.c).
  `pixel_size` is also the shared size for stride math and the mouse cursor.

**A4. `vdi_control.c:430`:** `INQ_TAB[5]` (colour-LUT support) also reports 0
for `INQ_TAB[4] == 32` (direct colour, no LUT), like the existing `== 16`.

### Part B: shared template + two wrappers

**B1. New `vdi/vdi_backend_truecolor_tmpl.c`** — not an object in build.mk;
`#include`d by both wrappers (the fVDI technique). Holds the
pixel-parameterized drawing code as `static` functions written against the
macros `PIXEL`, `PIXEL_SIZE`, `PACK_PIXEL`, `UNPACK_PIXEL`: get_start_addr,
get_pixel, put_pixel, get_raw_pixel, put_raw_pixel, fill_rect, text_blit,
raster_copy, draw_line, search_left, search_right. The `d_nxwd != 2` /
`s_nxwd != 2` guards in the raster-copy path become `PIXEL_SIZE` comparisons.

**B2. `vdi/vdi_backend_truecolor.c`** (RGB565 wrapper, same object name):
`#define PIXEL UWORD` / `#define PIXEL_SIZE 2` / `#define PACK_PIXEL(v) \
rgb565_from_prgb(v)` and includes the template. It keeps **all shared state**:
`active_vwk`, `physical_vwk_seeded`, `default_prgb_palette[]`, and the
format-independent exports (`vdi_truecolor_pixel_for_index`,
`vdi_backend_active_vwk`/`set_active_vwk`, `vdi_truecolor_screen`). It defines
`packed_truecolor_backend_ops`.

**B3. New `vdi/vdi_backend_truecolor32.c`** (XRGB8888 wrapper): `#define
PIXEL ULONG` / `#define PIXEL_SIZE 4` / `#define PACK_PIXEL(v) ((v) |
0xff000000UL)` (the existing `0x00BBGGRR` palette data is already in DRM
XRGB8888 layout, missing only the alpha byte) and includes the template. It
defines `packed_truecolor32_backend_ops`. No shared state: everything lives in
the RGB565 wrapper's object, which is always built when `CONF_WITH_VDI_BACKEND_TRUECOLOR`
is set.

**B4. New Kconfig `CONF_WITH_VDI_BACKEND_TRUECOLOR32`:** `depends on
CONF_WITH_VDI_BACKEND_TRUECOLOR && CONF_WITH_VDI_BACKEND_DISPATCH`, default n.
vdi/build.mk: `obj-$(CONF_WITH_VDI_BACKEND_TRUECOLOR32) += vdi_backend_truecolor32.o`.

**B5. `vdi_backend_select()` (vdi/vdi_backend.c):** `SCREEN_PIXEL_XRGB8888`
selects `packed_truecolor32_backend_ops` when `CONF_WITH_VDI_BACKEND_TRUECOLOR32`,
else NULL (no backend). Declare `extern vdi_backend_ops
packed_truecolor32_backend_ops` in vdi_backend.h under dispatch.

### Part C: palette packing (single-copy, runtime dispatch on v_planes)

`linea_init()` sets `linea_vars.v_planes = desc.bits_per_pixel`
(bios/lineainit.c:46), so it is already 16 (RGB565) or 32 (XRGB8888) for a
truecolor screen. The single-copy palette functions in
`vdi_backend_truecolor.c` pick the pack format at runtime:

- `vdi_truecolor_init_palette()` seeds `tc_palette[]` via `PACK_PIXEL`-equivalent
  logic keyed on `v_planes == 32`; `tc_req_col[]` is unchanged (VDI 0-1000 scale,
  format-independent).
- `vdi_truecolor_set_color()` / `vdi_truecolor_get_color()` pack/unpack keyed
  the same way (vdi_col.c:643, 816 call them unchanged).
- `pixel_size = linea_vars.v_planes / 8` is the shared size helper, exposed as
  `vdi_truecolor_pixel_size()` (declared in vdi_backend.h).

This is preferred over routing the palette through `vdi_backend_ops` slots:
XRGB8888 is the only new format, bpp implies the packing, and the call sites
(vdi_control.c:302, vdi_col.c:643/816) stay untouched. If a future format makes
bpp insufficient to choose the packing, move these into the ops table then.

### Part D: software mouse cursor

`vdi_mouse.c` replaces the `#ifdef MACHINE_RPI` compile-time branch with a
runtime `vdi_screen_is_truecolor()` check (the truecolor cursor path, now
pixel-size-aware) vs. the planar path. `mouse_save.buffer[16*16]` becomes
`ULONG` and is gated on `CONF_WITH_VDI_BACKEND_TRUECOLOR` (not MACHINE_RPI);
`cdb_fg`/`cdb_bg` take the `ULONG` from `vdi_truecolor_pixel_for_index()`. The
`MCS_LONGS` planar handling is unchanged.

### Part E: AES colour icons

- `pack_planes()` (aes/gemrslib.c:298) writes `ULONG` pixels (from
  `vdi_truecolor_pixel_for_index()`).
- `pack_cicon()` (aes/gemrslib.c:334) allocates `pixels * sizeof(ULONG)` when
  the screen is 32 bpp, via `vdi_truecolor_pixel_size()`.
- `gr_colourblit()` (aes/gemgraf.c:696) is unchanged: it routes through
  `vro_cpyfm`, and `setup_info()` (Part F) picks up the pixel size.

### Part F: `setup_info()` stride math

`vdi_raster.c setup_info()`'s truecolor branches replace the hardcoded
`2` (s_nxwd, d_nxwd) and `fd_w * 2` (packed s_nxln) with `v_planes / 8` and
`fd_w * (v_planes / 8)`.

### Part G: virt-arm test hook

- **New Kconfig `CONF_WITH_VDI_TRUECOLOR32_TEST`:** `depends on MACHINE_VIRT_ARM
  && CONF_WITH_VDI_BACKEND_TRUECOLOR32`, default n — mirrors the CICON_TEST
  test-hook pattern (desk/Kconfig:83, desk/cicontest_rsc.c).
- **New `bios/machine/virt-arm/screen.c`:** `virt_arm_screen_init()` allocates a
  guest-RAM 640x480x32 framebuffer (~1.2 MB); `virt_arm_get_current_mode_desc()`
  reports XRGB8888, width 640, height 480, pitch 2560 (fits UWORD).
- `bios/screen.c` `screen_get_current_mode_desc()` (line 770) and
  `screen_init_address()` (line 577) gain a MACHINE_VIRT_ARM test branch (the
  default path ballocs only the ~32 KB shifter size — too small for 32 bpp).
- The framebuffer is never displayed (virt-arm has no video); the smoke test
  proves boot-to-desktop plus correct 32 bpp drawing into RAM. Issue #68 later
  replaces the RAM buffer with the real ramfb driver.

### Part H: verification

New `configs/virt-arm-tc32_defconfig` (from virt-arm + TRUECOLOR,
TRUECOLOR32, TRUECOLOR32_TEST). Smoke test (ptos-smoketest): QEMU virt-arm
boots to the GEM desktop; optionally assert `v_planes == 32` in the boot log.
Regression: rpi1, rpi2, atari512-dispatch, cartridge all still build;
atari512-dispatch still exercises the RGB565 dispatch path byte-for-byte
identical to today (TRUECOLOR32 defaults off).

## File Changes

New:
- vdi/vdi_backend_truecolor_tmpl.c
- vdi/vdi_backend_truecolor32.c
- bios/machine/virt-arm/screen.c
- configs/virt-arm-tc32_defconfig

Modified:
- include/screen_mode.h, bios/screen_mode.c
- vdi/vdi_backend.h, vdi/vdi_backend.c
- vdi/vdi_backend_truecolor.c
- vdi/vdi_defs.h, vdi/vdi_raster.c, vdi/vdi_mouse.c, vdi/vdi_control.c
- vdi/Kconfig, vdi/build.mk, Kconfig.machine
- aes/gemrslib.c
- bios/screen.c
- include/gsxdefs.h

## Risks

- The shared-state split (all in the RGB565 wrapper) assumes `vdi_backend_truecolor.c`
  is always built when `CONF_WITH_VDI_BACKEND_TRUECOLOR` is set — true today and
  preserved by B4's `depends on`. A future 32-bpp-only single-renderer build
  would need to relocate shared state; explicitly out of scope.
- Text blit on ARM reaches the truecolor pixel conversion via the template's
  `text_blit`; the m68k Line-A path (bios/arch/m68k/linea.S) calls
  `vdi_truecolor_pixel_for_index()` directly — its UWORD storage truncates the
  ULONG harmlessly because m68k truecolor is always RGB565. Verify at
  implementation that ARM text output never pokes UWORD pixels directly.
- `tc_palette` widening adds 512 B per workstation (mxalloc'd Vwks on ST-RAM);
  negligible but noted.
