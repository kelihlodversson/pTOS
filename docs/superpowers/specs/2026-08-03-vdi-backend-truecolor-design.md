# VDI Backend and Truecolor Design

## Context

Issue #35 tracks finishing the VDI chunky-pixel work and aligning pTOS with
upstream EmuTOS's truecolor VDI work. The current tree treats
`CONF_CHUNKY_PIXELS` as a compile-time switch that forks shared VDI function
bodies: on Raspberry Pi it means 8bpp packed indexed pixels, while Atari-style
code stays interleaved bitplanes. That shape does not scale past one packed
depth, and pixel depth alone cannot distinguish planar 8bpp from packed 8bpp
(both report `v_planes == 8`).

Issue #35 already worked out the direction: pixel layout and color model
become explicit runtime properties of a VDI workstation, dispatched through a
backend table, the way upstream EmuTOS's `CONF_WITH_VDI_16BIT` work dispatches
on `TRUECOLOR_MODE` but generalized to cover pTOS's packed-8bpp case that
upstream's `v_planes > 8` predicate cannot express. fVDI (see issue #35 Part 4)
independently arrived at the same split: a mode is `{bpp, layout, format}`,
not depth alone.

This is a second attempt at this slice. A first attempt (PR #70, closed
without merging) built the same architecture but paired it with host-compiled
"unit tests" that linked freestanding kernel files (`vdi_backend.c`,
`bios/screen_mode.c`) against the host `cc`, faking a one-line `autoconf.h` to
satisfy `config.h`'s include. That doesn't fit this tree: pTOS is freestanding
with no libc and no host runtime, and `obj/autoconf.h` is a real
Kconfig-generated artifact tied to an actual `.config`, not something to
hand-fabricate. The architecture was sound; the verification approach wasn't
trusted, so the PR was aborted. This spec keeps the architecture and replaces
the testing approach.

## Goals

- Introduce a VDI backend interface covering the VDI drawing surface.
- Store backend state per workstation (in `Vwk`), not behind a separate global.
- Treat Line-A as a compatibility layer over the current screen workstation,
  not an owner of layout state.
- Make Atari planar code a backend for planar descriptors, not the implicit
  fallback for everything.
- Move Raspberry Pi from 8bpp packed indexed to 16bpp packed truecolor.
- Route a small, reviewable subset of primitives through the backend table to
  prove the shape, rather than converting every VDI primitive in one slice.
- Verify entirely through the project's existing conventions: cross-compiled
  builds across the configuration matrix and a manual QEMU smoke test — no
  new host-side test infrastructure.

## Non-Goals

- Do not complete every VDI primitive in this slice (raster ops, text
  effects, mouse cursor drawing, full palette semantics are follow-ups).
- Do not keep 8bpp packed Raspberry Pi mode as a required supported mode.
- Do not add multi-screen support.
- Do not backport the whole upstream EmuTOS VDI truecolor series in one step.
- Do not fall back to planar algorithms for a non-planar descriptor under any
  circumstance.
- Do not add host-compiled tests that link kernel source files, fabricate
  `autoconf.h`, or otherwise assume a libc/host runtime is available to
  freestanding code.

## Architecture

A workstation's backend state is a mode descriptor plus a pointer to a
`vdi_backend_ops` table, stored directly in `Vwk`.

**Mode descriptor** (`SCREEN_MODE_DESC`) records:

- visible width and height
- bytes-per-line (pitch)
- bits per pixel
- pixel layout: `SCREEN_LAYOUT_PLANAR` (interleaved bitplanes) or
  `SCREEN_LAYOUT_PACKED` (packed pixels)
