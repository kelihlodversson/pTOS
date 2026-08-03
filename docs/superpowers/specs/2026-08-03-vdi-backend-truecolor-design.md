# VDI Backend and Truecolor Design

## Context

Issue #35 tracks finishing the VDI chunky-pixel work and aligning pTOS with upstream EmuTOS truecolor VDI work. The current tree treats `CONF_CHUNKY_PIXELS` as a compile-time switch that forks shared VDI function bodies. On Raspberry Pi it means 8bpp packed indexed pixels, while Atari-style code remains interleaved bitplanes.

That shape does not scale. Pixel depth alone cannot distinguish planar 8bpp from packed 8bpp, and targets without planar framebuffers should not pretend they can use Atari planar primitives. The new direction is to make pixel layout and color model explicit runtime properties of a VDI workstation, then dispatch VDI primitives through a backend table.

The first new layout target is 16bpp packed truecolor. Raspberry Pi framebuffer initialization should request a 16bpp mode instead of preserving the existing 8bpp packed indexed mode. Atari hardware that exposes packed truecolor, such as Falcon VIDEL truecolor or graphics-card modes, should select the same truecolor backend family through its mode descriptor.

## Goals

- Introduce a VDI backend interface that covers the full VDI drawing surface.
- Store backend state per workstation.
- Treat Line-A as a backwards-compatibility layer over the current screen workstation.
- Make Atari planar code an Atari/planar backend, not a fallback for all targets.
- Move Raspberry Pi from 8bpp packed indexed framebuffer setup to 16bpp packed truecolor setup.
- Keep the first implementation slice reviewable by routing behavior incrementally while defining the full backend surface up front.

## Non-Goals

- Do not complete every VDI primitive in the first slice.
- Do not keep 8bpp packed Raspberry Pi mode as a required supported backend.
- Do not add multi-screen support yet.
- Do not backport the whole upstream EmuTOS VDI truecolor series in one step.
- Do not make any platform fall back to planar algorithms unless its descriptor reports a planar framebuffer.

## Architecture

Add an internal VDI backend layer. Each workstation has backend state containing a mode descriptor and a pointer to a `vdi_backend_ops` table.

The mode descriptor records:

- visible width and height
- bytes per line/pitch
- bits per pixel
- pixel layout
- color model
- truecolor component format
- backend capability flags

The first layout values are:

- `VDI_LAYOUT_PLANAR`: interleaved bitplanes for hardware that actually exposes planar framebuffers.
- `VDI_LAYOUT_PACKED_TRUECOLOR`: packed pixels, first implemented for 16bpp framebuffers.

The first color models are:

- indexed CLUT for planar modes.
- truecolor for packed modes.

Backends are selected from the mode descriptor. A mode is unsupported if no backend exists for its layout, color model, and bpp combination.

## Backends

### Planar Backend

The planar backend owns the existing interleaved-bitplane algorithms. It is selected only for descriptors that report a planar framebuffer. Current Raspberry Pi targets do not support planar framebuffers and must not select this backend.

### Packed Truecolor Backend

The packed truecolor backend is the supported path for packed truecolor framebuffers on any platform. The first concrete target is a 16bpp Raspberry Pi framebuffer, but Falcon VIDEL truecolor and Atari graphics-card modes should use the same backend family when their screen drivers report compatible descriptors. The backend should be written so RGB555 vs RGB565 can be represented in the descriptor instead of buried in call sites.

Raspberry Pi screen initialization changes its mailbox depth request from 8 to 16, records the returned pitch, and reports a packed truecolor descriptor. The existing fixed VideoCore 256-entry palette is not part of the normal truecolor path.

## Backend Operations

Define the full backend table from the start. Slots may initially be unimplemented, but the surface should be stable enough for later upstream and fVDI-inspired work.

The table includes operations for:

- pixel read and write
- rectangle and pattern fill
- arbitrary line and vertical line drawing
- seed-fill scanning support
- raster copy and transform hooks
- text blitting hooks
- mouse cursor save/draw hooks
- palette or pseudo-palette get/set hooks
- workstation or mode lifecycle hooks

A `NULL` operation means the backend does not provide that primitive. It does not mean “use planar code.” Call sites may use a fallback only when that fallback explicitly supports the same layout and color model. Otherwise the operation fails explicitly or no-ops with a trace, depending on the existing VDI contract for that primitive.

## Workstation State

Backend state is per workstation. The physical screen workstation is the authoritative object for the currently displayed framebuffer. Virtual workstations inherit compatible backend state from the physical workstation and can later carry their own pseudo-palette state.

The current `Vwk` layout is internal to the VDI and virtual workstations are allocated dynamically with `sizeof(Vwk)`. Store backend state directly in `Vwk` so the workstation remains the single source of truth.

## Line-A Compatibility

Line-A should not own backend or layout state. It is a backwards-compatibility layer over VDI.

For now there is only one screen. Line-A/global drawing calls resolve the current screen workstation and dispatch through that workstation's backend. Resolution or mode changes update or recreate the current screen workstation's descriptor and backend state.

