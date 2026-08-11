# Truecolor-Correct Color Icon (CICON) Rendering Design

## Context

Issue #106 merged the base color icon (CICONBLK) support: 'new format' RSC
recognition, `fix_cicons()`, per-resolution `best_match()`, plane expansion,
`transform_cicon()`, and rendering through `gr_gicon()` -> `gr_colourblit()`.
Issue #88 merged raster-copy backend dispatch (`cpy_raster()` now routes through
`backend->raster_copy`). Both are on `master`.

Colour icons still do not render on truecolor screens (Raspberry Pi, packed
RGB565). The chain breaks before the backend is even reached:

1. On RPi, `gl_nplanes` = `v_planes` = 16 (`bios/lineainit.c`), so
   `transform_all_cicons()` expands the icon to 16 planes and
   `transform_cicon()` runs `vrn_trnfm()` to device-dependent form, producing
   16-plane interleaved words.
2. `gr_colourblit()` calls `vro_cpyfm(S_OR_D, ...)` with a 16-plane source
   MFDB (`fd_nplanes` = `cicon->num_planes` = 16). `setup_info()` computes
   `s_nxwd = 16 * 2 = 32` against the packed screen's `d_nxwd = 2`.
3. `cpy_raster()`'s opaque-path sanity check `if (info->s_nxwd !=
   info->d_nxwd) return;` (vdi_raster.c:909) rejects the blit. Nothing draws.

Upstream EmuTOS fixed this in two commits (`6df95d8e`, `1de2ecd2`) using
`phys_work.ext->palette` lookup and `planes > 8` checks. pTOS must not port
those verbatim: it never adopted `CONF_WITH_VDI_16BIT`/`phys_work.ext->palette`,
and its truecolor architecture deliberately replaced depth-based dispatch with
`vdi_backend_ops` selection. The palette pTOS does have is the per-workstation
truecolor pseudo-palette (`tc_palette[]`, indexed through
`vdi_truecolor_pixel_for_index()`), and the backend query is
`vdi_screen_is_truecolor()`.

A second problem is that none of this has ever been seen working. #106's PR
notes "no RSC file containing actual CICONBLK data was available to exercise
the new rendering path end-to-end", and the desktop builds its icons as
runtime-constructed mono `G_ICON` objects — nothing in the tree loads a CICON.
So the fix must ship with its own verification.

## Goals

- Colour icons render correctly (not corrupted, not blank/white) on packed
  truecolor screens (RPi).
- No `v_planes` / `planes > 8` depth checks introduced in AES icon-drawing
  code; the truecolor decision and the pixel conversion go through the backend
  abstraction.
- Planar (ST/TT/Falcon indexed) rendering path stays byte-identical.
- A config-gated test hook embeds a hand-built CICON RSC and renders it,
  so both truecolor and planar colour-icon rendering can be verified under
  QEMU/Hatari — now and as a regression test later.

## Non-Goals

- No Falcon-style 16-bitplane representation; pTOS truecolor is packed only.
- No change to the RSC parser or `expand_cicondata()` from #106 for the
  planar path.
- No change to the desktop's icon handling (it keeps using mono icons by
  default; colour icons are a separate slice of #105).
- No change to `truecolor_raster_copy()`'s generic opaque/transparent paths —
  the packed data produced by the transform flows through them as-is.
- No filesystem work; rpi2 has no block layer in any config, so the test RSC
  is embedded in the image and parsed in memory.

## Architecture

### Part A: the rendering fix

**A1. `transform_all_cicons()` (aes/gemrslib.c) gets a truecolor branch.**

The conversion must live here, not in `transform_cicon()`: that function only
sees the plane-expanded buffer and `gl_nplanes` (=16); the icon's original
plane count S (4 or 8) — needed to read the colour code back out — is only
known in the caller, from `cicon->num_planes` before it is overwritten.

Guarded by `#if CONF_WITH_VDI_BACKEND_TRUECOLOR` plus the runtime backend
query `vdi_screen_is_truecolor()`. For the selected CICON on a truecolor
screen:

- Read each pixel's colour code from the **original** S-plane standard-format
  `col_data` / `sel_data` (the RSC layout: plane-major, `w/16` words per
  plane-row, MSB-first within a word — consistent with the mono-icon and
  font conventions the tree already relies on).
- Write `w * h` packed `UWORD`s, each `vdi_truecolor_pixel_for_index(code)`.
  `tc_palette[256]` is fully seeded at workstation open
  (vdi_backend_truecolor.c:157), so all 0-255 codes are valid.
