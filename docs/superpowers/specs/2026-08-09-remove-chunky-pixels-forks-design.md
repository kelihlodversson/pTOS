# Remove Remaining CONF_CHUNKY_PIXELS Forks and v_planes != 8 Guards Design

## Context

Issue #35 converted the VDI's chunky-pixel work from a compile-time fork
(`CONF_CHUNKY_PIXELS`) into runtime dispatch through a VDI backend. The
packed-truecolor backend is now the only renderer the Raspberry Pi uses, and
the `#35` sub-issues removed the `v_planes != 8` corruption guards from
`vdi_mouse.c`, `vdi_line.c`'s `abline()`, and `vdi_fill.c`'s `end_pts()` once
those paths started dispatching through the backend instead.

Issue #95 finishes that cleanup. Two families of leftovers remain on `master`:

1. The `CONF_CHUNKY_PIXELS` compile-time option itself, plus the code forks it
   guards: `vdi_textblit.c` (`screen_blit()`'s `nextwrd`, `planar_text_blit()`'s
   packed-addressing branch), `vdi/arch/arm/vdi_tblit.c`'s `normal_blit()`
   chunky branch, and a stale comment in `vdi/vdi_fill.c`.
2. The `linea_vars.v_planes != 8` fail-safe guards in `bios/raspi_screen.c`'s
   BIOS text-console routines (`raspi_blank_out()`, `raspi_cell_xfer()`,
   `raspi_neg_cell()`), introduced by PR #71 as a stopgap.

The two families are different in kind. The `CONF_CHUNKY_PIXELS` forks are
**dead in every current configuration**: on Raspberry Pi (the only machine
that ever set the option) text output goes through the truecolor backend —
`screen_blit()` dispatches to `truecolor_text_blit()` / `backend->text_blit()`,
and neither reads `vars->nextwrd` — while `planar_text_blit()` and
`normal_blit()`'s planar branches are only reached on planar-only builds where
the option was forced off. Removing them is pure deletion.

The `raspi_screen.c` guards are different: they protect code that is still
genuinely 8bpp-only. The RPi framebuffer is always 16bpp truecolor
(`PROPTAG_SET_DEPTH` = 16, so `linea_vars.v_planes == 16` via `lineainit.c`),
and the three console routines write **one byte per pixel**. The guards
therefore skip every call, and the BIOS text console (boot banner, EmuCON,
panic output) currently renders *nothing* on RPi. Deleting the guards without
converting the routines would re-introduce the framebuffer corruption PR #71
stopped. So this issue's Part B converts the console to 16bpp-correct drawing
and then removes the guards.

## Goals

- Delete the `CONF_CHUNKY_PIXELS` Kconfig option and every code fork it
  guards; the symbol no longer appears anywhere in the tree.
- Remove all three `linea_vars.v_planes != 8` fail-safe guards in
  `bios/raspi_screen.c`.
- Make the BIOS text console actually draw on 16bpp RPi (boot banner, EmuCON,
  panic output) with ST-default-palette colors instead of rendering nothing.
- Keep the full configuration matrix building cleanly.

## Non-Goals

- No VDI behavior change: text rendering on every machine goes through the
  same code it goes through today, minus dead branches.
- No new test infrastructure; verification is the project's existing
  conventions (cross-compiled builds across the config matrix, a QEMU smoke
  test of an RPi image).
- No change to the truecolor backend's own palette handling, the mouse
  cursor's `vdi_truecolor_pixel_for_index()` usage, or anything outside the
  console routines and the removed forks.
- No 24/32bpp support in the console; RPi is fixed at 16bpp.

## Architecture

### Part A: remove CONF_CHUNKY_PIXELS

The option is forced `y` exactly when `MACHINE_RPI` (its Kconfig `default y
if MACHINE_RPI`), and no configuration selects it explicitly. Every RPi build
enables the truecolor backend (forced `y` on `MACHINE_RPI`), so `screen_blit()`
never reaches `planar_text_blit()` on a build where the option is on. The
converse holds on planar builds. All four forks are therefore unreachable in
every configuration:

- `vdi/Kconfig`: delete the `CONF_CHUNKY_PIXELS` config block. Nothing
  depends on it; nothing selects it.