Legacy Line-A fields remain compatibility fields populated from the current screen workstation descriptor:

- `v_planes` remains the legacy depth/count visible to old callers.
- `v_lin_wr` and `BYTES_LIN` come from the descriptor pitch.
- depth-sensitive cursor and console setup must not infer planar vs packed layout from platform or depth alone.

No separate active-backend global is part of the design. A later implementation may cache a derived pointer for speed, but the source of truth is the current screen workstation.

## Screen Mode Reporting

The existing `screen_get_current_mode_info(UWORD *planes, UWORD *hz_rez, UWORD *vt_rez)` API is too small. Add `screen_get_current_mode_desc()`, which reports layout, color model, bpp, pitch, width, and height.

The legacy API can remain as a wrapper for old callers while `linea_init()` and VDI backend selection move to the descriptor API.

Target behavior:

- Atari screen drivers report descriptors matching the actual hardware mode: planar indexed for ST-style modes, or packed truecolor for Falcon/graphics-card truecolor modes.
- Raspberry Pi reports packed 16bpp truecolor descriptors.
- Other targets must report a descriptor matching their actual framebuffer layout and color model; if no backend supports that descriptor, mode selection fails.

`linea_init()` calculates `v_lin_wr` and `BYTES_LIN` from descriptor pitch, not from `V_REZ_HZ / 8 * v_planes`.

## Palette and Color Mapping

Planar indexed modes keep hardware CLUT semantics.

Packed truecolor modes need a pseudo-palette so existing VDI color indexes still map to truecolor pixel values. The pseudo-palette should be per workstation, matching the direction of upstream EmuTOS truecolor work. `vs_color()` and `vq_color()` for truecolor backends update and query this pseudo-palette rather than programming an 8bpp framebuffer palette.

The first slice may initialize only the default mapping needed by existing colors, but the backend API should include palette hooks so full `vs_color()` semantics can be added without changing the interface.

## Data Flow

At boot, Raspberry Pi screen setup requests a 16bpp framebuffer and records the returned pitch. During Line-A/VDI initialization, screen code provides a descriptor. `linea_init()` fills legacy fields from that descriptor. `vdi_v_opnwk()` initializes the physical workstation, selects a backend from the descriptor, stores the backend state on the workstation, and makes that workstation the current screen workstation.

VDI calls with a `Vwk *` dispatch through that workstation's backend. Line-A/global calls first resolve the current screen workstation, then dispatch through its backend.

Virtual workstations opened through `vdi_v_opnvwk()` inherit compatible backend state and defaults. Closing a virtual workstation frees any backend-owned state associated with it.

Resolution changes update legacy Line-A fields and the current screen workstation descriptor/backend before VDI drawing resumes.

## Error Handling

Reject unsupported modes early. Examples include:

- descriptors that do not match the actual framebuffer layout
- Raspberry Pi framebuffer depth other than the implemented 16bpp mode
- zero pitch
- pitch smaller than `width * bytes_per_pixel`
- unknown truecolor component format
- no backend for a descriptor

Unsupported backend operations must be explicit. They may return an existing VDI/GEMDOS error where the caller supports errors, or no-op with a concise trace where the legacy contract has no error path. They must not silently draw invalid pixels and must not route through planar code unless the backend is planar.

## Implementation Slicing

The first implementation should establish the full backend interface and mode descriptor, switch Raspberry Pi to 16bpp truecolor, and route a small set of primitives through the backend so the design is exercised.

Recommended first routed primitives:

- pixel read/write
- start-address or pixel-address calculation
- rectangle fill
- simple line/vertical-line paths if they can be wrapped without changing behavior

Raster operations, text effects, mouse cursor drawing, and full palette semantics can then be implemented as follow-up slices against the same table.

## Testing

Build checks:

- `make rpi4_defconfig && make`
- `make rpi2_defconfig && make`
- one Atari or m68k configuration if the local toolchain is available
- `make gitready`
- `git diff --check`

Focused checks:

- Raspberry Pi requests 16bpp framebuffer depth.
- Raspberry Pi descriptor reports packed truecolor and mailbox pitch.
- `linea_vars.v_lin_wr` and `BYTES_LIN` use descriptor pitch.
- Planar descriptors select the planar backend.
- Packed truecolor descriptors select the packed truecolor backend, including Atari truecolor modes when available.
- Descriptors that do not match the actual framebuffer layout are rejected.
- `CONF_CHUNKY_PIXELS` no longer forks shared primitive bodies after conversion.

Runtime validation on real Raspberry Pi hardware is required before claiming visual correctness. QEMU can validate builds and some boot paths, but not all framebuffer behavior.

## Open Follow-Ups

- Backport upstream portable C text helper work.
- Port or adapt upstream/fVDI packed truecolor raster primitives.
- Implement full pseudo-palette semantics for truecolor workstations.
- Decide whether `CONF_CHUNKY_PIXELS` is removed outright or replaced by a clearer option such as `CONF_WITH_VDI_TRUECOLOR`.
- Extend the design for multiple screen workstations if pTOS gains multi-screen support.
