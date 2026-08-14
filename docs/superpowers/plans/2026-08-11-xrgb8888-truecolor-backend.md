# XRGB8888 (32 bpp) Truecolor VDI Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the VDI's packed-truecolor backend draw 32 bpp XRGB8888 pixels, driven by the screen mode descriptor (issue #91). The RGB565 backend becomes one instantiation of a shared pixel-parameterized template; a new 32 bpp instantiation (`vdi_backend_truecolor32.o`) draws XRGB8888. A config-gated test hook on QEMU virt-arm (which has no video hardware) reports an XRGB8888 descriptor backed by guest RAM, so the 32 bpp path is exercised under QEMU today; the real ramfb driver (issue #68) replaces the RAM buffer later.

**Architecture:** A template file `vdi/vdi_backend_truecolor_tmpl.c` holds the whole drawing implementation (`get_start_addr`, `get/put_pixel`, `get/put_raw_pixel`, `fill_rect`, `text_blit`, `raster_copy`, `draw_line`, `search_right/left`) as `static` functions parameterized by two macros — `PIXEL` (UWORD or ULONG) and `PIXEL_SIZE` (2 or 4) — and is `#include`d by both wrappers (the fVDI technique, per the design). The RGB565 wrapper keeps **all shared state** (`default_prgb_palette[]`, `active_vwk`/`physical_palette_seeded`, and the format-independent exports), defines `packed_truecolor_backend_ops` from the template's statics, and provides thin extern forwarding stubs under `#if !CONF_WITH_VDI_BACKEND_DISPATCH` for the nine direct-call names (single-renderer builds call the primitives directly, see vdi/build.mk). The 32 bpp wrapper has no shared state, just `#define PIXEL ULONG / PIXEL_SIZE 4`, the template, and `packed_truecolor32_backend_ops`. The active format is chosen at runtime in `vdi_backend_select()` from `mode->pixel_format` (`SCREEN_PIXEL_XRGB8888`). Colour→pixel conversion is already format-agnostic once `tc_palette[]` (widened to `ULONG`) holds the active-format packed value; the palette pack/unpack functions pick RGB565 vs XRGB8888 at runtime keyed on `linea_vars.v_planes == 32`. The three depth-sensitive consumers (software mouse cursor, `setup_info()` stride math, AES colour-icon packers) stop hard-coding 2 bytes/pixel.

**Tech Stack:** GNU make + kconfiglib, GCC C90 (`-std=gnu90`) for `arm-none-eabi-` and `m68k-atari-mintelf-`. Verification via QEMU (`raspi1ap`/`raspi2b`/`virt` ARM) and Hatari, per `.claude/skills/ptos-smoketest/SKILL.md`.

## Global Constraints

- C90 with GNU extensions (`-std=gnu90`): declarations at the top of a block; `/* */` comments. 4 spaces, never a hard tab. Run `make gitready` before committing.
- **`int` is 16 bits on m68k** (`-mshort`). Use `WORD`/`LONG`/`UBYTE`/`UWORD`/`ULONG`; suffix constants that must survive on m68k. `linea_vars.v_planes` is a 16-bit value — a `v_planes / 8` result (2 or 4) is always small, never `LONG`.
- `-Wundef` is on: every `#if` symbol must be defined. Feature symbols are always defined `0`/`1` and tested with `#if`; new `CONF_WITH_*` options are plain feature symbols. Never edit `obj/autoconf.h` / `obj/auto.conf`.
- `-Wmissing-prototypes` is on: every new non-static function needs a prototype in a header the defining TU includes.
- `-Wall` is on (no `-Werror`), but the tree must stay warning-clean. In particular `rpi2-sparse_defconfig` (`CONF_VDI_SPARSE_TABLE=y`) leaves the template's six optional-slot functions unreferenced — they must not warn as unused statics.
- The design doc is `docs/superpowers/specs/2026-08-11-generic-xrgb8888-truecolor-design.md`. **Five corrections to it are folded into this plan** (see Task 4, Task 8, Task 9):
  1. The new virt-arm test file cannot be `bios/machine/virt-arm/screen.c` — that basename collides with `bios/screen.c` (objects land flat in `obj/`; CLAUDE.md requires unique basenames). It is `bios/machine/virt-arm/virt_screen.c` → `obj/virt_screen.o`.
  2. `CONF_WITH_VDI_TRUECOLOR32_TEST` must force `EXTENDED_PALETTE` on in vdi_col.c: with `v_planes == 32`, `DEV_TAB[13]` becomes 256, and `init_colors()` (vdi_col.c:711) writes `REV_MAP_COL[MAP_COL[i]]` for i up to 255 — an out-of-bounds write when `MAXCOLOURS` is 16 (the non-VIDEL/TT/RPI case). The design missed this; it is a hard requirement, not optional.
  3. The pixel_size-aware XOR mask in `vdi_backend_ops_init()` defaults cannot be `(1UL << (ops->pixel_size * 8)) - 1`: for `pixel_size == 4` the shift is 32 on a 32-bit `ULONG`, which is UB. Use an explicit 0xffffUL/0xffffffffUL choice.
  4. `vdi_screen_is_truecolor()`'s comparison against `packed_truecolor32_backend_ops` must be guarded by `#if CONF_WITH_VDI_BACKEND_TRUECOLOR32`, or every dispatch build without the 32 bpp wrapper fails to link (reference to a never-defined extern).
  5. The `vdi_v_opnwk` KDEBUG (vdi_control.c:453) does **not** print in stock builds — `ENABLE_KDEBUG` is commented out (vdi_control.c:18) and KDEBUG is a no-op unless it is locally defined. The "32 bpp screen active" boot-log signal therefore comes from (a) the stock `VDI video mode = 640x480 32-bit` KINFO line (gemgraf.c:436, unconditional on HAS_KPRINTF) and (b) an unconditional `kprintf` in the test hook. Do not touch `ENABLE_KDEBUG`.