- `vdi/vdi_textblit.c` `screen_blit()`: `vars->nextwrd` becomes unconditionally
  `vars->nbrplane * sizeof(WORD)`. The only reader of `nextwrd` is
  `normal_blit()` in `vdi/arch/arm/vdi_tblit.c`, which is reached only via
  `planar_text_blit()`; `truecolor_text_blit()` computes its own addressing.
- `vdi/vdi_textblit.c` `planar_text_blit()`: drop the `#if CONF_CHUNKY_PIXELS`
  arm (packed `dform`/`d_next` addressing); keep the planar body.
- `vdi/arch/arm/vdi_tblit.c` `normal_blit()`: drop the `#if CONF_CHUNKY_PIXELS`
  arm (the `nbrplane == 8 && nextwrd == sizeof(WORD)` byte-at-a-time glyph
  blit); keep the `nbrplane == 1 && nextwrd == 2` buffer blit and the `else`
  TODO. Flatten the outer `#else` braces into the function body.
- `vdi/vdi_fill.c`: reword `get_color()`'s NOTE comment to stop referencing
  `#if !CONF_CHUNKY_PIXELS` (the chunky branch in that file is long gone).

### Part B: 16bpp BIOS console on raspi_screen.c

The framebuffer is a packed RGB565 array (`raspi_screenbase` is a `UBYTE *`),
one pixel = `v_planes / 8` bytes. `v_col_fg`/`v_col_bg` hold ST default-palette
indices (0-15), set by `bios/vt52.c`. Convert the three routines to write
`UWORD` RGB565 values, translating the index through the existing
`raspi_dflt_palette[]`:

- Add a static helper that maps a palette index to RGB565, mirroring
  `rgb565_from_prgb()` in `vdi_backend_truecolor.c` (kept local to `bios/` so
  the BIOS stays independent of the VDI; the palette itself is shared).
- Fix `raspi_cell_addr()`'s x-stride from `x * 8` to `x * 8 * (v_planes / 8)`
  so a cell lands 16 bytes (not 8) into the row at 16bpp. `v_cel_wr` already
  carries the correct byte stride per cell row.
- `raspi_blank_out()`: clear a region of `UWORD`s with the background's RGB565
  value, keeping the existing full-width fast path (compare pixel width times
  2 against `raspi_screen_width_in_bytes`).
- `raspi_cell_xfer()`: glyph blit writing `UWORD` fg/bg pixels, with the
  row stride `v_lin_wr / sizeof(UWORD)` and the existing `M_REVID` fg/bg swap.
- `raspi_neg_cell()`: invert a `UWORD` per pixel across the full 8-pixel cell
  width per row (the current code toggles only the first byte of each row —
  a latent 8bpp-width bug fixed by the conversion), keeping the `M_CRIT`
  bracketing.
- Also fix the glyph mask in `raspi_cell_xfer()`: the original
  `(cel & (256>>pixel))` is an off-by-one (`256>>0` = `0x100` never matches
  a `UBYTE`, so pixel 0 always rendered background and bit 0 was never
  tested); the tree-wide MSB-first convention is `0x80 >> pixel`. Neither
  bug was observable while the 16bpp guard skipped every call.
- Delete the three `v_planes != 8` guards and their explanatory comments.

A side effect: the boot banner / EmuCON / panic text becomes visible on RPi for
the first time, in the ST default palette (background 0 = white, foreground
pen as chosen by vt52).

## Verification

1. Full config matrix: for every `configs/*_defconfig`, `make <name>_defconfig
   && make` must build clean. CI defines the matrix by architecture (see
   `.github/workflows/build.yml`); run it locally for all 27 configs with the
   installed `arm-none-eabi-` and `m68k-atari-mintelf-` toolchains.
2. QEMU smoke test of an RPi image (per `.claude/skills/ptos-smoketest`):
   `qemu-system-arm -M raspi2b -bios kernel7.img -d guest_errors -serial stdio`
   — no `guest_errors`, desktop renders.
3. `grep -rn CHUNKY .` outside `obj/` and `.git/` and `docs/superpowers/*`
   history: no matches. `grep -n "v_planes != 8"` : no matches.
4. `make gitready` before committing.