- Skip `expand_cicondata()` / `transform_cicon()` for this case.
- Set `cicon->num_planes = 1` so `gr_colourblit()`'s MFDB is one plane, the
  layout `truecolor_raster_copy()`'s opaque path reads (`s_nxwd == 2`).
- The conversion uses the **unmasked** colour codes and the blit replaces the
  whole icon box, so pixels outside the mask show the data's own background
  colour (the usual real-RSC case is a background that matches the desktop).
  This is the deliberate truecolor look; the mask's own blit in `gr_gicon()`
  and the selected-state dither fallback still draw on top as today.

The exact plane->code bit order is treated as a calibration constant: the
generator (Part B) emits the test icon in known colours, and the screendump
confirms or flips the interpretation. One boot iteration resolves it.

**A2. `vdi_colour_blit_mode()` (vdi/, declared in vdi_backend.h).**

With packed data the blit must **replace** pixels, but `gr_colourblit()` today
passes `S_OR_D`, whose packed meaning (`apply_raster_op` BM_S_OR_D = `src|dst`)
would OR RGB565 values — corruption. Planar needs `S_OR_D` (the data planes
are mask-ANDed and composed onto the mask blit). So the mode is a
backend-owned property, surfaced as a small helper:

- planar backend: `S_OR_D`
- packed truecolor backend: `BM_S_ONLY` (= 3, `D' = S`)

The helper is implemented in terms of `vdi_screen_is_truecolor()`, so the AES
never branches on depth or on a backend table itself.

**A3. `gr_colourblit()` (aes/gemgraf.c) uses the helper.**

`vro_cpyfm(vdi_colour_blit_mode(), ...)` instead of the hard-coded `S_OR_D`.
Everything downstream is unchanged: the packed data hits the existing opaque
word-at-a-time path in `truecolor_raster_copy()`.

The planar path is unaffected by A1 (branch compiled out on planar-only
builds, runtime-false under dispatch) and byte-identical in A3 (helper returns
`S_OR_D`).

### Part B: config-gated verification hook (`CONF_WITH_VDI_CICON_TEST`)

A new Kconfig option, default `n`, enabled in no shipped defconfig. When set:

**B1. RSC generator (tools/, host-side).** A small script emits the minimal
'new format' RSC binary for the test: one object tree with a single 32x32
`G_CICON`, one `CICONBLK` (mask + 4-plane colour data in 2-3 known colours),
with the exact byte layout the parser in gemrslib.c expects. It emits both a
flat RSC file (for the planar .prg test) and a C byte array (for embedding).

**B2. In-memory RSC load (aes/gemrslib.c).** Extract `rs_readit()`'s parsing
body (header read, size probe, allocation, `fix_trindex()` / `fix_cicons()` /
`fix_tedinfo()` / `fix_nptrs()`) into a shared entry that parses a buffer
already in memory. `rs_readit()` becomes a thin file-reading wrapper. This is
the only way to feed an RSC to the parser on rpi2, which has no filesystem.

**B3. Desktop hook (desk/deskmain.c).** After the desktop is up, load the
embedded RSC through the in-memory path and `objc_draw()` its `G_CICON` object
at a fixed position on the desktop background, so the normal
`just_draw()` -> `gr_gicon()` -> `gr_colourblit()` path runs. Failure (no
memory) logs and continues boot.

**B4. Planar end-to-end.** A small m68k `.prg` (minicrt + a minimal GEMDOS/AES
binding) loads the same RSC from a GEMDOS drive via the normal file-based
`rsrc_load()`, draws the G_CICON, and idles. Run under Hatari STE, screencap.
This validates the #106 planar path — never exercised before — and the
file-based loader.

## Verification

1. Full config matrix: every `configs/*_defconfig` builds clean with the
   installed `arm-none-eabi-` and `m68k-atari-mintelf-` toolchains. The test
   hook builds (and is off by default) in the configs that compile it.
2. rpi2 (`-M raspi2b -bios kernel7.img -d guest_errors -serial stdio`, per
   `.claude/skills/ptos-smoketest`), `CONF_WITH_VDI_CICON_TEST=y`: the
   screendump shows the test icon in its known colours; the calibrated
   colour-code bit order is recorded in the plan.
3. Hatari STE, planar: the `.prg` + RSC on a GEMDOS drive renders the same
   icon via the file-based path (screencap).
4. `grep -rn "v_planes" aes/gemrslib.c aes/gemgraf.c`: no new depth checks.
   `grep -rn "planes > 8" aes/`: no matches.
5. `make gitready` before committing.