- color model: indexed (CLUT) or truecolor
- truecolor component format (RGB565 first; the field exists so RGB555 and
  others aren't call-site literals later)

A descriptor is validated at construction: zero pitch, pitch smaller than
`width * bytes_per_pixel`, or an unrecognized truecolor format are all
rejected before anything tries to use the descriptor.

**Backend ops table** (`vdi_backend_ops`) covers: pixel read, pixel write,
pixel/start address calculation, rectangle fill, arbitrary line and vertical
line drawing, seed-fill scan support, raster copy/transform hooks, text blit
hooks, mouse cursor save/draw hooks, palette get/set hooks, and
open/clone/close lifecycle hooks. A `NULL` slot means "this backend does not
implement this primitive" — it is never interpreted as "fall back to planar."
A backend is selected only for descriptors whose layout/color-model/bpp
combination it actually supports; no descriptor is silently coerced onto an
incompatible backend.

## Backends

### Planar backend

Wraps the existing interleaved-bitplane VDI code, moved behind the ops table
without behavioral changes. Selected only for descriptors that report planar
layout. Current Raspberry Pi targets never produce a planar descriptor.

### Packed truecolor backend

New. The first concrete target is a 16bpp Raspberry Pi framebuffer (RGB565),
but the descriptor-driven shape means Falcon VIDEL truecolor or graphics-card
packed truecolor modes can select the same backend family later without a
new backend. Raspberry Pi screen init changes its mailbox depth request from
8 to 16, records the pitch the firmware actually returns, and reports a
packed-truecolor descriptor built from that. The existing fixed 256-entry
VideoCore palette table is not part of this path.

## Screen Mode Reporting

`screen_get_current_mode_info(planes, hz_rez, vt_rez)` is too small to carry
layout and color model. Add `screen_get_current_mode_desc()`, returning a
`SCREEN_MODE_DESC`. The legacy call remains as a thin wrapper for existing
callers; `linea_init()` and backend selection move to the descriptor API.
`linea_init()` computes `v_lin_wr`/`BYTES_LIN` from descriptor pitch instead
of the bitplane formula `V_REZ_HZ / 8 * v_planes`, which only happens to be
correct for packed 8bpp by coincidence.

## Line-A Compatibility

Line-A owns no backend or layout state of its own. There is currently one
screen; Line-A/global drawing calls resolve the current screen workstation
and dispatch through its backend. A resolution change updates or recreates
that workstation's descriptor and backend state before drawing resumes.
Legacy Line-A fields (`v_planes`, `v_lin_wr`, `BYTES_LIN`) remain, populated
from the current screen workstation's descriptor — old callers keep working,
but nothing infers layout from depth or platform.

## Palette and Color Mapping

Planar indexed modes keep existing hardware CLUT semantics unchanged. Packed
truecolor modes need a pseudo-palette so VDI color indexes still resolve to
truecolor pixel values; this is per-workstation, following upstream's
direction. This slice defines the palette hooks in the ops table so the
interface doesn't change later, but only initializes the default mapping
needed by colors already in use — full `vs_color()`/`vq_color()` semantics
for truecolor backends are a follow-up.

## Data Flow

At boot, Raspberry Pi screen setup requests a 16bpp framebuffer and records
the pitch the firmware returns. During Line-A/VDI init, screen code builds a
descriptor from that. `vdi_v_opnwk()` initializes the physical workstation,
selects a backend from the descriptor, stores backend state on the
workstation, and makes it the current screen workstation. VDI calls taking a
`Vwk *` dispatch through that workstation's backend; Line-A/global calls
resolve the current screen workstation first, then dispatch the same way.
Virtual workstations (`vdi_v_opnvwk()`) inherit compatible backend state;
closing one frees any backend-owned state it holds.

## Error Handling

Nothing silently draws garbage or silently falls through to planar code for a
non-planar descriptor:

- No backend matches a descriptor's layout/color-model/bpp → mode selection
  fails outright, at open time.
- Zero pitch, or pitch smaller than `width * bytes_per_pixel` → rejected when
  the descriptor is built.
- Raspberry Pi framebuffer depth other than the implemented 16bpp → rejected,
  not coerced to the nearest supported depth.
- Unrecognized truecolor component format → rejected.
- A `NULL` ops slot invoked for the current call → the existing VDI/GEMDOS
  error path where the caller has one, otherwise a concise `KDEBUG` trace and
  a no-op. Never a guess, never a route through an incompatible backend.

## Implementation Slicing

This slice establishes the full backend interface and mode descriptor,
switches Raspberry Pi to 16bpp truecolor, and routes a small set of
primitives through the backend so the design is actually exercised rather
than just declared:

- pixel read/write
- start-address / pixel-address calculation
- rectangle fill
- simple line/vertical-line paths, if wrapping them doesn't change behavior

Raster operations (`vro_cpyfm`/`vrt_cpyfm`/`vr_trnfm`, tracked separately as
#5), text effects, mouse cursor drawing, and full palette semantics are
follow-up slices against the same table, not part of this one.

## Testing

No host-compiled unit tests. Verification uses the project's existing
convention — the same one every other pTOS change is checked with — because
introducing new host-side test infrastructure for freestanding kernel code is
what went wrong last time:

- `make rpi2_defconfig && make`
- `make rpi4_defconfig && make`
- One Atari/m68k configuration (whichever cross toolchain is available
  locally) — this is the only check that exercises the planar backend at all,
  since nothing else in this slice touches non-RPi hardware
- `make gitready`
- `git diff --check`
- Manual QEMU smoke test per `doc/install.txt`/`CLAUDE.md`:
  `qemu-system-arm -M raspi2 -bios kernel7.img -d guest_errors -serial stdio`,
  confirming the RPi path reaches a 16bpp framebuffer and basic drawing
  doesn't crash or trip `guest_errors`
- Descriptor validation (bad pitch, unrecognized format, mismatched
  layout/backend) verified by code review of the validation logic, not by a
  host test binary

Explicitly out of scope for this PR to claim: real Raspberry Pi hardware
behavior. QEMU validates the boot path and that drawing doesn't crash, but not
all framebuffer/timing behavior — real-hardware visual correctness stays an
open follow-up.

## Open Follow-Ups

- Backport upstream's portable-C text helper rewrite (issue #35 Part 2a)
  — done in #86 (PR #104): the text blit now dispatches through the backend
  ops table like the other primitives, the ported C `outline()`/`rotate()`/
  `scale()` shared by all arches share run on ARM, and the ARM `normal_blit`
  1-plane scratch-buffer blit handles skew/thicken/outline. The shared
  `outline()` and the ARM 1-plane blit both now access their big-endian
  scratch words through endian-neutral accessors, so the ring walk and
  per-column mask writes are correct on little-endian ARM as well as m68k.
  (Independent of the earlier truecolor-backend slice, landed before it
  could regress ARM styled text.)
- Port or adapt upstream/fVDI packed-truecolor raster primitives (issue #5).
- Implement full pseudo-palette semantics for truecolor workstations.
- Decide whether `CONF_CHUNKY_PIXELS` is removed outright once the backend
  conversion is complete, or replaced by a clearer option such as
  `CONF_WITH_VDI_TRUECOLOR`. The chunky 8bpp `nbrplane==8` branch in
  `vdi/arch/arm/vdi_tblit.c` is now unreachable on truecolor backends (RPi
  renders text through the backend dispatch, not the chunky fallback) and
  can be removed when the option itself is retired in a later slice.
- Extend the design for multiple screen workstations if pTOS ever gains
  multi-screen support.