- Regression bar: with `CONF_WITH_VDI_BACKEND_TRUECOLOR32` off (every config except the new test config), the drawing code paths are byte-identical to today. Where a task necessarily changes code layout (Task 4's template split) the image size may shift by a few tens of bytes in single-renderer truecolor builds (nine tiny forwarding thunks) and **zero** in dispatch builds; verify by comparing `stat -c %s` of the built image before/after where a task claims neutrality.
- Verification before completion: build the affected configs and run the smoke tests listed in each task; report real output, not assumptions. `make` rebuilds everything on a `.config` change; switching configs is intended.

---

### Task 1: `SCREEN_PIXEL_XRGB8888` + descriptor validation (design A1/A2)

Make XRGB8888 a real pixel format that passes `screen_mode_desc_valid()`. No drawing code yet.

**Files:**
- Modify: `include/screen_mode.h` — add the pixel-format constant
- Modify: `bios/screen_mode.c` — accept it in `screen_mode_desc_valid()`

- [ ] **Step 1: the constant**

In `include/screen_mode.h`, after `SCREEN_PIXEL_RGB565` (line 19), add:

```c
#define SCREEN_PIXEL_XRGB8888  2   /* 8 red / 8 green / 8 blue / 8 ignored bits, packed into a ULONG */
```

- [ ] **Step 2: validation**

In `bios/screen_mode.c`, in the `SCREEN_COLOR_TRUECOLOR` switch (line 38), add a `SCREEN_PIXEL_XRGB8888` case mirroring the RGB565 one:

```c
        case SCREEN_PIXEL_XRGB8888:
            /*
             * Mirror of the RGB565 check: vdi_backend_select() picks the
             * 32 bpp backend off pixel_format alone, so a descriptor
             * claiming XRGB8888 with the wrong bits_per_pixel would drive
             * 4-byte address arithmetic against a buffer that isn't 32 bpp.
             * Reject the mismatch here instead.  The 32 bpp backend does
             * ULONG loads/stores per pixel and relies on every scanline
             * starting 4-byte aligned.
             */
            if (desc->bits_per_pixel != 32)
                return FALSE;
            if (desc->pitch & 3)
                return FALSE;
            break;
```

**Verification:**
- `make rpi2_defconfig && make` and `make atari512_defconfig && make` build clean (screen_mode.c is compiled only under `CONF_WITH_VDI_BACKEND_DISPATCH`, bios/build.mk:28 — rpi2 (no dispatch) compiles nothing new; atari512 (no dispatch) likewise; rpi2-sparse/atari512-dispatch build the new case).
- `make rpi2-sparse_defconfig && make` and `make atari512-dispatch_defconfig && make` build clean, `stat -c %s` of `kernel7.img` / `ptos512k.img` unchanged from a pre-change build (saved first).
- No `-Wunused-*` or missing-prototype warnings.

---

### Task 2: widen the raw-pixel interface + `pixel_size` + `INQ_TAB[5]` (design A3/A4)

Widen the whole raw-pixel/palette interface from `UWORD` to `ULONG` so it can carry 32 bpp values, add the `pixel_size` field to `vdi_backend_ops`, make the generic-default XOR masks pixel-size aware, and report no colour LUT for a 32 bpp screen. Everything here must be behavior-neutral on RGB565/planar.

**Files:**
- Modify: `vdi/vdi_backend.h` — raw slots to `ULONG`, add `pixel_size`
- Modify: `vdi/vdi_defs.h` — `Vwk.tc_palette[256]` to `ULONG`
- Modify: `include/gsxdefs.h` — `vdi_truecolor_pixel_for_index()` to `ULONG`
- Modify: `vdi/vdi_backend_truecolor.c` — raw-pixel pair to `ULONG`, `pixel_size: 2` in the ops table, public `vdi_truecolor_pixel_for_index()` returns `ULONG`
- Modify: `vdi/vdi_backend_planar.c` — add ULONG raw-pixel wrappers, `pixel_size: 2` in the ops table
- Modify: `vdi/vdi_backend.c` — XOR masks in the generic defaults use `pixel_size`
- Modify: `vdi/vdi_control.c` — `INQ_TAB[5]` for 32 bpp

- [ ] **Step 1: `vdi_backend_ops` struct (vdi/vdi_backend.h:39-40)**

Change the raw-pixel slots to `ULONG`, and append `pixel_size` as the last member (after `search_left`, line 62):

```c
    ULONG (*get_raw_pixel)(WORD x, WORD y);
    void (*put_raw_pixel)(WORD x, WORD y, ULONG raw);
```

```c
    /*
     * Bytes per packed pixel (2 for RGB565, 4 for XRGB8888).  Used by
     * vdi_backend_ops_init()'s generic defaults to compute a raw XOR mask
     * that covers one whole pixel.  Mandatory, always set by the table.
     */
    UWORD pixel_size;
} vdi_backend_ops;
```

Update the struct-head comment (lines 31-38) to say "raw pixel access" instead of "raw framebuffer word access" and that a raw value is `ULONG`, the active-format packed pixel.

- [ ] **Step 2: `tc_palette` widening (vdi/vdi_defs.h:228)**

```c
    ULONG tc_palette[256];
```

Update the comment (lines 220-227): the palette stores the active-format packed pixel value; RGB565 occupies the low 16 bits.

- [ ] **Step 3: AES-facing prototype (include/gsxdefs.h:146)**

```c
ULONG vdi_truecolor_pixel_for_index(WORD index);
```

- [ ] **Step 4: truecolor backend raw pair + table (vdi/vdi_backend_truecolor.c)**

At lines 975-983:

```c
static ULONG truecolor_get_raw_pixel(WORD x, WORD y)
{
    return (ULONG)*truecolor_get_start_addr(x, y);
}

static void truecolor_put_raw_pixel(WORD x, WORD y, ULONG raw)
{
    *truecolor_get_start_addr(x, y) = (UWORD)raw;
}
```

(The low 16 bits carry the RGB565 pixel; the cast is the pre-template spelling — Task 4 makes these `PIXEL`.) Append `2,` to `packed_truecolor_backend_ops` (line 1016). Change the public `vdi_truecolor_pixel_for_index()` (line 284) to return `ULONG` (its body already returns `physical_vwk_seeded()->tc_palette[index]`, now a `ULONG`).

- [ ] **Step 5: planar backend raw pair + table (vdi/vdi_backend_planar.c)**

The planar table currently aliases the raw slots to `planar_get_pixel`/`planar_put_pixel` (lines 30-31), which return/take `UWORD` — incompatible with the widened `ULONG` slots. Add two static wrappers and use them:

```c
/*
 * ULONG raw-pixel adapters for the ops table: a planar pixel's raw value
 * is its composed colour index, which fits a UWORD; widen to the ops
 * interface's ULONG on the way in/out.
 */
static ULONG planar_get_raw_pixel(WORD x, WORD y)
{
    return (ULONG)planar_get_pixel(x, y);
}

static void planar_put_raw_pixel(WORD x, WORD y, ULONG raw)
{
    planar_put_pixel(x, y, (UWORD)raw);
}
```

Replace lines 30-31 with `planar_get_raw_pixel,` / `planar_put_raw_pixel,` and append `2,` to the table.

- [ ] **Step 6: pixel-size-aware XOR masks (vdi/vdi_backend.c)**

The generic defaults XOR with `^ 0xffff` (lines 86, 146, 222, 297, 315). Replace with a mask that covers one whole pixel of the selected backend. Add a helper and use it (each default already has `const vdi_backend_ops *ops = vdi_screen_backend();`):

```c
/*
 * Raw XOR mask covering one whole pixel of the selected backend.  Not
 * (1UL << (pixel_size * 8)) - 1: for pixel_size 4 that shifts a 32-bit
 * ULONG by 32, which is undefined.
 */
static ULONG raw_xor_mask(const vdi_backend_ops *ops)
{
    return (ops->pixel_size == 4) ? 0xffffffffUL : 0xffffUL;
}
```

Then in `default_fill_rect` (line 86), `default_text_blit` (line 146), `default_raster_copy` (line 222) and both branches of `default_draw_line` (lines 297, 315):

```c
    ops->put_raw_pixel(x, y, ops->get_raw_pixel(x, y) ^ raw_xor_mask(ops));
```

`default_raster_copy`'s opaque path (line 261) keeps `apply_raster_op()` (UWORD semantics) — the truecolor backends provide their own raster_copy, so the generic opaque copy only ever runs for the planar backend, where a UWORD apply is correct.

- [ ] **Step 7: `INQ_TAB[5]` for 32 bpp (vdi/vdi_control.c:430)**

```c
    /* Indicate whether LUT is supported */
    if ((linea_vars.INQ_TAB[4] == 16) || (linea_vars.INQ_TAB[4] == 32)
        || (get_monitor_type() == MON_MONO))
        linea_vars.INQ_TAB[5] = 0;
    else linea_vars.INQ_TAB[5] = 1;
```

**Verification:**
- Build `rpi2_defconfig`, `rpi2-sparse_defconfig`, `atari512_defconfig`, `atari512-dispatch_defconfig`, `virt-arm_defconfig`; all clean, no new warnings. This is the widest-blip-radius change of the plan — check each.
- Behavior-neutrality: save `stat -c %s` of `kernel7.img`/`ptos512k.img`/`virt-arm.elf` before this task and compare after — identical (widening changes struct sizes but not code paths; tables gain one `UWORD` member, which lands in .data, not the image's code sections — confirm there is **no** size delta; if there is, note it and confirm it is only the two ops tables).
- `timeout 5 qemu-system-arm -M raspi2b -bios kernel7.img -d guest_errors -serial stdio` boots to the desktop (raspi2 pass signals per the smoke skill; `VDI video mode = 1280x720 16-bit`, `AES: EMUDESK: evnt_multi()`).

---

### Task 3: runtime palette packing keyed on `v_planes` + `vdi_truecolor_pixel_size()` (design C)

The palette stores the active-format packed value. Seed/write/read it as RGB565 when `v_planes == 16` and XRGB8888 (with the alpha byte) when `v_planes == 32`. On RGB565 the output must be bit-identical to today.

**Files:**
- Modify: `vdi/vdi_backend_truecolor.c`
- Modify: `vdi/vdi_backend.h` — declare `vdi_truecolor_pixel_size()`
- Modify: `include/gsxdefs.h` — declare it for the AES too

- [ ] **Step 1: 8-bit component conversion helper**

Add next to `rgb565_from_vdi` (line 299):

```c
/*
 * VDI-scale (0-1000) component -> 8 bits, rounding division like
 * rgb565_from_vdi() above.  Used by the XRGB8888 packing path.
 */
static UBYTE vdi_from_vdi8(WORD c)
{
    return (UBYTE)(((LONG)c * 255 + 500) / 1000);
}
```

- [ ] **Step 2: `vdi_truecolor_init_palette()` (line 153)**

In the seeding loop (line 157-158), pack per screen depth:

```c
    for (i = 0; i < 256; i++) {
        if (linea_vars.v_planes == 32) {
            /* XRGB8888: default_prgb_palette[] is already 0x00BBGGRR,
             * DRM XRGB8888 layout missing only the alpha byte */
            vwk->tc_palette[i] = default_prgb_palette[i] | 0xff000000UL;
        } else {
            vwk->tc_palette[i] = rgb565_from_prgb(default_prgb_palette[i]);
        }
    }
```

The `tc_req_col` loop below is unchanged (VDI 0-1000 scale, format-independent).

- [ ] **Step 3: `vdi_truecolor_set_color()` / `vdi_truecolor_get_color()` (lines 331, 350)**

`set_color` packs per depth instead of always `rgb565_from_vdi()`:

```c
    if (linea_vars.v_planes == 32) {
        ULONG prgb = (ULONG)vdi_from_vdi8(r) | ((ULONG)vdi_from_vdi8(g) << 8)
                   | ((ULONG)vdi_from_vdi8(b) << 16);
        vwk->tc_palette[index] = prgb | 0xff000000UL;
    } else {
        vwk->tc_palette[index] = rgb565_from_vdi(r, g, b);
    }
```

`get_color` unpacks per depth instead of always `vdi_from_rgb565()`:

```c
    if (linea_vars.v_planes == 32) {
        ULONG packed = vwk->tc_palette[index];
        *r = (WORD)(((LONG)((packed >> 16) & 0xffUL) * 1000 + 127) / 255);
        *g = (WORD)(((LONG)((packed >>  8) & 0xffUL) * 1000 + 127) / 255);
        *b = (WORD)(((LONG)(packed & 0xffUL) * 1000 + 127) / 255);
    } else {
        vdi_from_rgb565(vwk->tc_palette[index], r, g, b);
    }
```

- [ ] **Step 4: `vdi_truecolor_pixel_size()`**

Add next to `vdi_truecolor_screen()` (line 262):

```c
/*
 * Bytes per packed pixel of the current screen (2 for RGB565, 4 for
 * XRGB8888).  v_planes is the descriptor's bits_per_pixel (lineainit.c),
 * so this is exactly bpp/8.  Used by the software mouse cursor, the AES
 * colour-icon packers and setup_info() to stop hard-coding 2.
 */
UWORD vdi_truecolor_pixel_size(void)
{
    return (UWORD)(linea_vars.v_planes / 8);
}
```

Declare it in `vdi/vdi_backend.h` in the `#if CONF_WITH_VDI_BACKEND_TRUECOLOR` block (after line 160), and in `include/gsxdefs.h` in the AES-visible block (after line 146) so `aes/gemrslib.c` can call it.

**Verification:**
- `make rpi2_defconfig && make` builds clean; smoke-boot `timeout 5 qemu-system-arm -M raspi2b -bios kernel7.img -d guest_errors -serial stdio` reaches the desktop; the desktop palette (menu bar white, etc.) is unchanged — RGB565 path must be bit-identical.
- `stat -c %s kernel7.img` unchanged vs pre-task (the depth branches are runtime `if`s on `v_planes`, and the compiler keeps both packs — check whether size moved; either is acceptable as long as behavior is unchanged, but report the delta).

---

### Task 4: shared template + RGB565 wrapper (design B1/B2)

Split `vdi/vdi_backend_truecolor.c` into a pixel-parameterized template plus a thin RGB565 wrapper. The object name, the exported symbols, and all shared state stay in `obj/vdi_backend_truecolor.o`. Dispatch builds must compile to code identical to today.

**Files:**
- New: `vdi/vdi_backend_truecolor_tmpl.c` — the template (not listed in build.mk)
- Modify: `vdi/vdi_backend_truecolor.c` — becomes the RGB565 wrapper

- [ ] **Step 1: extract the drawing code into the template**

Create `vdi/vdi_backend_truecolor_tmpl.c` containing, from today's `vdi_backend_truecolor.c`:
- `tc_get_start_addr`, `tc_get_pixel`, `tc_put_pixel` (from `truecolor_get_start_addr`/`get_pixel`/`put_pixel`, lines 363-397),
- `tc_fill_rect` (lines 399-448),
- `tc_text_blit` (lines 474-686),
- `tc_raster_copy` (lines 732-838) plus its `apply_raster_op()` (lines 695-716),
- `tc_draw_line` (lines 863-937),
- `tc_search_right` / `tc_search_left` (lines 951-973),
- `tc_get_raw_pixel` / `tc_put_raw_pixel` (lines 975-983),
- `get_src_word` (lines 461-464).

Rename each `truecolor_X` → `tc_X` and make it `static` (they are referenced only through the wrapper's ops table / forwarding stubs). `tc_open`/`tc_close` are **not** needed in the template — the wrapper's ops table can use the existing no-op statics or NULL (NULL is fine: `vdi_backend_ops_init()` installs `default_open`/`default_close`).

Parameterize:
- Type: every `UWORD` that is a *pixel* becomes `PIXEL`; every `UWORD *`/`const UWORD *` that is a *pixel pointer* becomes `PIXEL *`/`const PIXEL *`. Non-pixel `UWORD`s (masks, linemask, pattern words, `get_src_word` return) stay `UWORD`.
- Constants: `addr += (LONG)x * 2` (line 368) → `* PIXEL_SIZE`; `dst[x] ^= 0xffff` (line 432) → `^= (PIXEL)-1`; `*q = ~*q` (lines 627, 786) → `(PIXEL)~*q` (stays a whole-pixel invert); `*p ^= 0xffff` (lines 903, 923) → `^= (PIXEL)-1`.
- Strides: `vars->dform += vars->DESTX * sizeof(WORD)` (line 542) → `* PIXEL_SIZE`; the three `dst += sizeof(UWORD)` skew steps (lines 583, 614, 645) → `+= PIXEL_SIZE`; `q++` stays (PIXEL pointer).
- Sanity checks: `if (info->d_nxwd != 2)` (line 736) and `if (info->s_nxwd != 2)` (line 804) → `!= PIXEL_SIZE`.
- Palette access: add a template-local index helper that reads the shared, per-workstation palette through the extern accessor (keeps the template self-contained so the 32 bpp TU compiles):

```c
static PIXEL tc_pixel_for_index(WORD index)
{
    if (index < 0 || index > 255)
        index = 0;
    return (PIXEL)vdi_backend_active_vwk()->tc_palette[index];
}
```

Replace every `truecolor_pixel_for_index(` call in the moved code with `tc_pixel_for_index(`. In `tc_get_pixel`'s reverse search (line 385), compare against the widened palette: `if (palette[i] == (ULONG)raw)` (palette is `ULONG`, raw is `PIXEL` — for RGB565 the stored value's upper bits are zero).

The template needs no `#include`s of its own — it is compiled inside the wrapper's TU. Add a file-heading comment saying exactly that, documenting the two required macros.

- [ ] **Step 2: sparse-table guard for the optional-slot statics**

In `rpi2-sparse_defconfig` (`CONF_VDI_SPARSE_TABLE=y`) the wrapper's ops table leaves `fill_rect`/`text_blit`/`raster_copy`/`draw_line`/`search_right`/`search_left` NULL, so `tc_fill_rect`, `tc_text_blit`, `tc_raster_copy`, `tc_draw_line`, `tc_search_right`, `tc_search_left` become unreferenced statics — `-Wall` would flag them. Mark exactly those six with the attribute, and explain why:

```c
#if CONF_VDI_SPARSE_TABLE
/* Unreferenced in sparse builds, where the wrapper's ops table leaves the
 * optional slots NULL (see vdi_backend_truecolor.c). */
#define TC_SPARSE_UNUSED __attribute__((unused))
#else
#define TC_SPARSE_UNUSED
#endif
```

and declare them as e.g. `static void tc_fill_rect(const VwkAttrib *attr, const Rect *rect) TC_SPARSE_UNUSED`.

- [ ] **Step 3: turn `vdi/vdi_backend_truecolor.c` into the RGB565 wrapper**

The file keeps (unchanged code):
- the `default_prgb_palette[]` table (lines 36-101) and `rgb565_from_prgb` (line 103),
- `vdi_truecolor_init_palette()` (Task 3 version), `vdi_truecolor_set_color`/`get_color` (Task 3 versions),
- `active_vwk`, `physical_palette_seeded`, `physical_vwk_seeded()`, `vdi_backend_set_active_vwk`/`vdi_backend_active_vwk`,
- `vdi_truecolor_screen()`, `vdi_truecolor_pixel_for_index()` (public), `vdi_truecolor_pixel_size()`.

The static `active_palette()` and static `truecolor_pixel_for_index()` (lines 239-250) are **removed** — the template has `tc_pixel_for_index` and reads `vdi_backend_active_vwk()` directly. Delete the moved drawing functions and `get_src_word`/`apply_raster_op`. Then, with `PIXEL`/`PIXEL_SIZE` defined and the template included:

```c
/* The drawing code lives in the shared template, instantiated here for
 * 16bpp RGB565.  The 32 bpp XRGB8888 instantiation is
 * vdi_backend_truecolor32.c.  Everything in the template is static; the
 * nine direct-call names below exist only for single-renderer builds. */
#define PIXEL UWORD
#define PIXEL_SIZE 2
#include "vdi_backend_truecolor_tmpl.c"
#undef PIXEL_SIZE
#undef PIXEL
```

After the include, define the ops table (mandatory slots from the template's statics, six optional slots NULL under sparse as today, and the trailing `pixel_size: 2`). The no-op `open`/`close` can be NULL (init fills them) or kept as tiny statics — either compiles identically; keep them as small statics matching today's table shape if simpler.

- [ ] **Step 4: direct-call forwarding stubs**

At the bottom of the wrapper, under `#if !CONF_WITH_VDI_BACKEND_DISPATCH`, provide the nine names the single-renderer callers use (prototypes already exist: vdi_defs.h:320-326, vdi_raster.h:109, vdi_textblit.h:101):

```c
#if !CONF_WITH_VDI_BACKEND_DISPATCH
/*
 * Single-renderer truecolor builds call these directly (see vdi/build.mk
 * comment); under dispatch they are never referenced and the templates'
 * statics are reached through the ops table instead.
 */
UWORD *truecolor_get_start_addr(WORD x, WORD y) { return tc_get_start_addr(x, y); }
UWORD truecolor_get_pixel(WORD x, WORD y) { return tc_get_pixel(x, y); }
void truecolor_put_pixel(WORD x, WORD y, UWORD color) { tc_put_pixel(x, y, color); }
void truecolor_fill_rect(const VwkAttrib *attr, const Rect *rect) { tc_fill_rect(attr, rect); }
UWORD truecolor_draw_line(const Line *line, WORD wrt_mode, UWORD color, UWORD linemask) { return tc_draw_line(line, wrt_mode, color, linemask); }
WORD truecolor_search_right(const VwkClip *clip, WORD x, WORD y, UWORD search_col) { return tc_search_right(clip, x, y, search_col); }
WORD truecolor_search_left(const VwkClip *clip, WORD x, WORD y, UWORD search_col) { return tc_search_left(clip, x, y, search_col); }
void truecolor_raster_copy(struct raster_t *raster, struct blit_frame *info) { tc_raster_copy(raster, info); }
void truecolor_text_blit(LOCALVARS *vars) { tc_text_blit(vars); }
#endif
```

- [ ] **Step 5: confirm no dispatch build references the bare `truecolor_*` names**

`grep -n 'truecolor_get_start_addr\|truecolor_get_pixel\|truecolor_put_pixel\|truecolor_fill_rect\|truecolor_draw_line\|truecolor_search_right\|truecolor_search_left\|truecolor_raster_copy\|truecolor_text_blit' vdi/` — every hit outside `vdi_backend_truecolor.c` must be inside a `#else` branch of `CONF_WITH_VDI_BACKEND_DISPATCH` (vdi_misc.c get_start_addr/pixelread/put_pix, vdi_fill.c draw_rect_common, vdi_line.c abline, vdi_raster.c cpy_raster, vdi_textblit.c screen_blit) or a prototype (vdi_defs.h/vdi_raster.h/vdi_textblit.h). The prototypes stay unconditional (harmless in dispatch builds).

**Verification:**
- `make rpi2_defconfig && make` (single-renderer truecolor) builds clean, boots to the desktop (`qemu-system-arm -M raspi2b -bios kernel7.img -d guest_errors -serial stdio`); image size changes only by the ~9 tiny thunks — report the delta.
- `make rpi2-sparse_defconfig && make` builds **warning-free** (this is the task that could regress sparse) and boots to the desktop.
- `make atari512-dispatch_defconfig && make` builds clean; `stat -c %s ptos512k.img` **identical** to the pre-task build (dispatch builds have no stubs — this is the byte-identical regression target).
- `make gitready` clean.

---

### Task 5: software mouse cursor runtime truecolor check (design D)

Replace the `#ifdef MACHINE_RPI` compile-time split in `cur_display()` with a runtime `vdi_screen_is_truecolor()` check, and make the truecolor cursor path pixel-size-aware.

**Files:**
- Modify: `vdi/vdi_mouse.c`

- [ ] **Step 1: `mouse_save` buffer (lines 874-898)**

`mouse_save` is declared under `#ifdef MACHINE_RPI` and its `buffer[16*16]` is `UWORD`. Gate it on the backend instead and widen the buffer:

```c
#if CONF_WITH_VDI_BACKEND_TRUECOLOR
/* The mcs struct is not big enough for a packed truecolor cursor (2 or 4
 * bytes/pixel).  Also serves as the software cursor's fallback save area
 * when CONF_RASPI_MOUSE_CURSOR is set but the hardware cursor is
 * unavailable at runtime -- see raspi_hw_cursor_available below. */
static struct {
    WORD x;
    WORD y;
    WORD width;
    WORD height;
    ULONG buffer[16*16];
} mouse_save;
#endif
```

The `raspi_hw_cursor_available` block (lines 887-897) stays `#ifdef MACHINE_RPI` (it drives `raspi_hw_cur_display()`, which is RPi-only).

- [ ] **Step 2: restructure `cur_display()` (line 900)**

Keep the RPi hardware-cursor attempt at the top, gated `#ifdef MACHINE_RPI` (unchanged). Then replace the `#ifdef MACHINE_RPI / #else` renderer split with a runtime backend check:

```c
#if CONF_WITH_VDI_BACKEND_TRUECOLOR
    if (vdi_screen_is_truecolor())
    {
        /* packed truecolor cursor (2 or 4 bytes/pixel) */
        ...
    }
    else
#endif
    {
        /* planar cursor -- unchanged MCS_LONGS handling */
        ...
    }
```

In the truecolor branch, make the pixel step dynamic: `cdb_fg`/`cdb_bg` become `ULONG` (from `vdi_truecolor_pixel_for_index()`, Task 2/3), the save buffer a `ULONG *`, and the cursor walk advances by `vdi_truecolor_pixel_size()` bytes per pixel instead of `addr++` on a `UWORD *`:

```c
        UWORD psize = vdi_truecolor_pixel_size();
        ULONG *save_data = mouse_save.buffer;
        ...
        while (row_count--)
        {
            UBYTE *base = (UBYTE *)get_start_addr(x, y++);
            UBYTE *addr8 = base;
            for (current_bit = start_bit; current_bit > end_bit; current_bit >>= 1)
            {
                ULONG *px = (ULONG *)addr8;
                *(save_data++) = *px;
                if (data[1] & current_bit)
                    *px = cdb_fg;
                else if (data[0] & current_bit)
                    *px = cdb_bg;
                addr8 += psize;
            }
            data += 2;
        }
```

Alignment note: the framebuffer's rows are 2- or 4-byte aligned (the descriptor pitch check, Task 1) and pixel starts are `x * psize` from a row start, so the `ULONG *` cast is aligned for psize 4. The `cur_replace()`/`cur_display_clip()` planar helpers are unchanged.

**Verification:**
- `make rpi2_defconfig && make` builds clean and boots (`raspi2b`); with `CONF_RASPI_MOUSE_CURSOR` defaulting y, QEMU fails the hardware cursor and falls back to the software cursor — the desktop must still reach `evnt_multi()` with no guest errors (this exercises the runtime-truecolor branch on RGB565).
- `make atari512_defconfig && make` builds clean and boots (planar branch byte-identical; this config compiles the truecolor branch out entirely).
- `stat -c %s` comparisons: rpi2 image differs only if the refactored cursor code reorders; atari512 image must be identical (truecolor branch compiled out).

---

### Task 6: `setup_info()` stride math (design F)

Stop hard-coding 2 bytes/pixel in the packed-truecolor branches of `setup_info()`; use `v_planes / 8`.

**Files:**
- Modify: `vdi/vdi_raster.c`

- [ ] **Step 1: introduce the shared byte-per-pixel value**

At the top of `setup_info()` (line 808) add:

```c
    /* bytes per packed pixel (2 for RGB565, 4 for XRGB8888) */
    const UWORD packed_ppb = (UWORD)(linea_vars.v_planes / 8);
```

- [ ] **Step 2: replace the hard-coded `2`s in the truecolor branches**

| Line | Today | Becomes |
|---|---|---|
| 837 | `info->s_nxln = src->fd_w * 2;` | `info->s_nxln = src->fd_w * packed_ppb;` |
| 861 | `info->s_nxwd = 2;` | `info->s_nxwd = packed_ppb;` |
| 862 | `info->s_nxln = src->fd_w * 2;` | `info->s_nxln = src->fd_w * packed_ppb;` |
| 877 | `info->s_nxwd = 2;` | `info->s_nxwd = packed_ppb;` |
| 902 | `info->d_nxwd = 2;` | `info->d_nxwd = packed_ppb;` |
| 903 | `info->d_nxln = dst->fd_w * 2;` | `info->d_nxln = dst->fd_w * packed_ppb;` |
| 913 | `info->d_nxwd = 2;` | `info->d_nxwd = packed_ppb;` |

Update the comments at lines 870-875 and 892-898 that say "2 bytes" to say "the packed pixel size". `s_nxwd`/`d_nxwd` are `UWORD` and `packed_ppb` is 2 or 4, so both fit; `src->fd_w * packed_ppb` stays `UWORD`-sized arithmetic (matches the current `* 2`).

**Verification:**
- `make rpi2_defconfig && make` builds clean and boots to the desktop (RGB565: `packed_ppb == 2`, byte-identical path).
- `make atari512-dispatch_defconfig && make` builds clean (planar path unchanged; the truecolor branches compile because TRUECOLOR is on, but `vdi_screen_is_truecolor()` is FALSE at runtime on the planar screen).
- The window-drag/scroll path (`bb_save`/`bb_restore`, which routes through these strides) still works: under QEMU raspi2 the desktop menu interactions after boot must not corrupt — at minimum, no guest_errors and the desktop stays alive through the timeout window.

---

### Task 7: AES colour-icon packers (design E)

Make the AES's packed truecolor icon conversion write 32 bpp pixels on a 32 bpp screen, keyed on `vdi_truecolor_pixel_size()`.

**Files:**
- Modify: `aes/gemrslib.c`

- [ ] **Step 1: `pack_planes()` (line 298)**

```c
static void pack_planes(const WORD *data, ULONG *pix, WORD planes, WORD w, WORD h)
```

`*pix++ = vdi_truecolor_pixel_for_index(code);` stays (the call now returns `ULONG`). Update the comment (lines 289-296): pixels are the active-format packed value, not specifically RGB565.

- [ ] **Step 2: `pack_cicon()` (line 329)**

```c
    LONG pixels = (LONG)w * h;
    ULONG *packed;

    packed = dos_alloc_anyram(pixels * (cicon->sel_data ? 2 : 1) * vdi_truecolor_pixel_size());
```

`ULONG *selbuf = packed + pixels;` stays. `cicon->col_data = (WORD *)packed;` / `= (WORD *)selbuf;` stay (casts away the pixel type). Note `vdi_truecolor_pixel_size()` is the AES-visible extern declared in Task 3.

**Verification:**
- `make rpi2_defconfig && make` builds clean (RGB565: `pixel_size() == 2`, allocation and layout byte-identical to today).
- Boot rpi2 to the desktop — colour icons render via the same path; no guest_errors.
- The `CONF_WITH_VDI_CICON_TEST` build (`make menuconfig` on rpi2 to add it, or the cicon test config used by the earlier plan) still renders the test icon on RGB565 — proves the packers still produce correct 16 bpp output.

---

### Task 8: the 32 bpp wrapper + Kconfig + selection (design B3/B4/B5)

Add the XRGB8888 instantiation of the template, its Kconfig switch, the select branch, and the two-before-one `vdi_screen_is_truecolor()`.

**Files:**
- New: `vdi/vdi_backend_truecolor32.c`
- Modify: `vdi/Kconfig`
- Modify: `vdi/build.mk`
- Modify: `vdi/vdi_backend.h`
- Modify: `vdi/vdi_backend.c`

- [ ] **Step 1: `vdi/vdi_backend_truecolor32.c`**

```c
/*
 * vdi_backend_truecolor32.c - packed 32bpp XRGB8888 truecolor VDI backend
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * Thin wrapper: the drawing code is the shared template
 * (vdi_backend_truecolor_tmpl.c), instantiated here for 32bpp XRGB8888.
 * All shared state (default palette, active workstation, the format-
 * independent exports) lives in vdi_backend_truecolor.c, which is always
 * built when CONF_WITH_VDI_BACKEND_TRUECOLOR is set -- this wrapper only
 * defines its own ops table.  No extern stubs: a single-renderer 32bpp
 * build is impossible (CONF_WITH_VDI_BACKEND_TRUECOLOR32 depends on the
 * dispatcher), and under dispatch the drawing goes through the table.
 */

#include "config.h"
#include "portab.h"
#include "asm.h"
#include "../bios/lineavars.h"
#include "../bios/tosvars.h"
#include "vdi_defs.h"
#include "vdi_backend.h"

#define PIXEL ULONG
#define PIXEL_SIZE 4
#include "vdi_backend_truecolor_tmpl.c"
#undef PIXEL_SIZE
#undef PIXEL

vdi_backend_ops packed_truecolor32_backend_ops = {
    NULL,                       /* open: generic default */
    NULL,                       /* close: generic default */
    tc_get_start_addr,
    tc_get_pixel,
    tc_put_pixel,
    tc_get_raw_pixel,
    tc_put_raw_pixel,
    tc_fill_rect,
    tc_text_blit,
    tc_raster_copy,
    tc_draw_line,
    tc_search_right,
    tc_search_left,
    4,                          /* pixel_size */
};
```

- [ ] **Step 2: Kconfig (vdi/Kconfig, after the TRUECOLOR entry, line 39)**

```kconfig
config CONF_WITH_VDI_BACKEND_TRUECOLOR32
	bool "Packed 32bpp XRGB8888 truecolor VDI backend"
	depends on CONF_WITH_VDI_BACKEND_TRUECOLOR && CONF_WITH_VDI_BACKEND_DISPATCH
	default n
	help
	  Build the XRGB8888 (32bpp) variant of the packed-truecolor VDI
	  backend (vdi_backend_truecolor32.o), sharing its drawing code with
	  the 16bpp RGB565 backend via the PIXEL/PIXEL_SIZE template
	  (vdi_backend_truecolor_tmpl.c).  Only selectable with the runtime
	  dispatcher enabled, since a single-renderer 32bpp build is not a
	  supported configuration.

	  The active pixel format comes from the screen mode descriptor
	  (SCREEN_PIXEL_XRGB8888), never from per-machine code.  Raspberry Pi
	  stays RGB565; this exists for machines like QEMU's virt-arm whose
	  (test) framebuffer is 32 bpp -- see issue #91.
```

- [ ] **Step 3: build.mk (vdi/build.mk, after line 15)**

```make
obj-$(CONF_WITH_VDI_BACKEND_TRUECOLOR32) += vdi_backend_truecolor32.o
```

The template is not a build target — it is `#include`d.

- [ ] **Step 4: `vdi_backend.h`**

Add the extern next to `packed_truecolor_backend_ops` (line 105), guarded so non-32bpp dispatch builds don't reference an undefined symbol:

```c
#if CONF_WITH_VDI_BACKEND_TRUECOLOR32
extern vdi_backend_ops packed_truecolor32_backend_ops;
#endif
```

Change `vdi_screen_is_truecolor()` (line 126):

```c
static inline BOOL vdi_screen_is_truecolor(void)
{
#if CONF_WITH_VDI_BACKEND_DISPATCH
    const vdi_backend_ops *backend = vdi_screen_backend();

#if CONF_WITH_VDI_BACKEND_TRUECOLOR32
    if (backend == &packed_truecolor32_backend_ops)
        return TRUE;
#endif
    return backend == &packed_truecolor_backend_ops;
#else
    return CONF_WITH_VDI_BACKEND_TRUECOLOR;
#endif
}
```

- [ ] **Step 5: `vdi_backend_select()` (vdi/vdi_backend.c, inside the DISPATCH-only file)**

After the RGB565 branch (line 34):

```c
#if CONF_WITH_VDI_BACKEND_TRUECOLOR32
    if (mode->layout == SCREEN_LAYOUT_PACKED
        && mode->color_model == SCREEN_COLOR_TRUECOLOR
        && mode->pixel_format == SCREEN_PIXEL_XRGB8888) {
        vdi_backend_ops_init(&packed_truecolor32_backend_ops);
        return &packed_truecolor32_backend_ops;
    }
#endif
```

**Verification:**
- `make rpi2_defconfig && make` and `make atari512-dispatch_defconfig && make`: with TRUECOLOR32 default n, builds are byte-identical (`stat -c %s` matches pre-task); the 32bpp wrapper object is not built; nothing references `packed_truecolor32_backend_ops`.
- `make rpi2-sparse_defconfig && make` builds warning-free and boots (the `vdi_screen_is_truecolor` inline still links without TRUECOLOR32).
- A scratch build with TRUECOLOR32=y on rpi2 (`make menuconfig`, enable it, `make`) — the wrapper object compiles, links, and boots (rpi2's descriptor is still RGB565, so runtime behavior is unchanged; this proves the new object is self-contained).

---

### Task 9: virt-arm test hook + `EXTENDED_PALETTE` + test defconfig (design G + correction 2)

Give QEMU virt-arm an XRGB8888 descriptor backed by guest RAM, so the 32 bpp path actually runs. This is where design corrections 1 and 2 land.

**Files:**
- New: `bios/machine/virt-arm/virt_screen.c`
- Modify: `bios/screen.c`
- Modify: `bios/build.mk`
- Modify: `vdi/vdi_col.c` — `EXTENDED_PALETTE` (correction 2)
- Modify: `Kconfig.machine` — the test switch
- New: `configs/virt-arm-tc32_defconfig`

- [ ] **Step 1: `EXTENDED_PALETTE` (vdi/vdi_col.c:22) — mandatory**

```c
#define EXTENDED_PALETTE (CONF_WITH_VIDEL || CONF_WITH_TT_SHIFTER || defined(MACHINE_RPI) \
    || CONF_WITH_VDI_TRUECOLOR32_TEST)
```

Without this, `v_planes == 32` makes `DEV_TAB[13] == 256` (vdi_control.c:436) while `MAP_COL[]`/`REV_MAP_COL[]` have 16 entries — `init_colors()` (vdi_col.c:711-712) then writes `REV_MAP_COL[MAP_COL[i]]` up to index 255, an out-of-bounds write. Under the test build `init_colors()` seeds `req_col2` untouched (the VIDEL/TT branches don't run on virt-arm), so `vq_color(pen>15, 0)` reads BSS zeros — harmless for a test build; note it in the commit message. `MAP_COL[1]` stays `DEV_TAB[13]-1` = 255, which is what the truecolor backend expects.

- [ ] **Step 2: the test switch (Kconfig.machine, after the machine `endchoice` at line 71)**

```kconfig
config CONF_WITH_VDI_TRUECOLOR32_TEST
	bool "Virt (ARM) XRGB8888 screen test hook"
	depends on MACHINE_VIRT_ARM && CONF_WITH_VDI_BACKEND_TRUECOLOR32
	default n
	help
	  Test hook for issue #91: makes the QEMU virt (ARM) machine report
	  a 640x480 XRGB8888 mode descriptor backed by a guest-RAM
	  framebuffer (bios/machine/virt-arm/virt_screen.c), so the 32bpp
	  truecolor backend can be exercised under QEMU even though the
	  machine has no display hardware.  For verification only -- do not
	  enable in production images.  Issue #68's real ramfb driver will
	  replace this buffer later.
```

- [ ] **Step 3: `bios/machine/virt-arm/virt_screen.c`** (design correction 1 — not `screen.c`, whose basename collides with `bios/screen.c`)

```c
/*
 * virt_screen.c - QEMU virt (ARM) XRGB8888 test framebuffer (issue #91)
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * Test hook only (CONF_WITH_VDI_TRUECOLOR32_TEST): QEMU's 'virt' board
 * has no display hardware, so the framebuffer is a guest-RAM buffer.  It
 * is never displayed; the point is to exercise the 32bpp XRGB8888
 * truecolor VDI backend end-to-end.  Issue #68's ramfb driver replaces
 * this buffer later.
 */

#include "config.h"
#ifndef MACHINE_VIRT_ARM
#error This file must only be compiled for the QEMU virt (ARM) target
#endif

#include "portab.h"
#include "screen_mode.h"
#include "biosmem.h"            /* balloc_stram */
#include "tosvars.h"            /* v_bas_ad */
#include "kprint.h"
#include "virt_mmu.h"           /* virt_to_phys */

#define VIRT_TC32_WIDTH  640
#define VIRT_TC32_HEIGHT 480
#define VIRT_TC32_PITCH  (VIRT_TC32_WIDTH * 4)

void virt_arm_screen_init(void)
{
    UBYTE *screen_start = balloc_stram(VIRT_TC32_PITCH * VIRT_TC32_HEIGHT, TRUE);

    v_bas_ad = screen_start;
    kprintf("virt-arm tc32: %dx%d XRGB8888 framebuffer at phys 0x%08lx (%d bytes)\n",
            VIRT_TC32_WIDTH, VIRT_TC32_HEIGHT,
            virt_to_phys(screen_start), VIRT_TC32_PITCH * VIRT_TC32_HEIGHT);
}

void virt_arm_get_current_mode_desc(SCREEN_MODE_DESC *desc)
{
    desc->width = VIRT_TC32_WIDTH;
    desc->height = VIRT_TC32_HEIGHT;
    desc->pitch = VIRT_TC32_PITCH;
    desc->bits_per_pixel = 32;
    desc->layout = SCREEN_LAYOUT_PACKED;
    desc->color_model = SCREEN_COLOR_TRUECOLOR;
    desc->pixel_format = SCREEN_PIXEL_XRGB8888;
}
```

`virt_arm_screen_init` needs a prototype (it is called from bios/screen.c): add `void virt_arm_screen_init(void);` and `void virt_arm_get_current_mode_desc(SCREEN_MODE_DESC *desc);` to a header. `bios/machine/virt-arm/virt_mmu.h` already declares `virt_to_phys()`. (Alternative: put the two prototypes in `include/screen.h`, next to the other screen_* decls.)

- [ ] **Step 4: wire it into bios/screen.c**

`screen_init_address()` (line 577) — test branch first (note CONF_WITH_VDI_TRUECOLOR32_TEST implies MACHINE_VIRT_ARM):

```c
static void screen_init_address(void)
{
#if CONF_WITH_VDI_TRUECOLOR32_TEST
    virt_arm_screen_init();
    setphys(v_bas_ad);
#elif defined(MACHINE_RPI)
    ...
```

`screen_get_current_mode_desc()` (line 770):

```c
#if CONF_WITH_VDI_TRUECOLOR32_TEST
    virt_arm_get_current_mode_desc(desc);
#elif defined(MACHINE_RPI)
    ...
```

Include the virt_screen.h header where the prototypes live (under `#if CONF_WITH_VDI_TRUECOLOR32_TEST`, or unconditionally with the prototypes themselves guarded).

- [ ] **Step 5: bios/build.mk** — add near the virt-arm line (line 46):

```make
obj-$(CONF_WITH_VDI_TRUECOLOR32_TEST) += virt_screen.o
```

- [ ] **Step 6: `configs/virt-arm-tc32_defconfig`**

Start from `virt-arm_defconfig` (copy it), then via `make menuconfig` enable `CONF_WITH_VDI_BACKEND_TRUECOLOR`, `CONF_WITH_VDI_BACKEND_TRUECOLOR32` and `CONF_WITH_VDI_TRUECOLOR32_TEST`; run `make savedefconfig` and copy `./defconfig` to `configs/virt-arm-tc32_defconfig`. Expect the resulting file to be `virt-arm_defconfig` plus the three `=y` lines:

```
CONF_WITH_VDI_BACKEND_TRUECOLOR=y
CONF_WITH_VDI_BACKEND_TRUECOLOR32=y
CONF_WITH_VDI_TRUECOLOR32_TEST=y
```

(with `CONF_WITH_VDI_BACKEND_PLANAR` staying at its default y — the derived `CONF_WITH_VDI_BACKEND_DISPATCH` comes on automatically). Keep the explanatory comments of the parent defconfig; add one noting this is the #91 32bpp test build.

**Verification:**
- `make virt-arm-tc32_defconfig && make` builds clean. Image is `virt-arm.elf`.
- `timeout 5 qemu-system-arm -M virt,highmem=off -cpu cortex-a7 -m 128 -kernel virt-arm.elf -d guest_errors,unimp -D /tmp/qemu.log -display none -serial stdio`:
  - pass signals: process survives the full window (rc=124); no `guest_errors`/`unimp` beyond the one benign ARM equivalent (the m68k `_detect_fpu` entry does not exist here); stdout shows `virt-arm tc32: 640x480 XRGB8888 framebuffer at phys ...`, `VDI video mode = 640x480 32-bit`, `AES: EMUDESK: appl_init()`, `AES: EMUDESK: evnt_multi()`.
  - the `VDI video mode = 640x480 32-bit` line **is** the `v_planes == 32` assertion (gemgraf.c:436 reads gl_nplanes == v_planes). Do not look for the vdi_v_opnwk KDEBUG — it is compiled out (correction 5).
- `make virt-arm_defconfig && make` still boots unchanged (test off → no `virt_screen.o`, EXTENDED_PALETTE back to the non-RPI value, planar descriptor as today).

---

### Task 10: verification sweep + regression matrix (design H)

**Files:** none (verification only).

- [ ] **Step 1: full build matrix** — each of the following must build clean and pass its smoke test:

| Config | Emulator check |
|---|---|
| `virt-arm-tc32_defconfig` | QEMU virt ARM (Task 9 command): rc=124, `640x480 32-bit`, desktop idle lines, no guest_errors/unimp |
| `virt-arm_defconfig` | QEMU virt ARM: unchanged boot (planar) |
| `virt-m68k_defconfig` | `timeout 5 qemu-system-m68k -M virt -m 128 -cpu m68020 -kernel virt-m68k.elf -d guest_errors,unimp -D /tmp/qemu.log -display none -serial stdio`: rc=124, one benign `Illegal Instruction` from `_detect_fpu`, no unimp |
| `rpi1_defconfig` | `timeout 5 qemu-system-arm -M raspi1ap -bios kernel.img -d guest_errors -serial stdio` → desktop |
| `rpi2_defconfig` | `timeout 5 qemu-system-arm -M raspi2b -bios kernel7.img -d guest_errors -serial stdio` → desktop |
| `rpi2-sparse_defconfig` | QEMU raspi2b: desktop, warning-free build (sparse generic-defaults path intact) |
| `atari512-dispatch_defconfig` | Hatari STE smoke per the smoke skill (AVI frame analysis for the desktop; allow ~95 s wall): the RGB565 dispatch build is unchanged |
| `atari512_defconfig` | Hatari STE or at least clean build: planar regression |
| `cartridge_defconfig` | clean build: the 128 KB single-renderer planar build (no dispatch) still fits |

- [ ] **Step 2: code-size regression when TRUECOLOR32 is off**

For `atari512-dispatch_defconfig`, `rpi2-sparse_defconfig` and `rpi1_defconfig`, `stat -c %s` of the image must match the pre-change build (dispatch builds are byte-identical per Task 4; rpi1 was already verified in Tasks 4-8). Record the actual numbers in the task report.

- [ ] **Step 3: 32 bpp content check (optional, deep)**

The desktop is drawn into the headless RAM framebuffer; the only way to *see* it is to dump the buffer. The test hook prints the framebuffer's physical address (`virt_to_phys(v_bas_ad)`). Run QEMU with `-monitor unix:/tmp/mon,server,nowait -serial file:/tmp/boot.log`, wait for boot, then:

```
echo 'pmemsave 0x<phys> 1228800 /tmp/fb.bin' | socat - UNIX-CONNECT:/tmp/mon
```

then inspect `/tmp/fb.bin` with python3 + PIL: pixels of the top menu-bar rows should be the menu-bar background (white-ish, 0xffc0c0c0-ish in XRGB8888 byte order B,G,R,X on little-endian) with black glyph pixels where "Desk File View Options" renders, and the desktop area below shows the green/white pattern region. This proves the fill_rect/text_blit paths write correct 32 bpp data, not just that the machine boots. If the monitor/socat dance is unavailable, fall back to `-gdb tcp::1234` + `arm-none-eabi-gdb -batch -ex 'target remote :1234' -ex 'dump binary memory /tmp/fb.bin <va> <va>+1228800'` using the CPU-visible (low-window) address.

- [ ] **Step 4: desktop drawing on 32 bpp**

During the Step 1 virt-arm-tc32 run, note that `evnt_multi()` idle plus a complete boot is the pass bar; the real drawing correctness is the Step 3 dump (the desktop repaints its background and menu bar at boot through the truecolor32 backend's `fill_rect`/`text_blit`). Anything that made the backend write wrong strides would fault or corrupt — the absence of guest_errors plus a sane dump is the evidence.

---

## Self-Review

- **Interface widening is behavior-neutral on existing configs**: Task 2 keeps RGB565/planar byte-identical; the raw slots carry `ULONG` but every consumer casts back on the way in/out. Verified by the Task 2 size-comparison step.
- **The template refactor preserves both build modes**: single-renderer truecolor gets nine forwarding thunks (prototypes already unconditional in vdi_defs.h/vdi_raster.h/vdi_textblit.h); dispatch builds get zero new code (Task 4 Step 5 grep + size comparison). The `rpi2-sparse` unused-static hazard is handled with the `TC_SPARSE_UNUSED` attribute.
- **`vdi_screen_is_truecolor()` stays linkable in every config**: the `packed_truecolor32_backend_ops` comparison and extern are both inside `#if CONF_WITH_VDI_BACKEND_TRUECOLOR32` (correction 4); the inline's non-dispatch branch still returns the compile-time constant.
- **The 32 bpp test build cannot silently corrupt memory**: `EXTENDED_PALETTE` on the test config (correction 2) sizes `MAP_COL`/`REV_MAP_COL` for 256 entries before `v_planes == 32` reaches `init_colors()`/`vdi_truecolor_init_palette()`. Task 9 Step 1 lands before Step 6's defconfig ever runs.
- **No per-machine drawing code**: the virt-arm hook supplies only the descriptor and a RAM buffer; everything downstream is the shared backend. The test switch defaults n and is documented as a test hook.
- **Byte-order of XRGB8888**: the palette is 0x00BBGGRR (DRM XRGB8888 byte order B,G,R,X on little-endian targets); the `| 0xff000000UL` alpha add (Task 3) matches DRM layout. The framebuffer dump check (Task 10 Step 3) confirms the actual byte order empirically before any wider claim.
- **Verification-before-completion**: each task lists concrete builds + smoke commands with named pass signals; Task 10 is the explicit sweep. No task claims success without running them.
