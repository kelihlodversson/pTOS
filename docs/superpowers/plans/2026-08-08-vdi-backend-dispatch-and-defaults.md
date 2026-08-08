# VDI Backend Dispatch Gating + Generic Defaults Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Gate the VDI backend dispatcher on having more than one renderer (not on truecolor), and replace NULL dispatch-table slots with generic defaults installed by an init function, so callers stop null-checking individual slots (issue #138).

**Architecture:** Split the single `CONF_WITH_VDI_TRUECOLOR` bool into per-renderer bools (`CONF_WITH_VDI_BACKEND_PLANAR` / `CONF_WITH_VDI_BACKEND_TRUECOLOR`) plus a derived `CONF_WITH_VDI_BACKEND_DISPATCH` (on exactly when both renderers are enabled). The caller dispatch sites become three-way `#if` arms: dispatch / direct-truecolor / direct-planar. Then extend `vdi_backend_ops` with a raw-pixel read/write pair (so generic defaults reach full XOR and raster-op fidelity), make the tables non-const, and have `vdi_backend_ops_init()` — called idempotently from `vdi_backend_select()` — fill any NULL optional slot with a generic default built only on the mandatory primitives. A test-only config (`CONF_VDI_SPARSE_TABLE`) makes the truecolor table sparse so the defaults are exercised against the real RGB565 framebuffer under QEMU.

**Tech Stack:** GNU make + kconfiglib, GCC C90 (`-std=gnu90`) for `arm-none-eabi-` and `m68k-atari-mintelf-`. Verification via QEMU (`raspi2b`, virt) and Hatari (STE), per `.claude/skills/ptos-smoketest/SKILL.md`.

## Global Constraints

- C90 with GNU extensions (`-std=gnu90`): declarations at the top of a block; `/* */` comments. 4 spaces, never a hard tab. Run `make gitready` before committing.
- **`int` is 16 bits on m68k** (`-mshort`). Use `WORD`/`LONG`/`UBYTE`/`UWORD`/`ULONG`; suffix constants that must survive on m68k.
- `-Wundef` is on: every `#if` symbol must be defined. Feature symbols are always defined `0`/`1` and tested with `#if` (see `CLAUDE.md` naming table). Never edit `obj/autoconf.h` / `obj/auto.conf`.
- Backend contract: a NULL slot means "this backend does not implement this primitive", never a fallback to another backend. Mandatory primitives are called unchecked.
- Generic defaults must be renderer-agnostic: built only on the five mandatory primitives (`get_start_addr`, `get_pixel`, `put_pixel`, `get_raw_pixel`, `put_raw_pixel`), never on planar/truecolor knowledge.
- The ops tables are only compiled when `CONF_WITH_VDI_BACKEND_DISPATCH` is set. Single-renderer builds must compile to *direct calls* (no indirect dispatch) — this is what keeps `cartridge_defconfig`'s 128 KB image small.
- `CONF_CHUNKY_PIXELS` is unchanged; its forks are separate issue-#35 work.
- pTOS accesses line-A variables as `linea_vars.<name>`; `v_bas_ad` is a global `UBYTE *`.
- Verification before completion: build the affected configs and run the smoke tests listed in each task; report real output, not assumptions.

---

### Task 1: Gate the dispatcher on the renderer count

Delivers issue #138 part 1: renderer-count gating with direct-truecolor and direct-planar fallbacks. Kconfig introduces the new symbols (keeping the tree building at every step via the alias `CONF_WITH_VDI_TRUECOLOR`, removed at the end of this task).

**Files:**
- Modify: `vdi/Kconfig` — replace `CONF_WITH_VDI_TRUECOLOR` with the renderer + dispatch symbols
- Modify: `vdi/build.mk` — gate backend objects on the new symbols
- Modify: `bios/build.mk` — gate `screen_mode.o` on dispatch
- Modify: `vdi/vdi_backend.h` — guard restructure + inline `vdi_screen_is_truecolor()`
- Modify: `vdi/vdi_backend.c` — guard arm rename; drop the out-of-line `vdi_screen_is_truecolor()`
- Modify: `vdi/vdi_control.c` — three gate changes
- Modify: `vdi/vdi_misc.c:180-206`, `vdi/vdi_fill.c:731-745,846-871,1137-1174`, `vdi/vdi_line.c:368-380,1423-1443`, `vdi/vdi_textblit.c:727-738`, `vdi/vdi_raster.c:810-822,836-847,864-870,981-1001` — three-way arms
- Modify: `vdi/vdi_backend_truecolor.c` — expose the primitives (drop `static`)
- Modify: `vdi/vdi_defs.h`, `vdi/vdi_textblit.h`, `vdi/vdi_raster.h` — truecolor primitive prototypes
- Test: `configs/atari512_defconfig`, `configs/rpi2_defconfig`, `configs/virt-arm_defconfig`, manual dispatch config

**Interfaces:**
- Consumes: existing `planar_*` prototypes in `vdi/vdi_defs.h:290-298`, `vdi/vdi_textblit.h:100`, `vdi/vdi_raster.h:108`.
- Produces: `CONF_WITH_VDI_BACKEND_PLANAR`, `CONF_WITH_VDI_BACKEND_TRUECOLOR`, `CONF_WITH_VDI_BACKEND_DISPATCH`; direct-callable `truecolor_get_start_addr()` / `truecolor_get_pixel()` / `truecolor_put_pixel()` / `truecolor_fill_rect()` / `truecolor_draw_line()` / `truecolor_search_right()` / `truecolor_search_left()` / `truecolor_text_blit()` / `truecolor_raster_copy()`; header inline `vdi_screen_is_truecolor()` valid in all three modes.

- [x] **Step 1: Replace the Kconfig option**

In `vdi/Kconfig`, replace the whole `config CONF_WITH_VDI_TRUECOLOR` block (lines 29-41) with:

```kconfig
config CONF_WITH_VDI_BACKEND_PLANAR
	bool "Planar (indexed bitplane) VDI backend"
	default y if !MACHINE_RPI
	default n
	help
	  Build the planar (interleaved-bitplane) VDI backend -- the classic
	  Atari screen layout, and the only renderer needed for any indexed
	  planar screen (all Atari-family machines, virt-arm, virt-m68k).

	  Disabling it while the truecolor backend is enabled removes the
	  runtime dispatcher (see vdi/build.mk), which is what a machine whose
	  descriptor can only report packed truecolor -- like the Raspberry
	  Pi -- wants.  At least one renderer must stay enabled.

config CONF_WITH_VDI_BACKEND_TRUECOLOR
	bool "Packed truecolor VDI backend"
	default y if MACHINE_RPI
	default n
	help
	  Build the packed-truecolor VDI backend (vdi_backend_truecolor.o),
	  for packed 16bpp RGB565 framebuffers.  Raspberry Pi is the first
	  machine that uses it -- see issue #35.

	  Raspberry Pi's screen descriptor unconditionally reports a
	  truecolor mode, so this backend is mandatory there: the prompt is
	  hidden (and the option forced to y) whenever MACHINE_RPI is set,
	  since turning it off would leave the RPi screen with no matching
	  renderer.

config CONF_WITH_VDI_BACKEND_DISPATCH
	bool
	default y if CONF_WITH_VDI_BACKEND_PLANAR && CONF_WITH_VDI_BACKEND_TRUECOLOR
	help
	  Runtime dispatch between more than one VDI renderer.  Derived: it
	  is on exactly when two or more renderers are enabled, so a single
	  renderer build carries no dispatch machinery at all -- the callers
	  call that one renderer's primitives directly.
```

Keep `CONF_CHUNKY_PIXELS` (above) and `CONF_WITH_LINEA` (below) untouched. Temporary alias, so the tree still builds until Step 4 converts the `#if CONF_WITH_VDI_TRUECOLOR` sites — insert between `CONF_WITH_VDI_BACKEND_DISPATCH` and `CONF_WITH_LINEA`:

```kconfig
# Temporary alias while #138's Task 1 converts the remaining
# #if CONF_WITH_VDI_TRUECOLOR sites. Removed at the end of the task.
config CONF_WITH_VDI_TRUECOLOR
	bool
	default y if CONF_WITH_VDI_BACKEND_TRUECOLOR
```

- [x] **Step 2: Update the object gating in `vdi/build.mk`**

Replace line 14:

```make
obj-$(CONF_WITH_VDI_TRUECOLOR) += vdi_backend.o vdi_backend_planar.o vdi_backend_truecolor.o
```

with:

```make
# The dispatch machinery (selection + the per-renderer ops tables) only
# exists when more than one renderer is enabled: with exactly one, the
# callers call that renderer's primitives directly (see vdi_misc.c/
# vdi_fill.c/vdi_line.c) and nothing dispatches through these tables.
obj-$(CONF_WITH_VDI_BACKEND_DISPATCH) += vdi_backend.o vdi_backend_planar.o
obj-$(CONF_WITH_VDI_BACKEND_TRUECOLOR) += vdi_backend_truecolor.o
```

- [x] **Step 3: Update the `screen_mode.o` gating in `bios/build.mk`**

Replace line 28:

```make
obj-$(CONF_WITH_VDI_TRUECOLOR) += screen_mode.o
```

with:

```make
obj-$(CONF_WITH_VDI_BACKEND_DISPATCH) += screen_mode.o
```

(`screen_mode_desc_valid()` is only called by `vdi_backend_select()` in `vdi/vdi_backend.c`, which itself only builds under dispatch.)

- [x] **Step 4: Restructure `vdi/vdi_backend.h`**

Rewrite the guards and the big comment (lines 59-117). Replace everything from the comment block starting `/* This runtime selection machinery` through the final `#endif /* CONF_WITH_VDI_TRUECOLOR */` with:

```c
/*
 * This runtime selection machinery -- vdi_backend_select(),
 * vdi_screen_backend(), the vdi_backend_ops table, and the per-renderer
 * tables -- only exists when CONF_WITH_VDI_BACKEND_DISPATCH is set (more
 * than one renderer enabled, see vdi/build.mk). With exactly one
 * renderer, the primitives that would otherwise dispatch through it
 * (get_start_addr/pixelread/put_pix/draw_rect_common/text_blit/raster_copy/
 * abline/end_pts in vdi_misc.c/vdi_fill.c/vdi_line.c/vdi_textblit.c/
 * vdi_raster.c) call that renderer's primitives directly instead -- the
 * planar ones for a planar-only build, the truecolor ones for a
 * truecolor-only build. This matters on cartridge_defconfig, whose 128 KB
 * image has essentially no room for dispatch overhead that can only ever
 * resolve one way.
 */
#if CONF_WITH_VDI_BACKEND_DISPATCH

/*
 * Picks a backend ops table for a mode descriptor, or NULL if no backend
 * supports that layout/color-model/pixel-format combination.
 *
 * Every existing driver reports a descriptor a backend handles (planar+
 * indexed, or on MACHINE_RPI, packed+truecolor+RGB565), and can only be
 * queried once its video hardware is set up, so in practice this cannot
 * return NULL for any of them. The check exists for whichever future
 * driver reports something no backend yet implements.
 */
const vdi_backend_ops *vdi_backend_select(const SCREEN_MODE_DESC *mode);

/*
 * The backend ops table for the current screen workstation. Self-
 * initializes on first call if vdi_v_opnwk() hasn't run yet (see the
 * comment on the definition) -- callers do not need to call vdi_v_opnwk()
 * first. Returns NULL only in the vdi_backend_select() case above, which
 * cannot happen for any of this codebase's drivers today -- but callers
 * still guard against it (see get_start_addr()/pixelread()/put_pix()/
 * draw_rect_common()). There is currently exactly one screen.
 */
const vdi_backend_ops *vdi_screen_backend(void);

extern vdi_backend_ops planar_backend_ops;
extern vdi_backend_ops packed_truecolor_backend_ops;

#endif /* CONF_WITH_VDI_BACKEND_DISPATCH */

/*
 * Is the current screen workstation driven by the packed-truecolor
 * backend?  Used by text_blt() to decide whether styled text must go
 * through pre_blit(), by cpy_raster() for the packed 1-plane MFDB layout,
 * and by contourfill() for the full MAP_COL palette index.
 *
 * Inline so it exists in all three build modes: under dispatch it is the
 * runtime check against the selected table; with exactly one renderer the
 * answer is a compile-time constant.
 */
static inline BOOL vdi_screen_is_truecolor(void)
{
#if CONF_WITH_VDI_BACKEND_DISPATCH
    return vdi_screen_backend() == &packed_truecolor_backend_ops;
#else
    return CONF_WITH_VDI_BACKEND_TRUECOLOR;
#endif
}

/*
 * Turns a MAP_COL-mapped hardware palette index into the raw RGB565 pixel
 * value the packed-truecolor backend would write for it. Used by callers
 * that poke pixels directly instead of going through put_pixel()/
 * fill_rect() -- currently the RPi software mouse cursor in vdi_mouse.c.
 */
#if CONF_WITH_VDI_BACKEND_TRUECOLOR
UWORD vdi_truecolor_pixel_for_index(WORD index);
#endif

#endif /* VDI_BACKEND_H */
```

The `vdi_backend_ops` typedef and its contract comment stay where they are, inside the new `#if CONF_WITH_VDI_BACKEND_DISPATCH` block (the struct is only referenced under dispatch). Update the typedef comment's wording: "A NULL slot means 'this backend does not implement this primitive' -- never 'fall back to another backend'... NULL slots are filled with generic defaults by `vdi_backend_ops_init()` (task 2); mandatory slots are `get_start_addr`, `get_pixel`, `put_pixel` (and `get_raw_pixel`/`put_raw_pixel` once task 2 adds them)." Keep `search_right`/`search_left`'s "Mandatory" comment as-is for now (task 2 amends it).

- [x] **Step 5: Update `vdi/vdi_backend.c`**

- Change the `#if CONF_WITH_VDI_TRUECOLOR` around the truecolor select arm (line 21) to `#if CONF_WITH_VDI_BACKEND_TRUECOLOR` (always true under dispatch; kept for symmetry).
- Delete the out-of-line `vdi_screen_is_truecolor()` (lines 31-34) — it moved to the header as an inline.

The file now reads:

```c
const vdi_backend_ops *vdi_backend_select(const SCREEN_MODE_DESC *mode)
{
    if (!screen_mode_desc_valid(mode))
        return NULL;

    if (mode->layout == SCREEN_LAYOUT_PLANAR && mode->color_model == SCREEN_COLOR_INDEXED)
        return &planar_backend_ops;

#if CONF_WITH_VDI_BACKEND_TRUECOLOR
    if (mode->layout == SCREEN_LAYOUT_PACKED
        && mode->color_model == SCREEN_COLOR_TRUECOLOR
        && mode->pixel_format == SCREEN_PIXEL_RGB565)
        return &packed_truecolor_backend_ops;
#endif

    return NULL;
}
```

- [x] **Step 6: Update the three gates in `vdi/vdi_control.c`**

- Line 413 (`vdi_v_opnwk` sets `vwk->mode`/`vwk->backend`): `#if CONF_WITH_VDI_TRUECOLOR` → `#if CONF_WITH_VDI_BACKEND_DISPATCH` (the block only drives the dispatch machinery).
- Line 470 (`vdi_v_clrwk`): `#if CONF_WITH_VDI_TRUECOLOR` → `#if CONF_WITH_VDI_BACKEND_TRUECOLOR` (a *behavior* gate: a packed truecolor screen's pen 0 is white, so it must clear through `draw_rect_common()`, never `memset(v_bas_ad, 0, ...)`).
- Line 561 (`vdi_screen_backend()` definition): `#if CONF_WITH_VDI_TRUECOLOR` → `#if CONF_WITH_VDI_BACKEND_DISPATCH`. Also update the comment inside it that says "Only built when CONF_WITH_VDI_TRUECOLOR is set" → "Only built when CONF_WITH_VDI_BACKEND_DISPATCH is set".

- [x] **Step 7: Expose the truecolor primitives**

In `vdi/vdi_backend_truecolor.c`, drop `static` from: `truecolor_get_start_addr` (line 158), `truecolor_get_pixel` (168), `truecolor_put_pixel` (187), `truecolor_fill_rect` (194), `truecolor_text_blit` (269), `truecolor_raster_copy` (527), `truecolor_draw_line` (658), `truecolor_search_right` (746), `truecolor_search_left` (758). Leave `truecolor_open`/`truecolor_close` static (table-only).

Add prototypes, following where the planar equivalents live:

In `vdi/vdi_defs.h`, after the planar block (after line 298):

```c
/* truecolor backend primitives (vdi_backend_truecolor.c) -- callable
 * directly in truecolor-only builds, where the dispatcher is compiled out */
UWORD *truecolor_get_start_addr(WORD x, WORD y);
UWORD truecolor_get_pixel(WORD x, WORD y);
void truecolor_put_pixel(WORD x, WORD y, UWORD color);
void truecolor_fill_rect(const VwkAttrib *attr, const Rect *rect);
UWORD truecolor_draw_line(const Line *line, WORD wrt_mode, UWORD color, UWORD linemask);
WORD truecolor_search_right(const VwkClip *clip, WORD x, WORD y, UWORD search_col);
WORD truecolor_search_left(const VwkClip *clip, WORD x, WORD y, UWORD search_col);
```

In `vdi/vdi_textblit.h`, next to `planar_text_blit` (line 100):

```c
void truecolor_text_blit(LOCALVARS *vars);
```

In `vdi/vdi_raster.h`, next to `planar_raster_copy` (line 108):

```c
void truecolor_raster_copy(struct raster_t *raster, struct blit_frame *info);
```

- [x] **Step 8: Convert the dispatch sites to three-way arms**

For each site below, the pattern is the same: the old `#if CONF_WITH_VDI_TRUECOLOR` / `#else` becomes `#if CONF_WITH_VDI_BACKEND_DISPATCH` / `#elif CONF_WITH_VDI_BACKEND_TRUECOLOR` (direct truecolor) / `#else` (direct planar). Update the accompanying comments to name the new symbols.

`vdi/vdi_misc.c` `get_start_addr()` (180-206):

```c
#if CONF_WITH_VDI_BACKEND_DISPATCH
    const vdi_backend_ops *backend = vdi_screen_backend();

    /*
     * Unlike the direct-call cases below, this path builds only for
     * configurations with more than one renderer, which have none of
     * cartridge_defconfig's byte-budget pressure -- so guard against
     * vdi_backend_select() returning NULL for a descriptor no backend
     * supports, rather than dereferencing it.
     */
    if (!backend)
        return NULL;
    return backend->get_start_addr(x, y);
#elif CONF_WITH_VDI_BACKEND_TRUECOLOR
    /*
     * Truecolor-only build: call the packed backend's address arithmetic
     * directly -- vdi_screen_backend() and its self-init check have no
     * caller left, so the machinery is compiled out entirely.
     */
    return truecolor_get_start_addr(x, y);
#else
    /*
     * Planar-only build: call the planar address arithmetic directly
     * instead of paying for an indirect call the result of which is
     * already known at compile time. This matters on cartridge_defconfig,
     * whose 128 KB image has essentially no spare room for dispatch
     * overhead that can only ever resolve one way.
     */
    return planar_get_start_addr(x, y);
#endif
```

`vdi/vdi_fill.c` `pixelread()` (731-745):

```c
#if CONF_WITH_VDI_BACKEND_DISPATCH
    const vdi_backend_ops *backend = vdi_screen_backend();

    /* see the comment in get_start_addr() (vdi_misc.c) */
    if (!backend)
        return 0;
    return backend->get_pixel(x, y);
#elif CONF_WITH_VDI_BACKEND_TRUECOLOR
    /* see the comment in get_start_addr() (vdi_misc.c) */
    return truecolor_get_pixel(x, y);
#else
    /* see the comment in get_start_addr() (vdi_misc.c) */
    return planar_get_pixel(x, y);
#endif
```

`vdi/vdi_fill.c` `end_pts()` (846-871):

```c
#if CONF_WITH_VDI_BACKEND_DISPATCH
    {
        const vdi_backend_ops *backend = vdi_screen_backend();

        /* see the comment in get_start_addr() (vdi_misc.c) */
        if (!backend) {
            *xleftout = *xrightout = x;
            return 0;
        }
        /* get the search color -- reuse backend, don't re-derive it via pixelread() */
        color = backend->get_pixel(x, y);
        *xrightout = backend->search_right(clip, x, y, color);
        *xleftout = backend->search_left(clip, x, y, color);
    }
#elif CONF_WITH_VDI_BACKEND_TRUECOLOR
    /*
     * Truecolor-only build: call the packed backend's primitives directly
     * (see the comment in get_start_addr() in vdi_misc.c).
     */
    color = truecolor_get_pixel(x, y);
    *xrightout = truecolor_search_right(clip, x, y, color);
    *xleftout = truecolor_search_left(clip, x, y, color);
#else
    /*
     * Planar-only build: call the planar primitives directly (see the
     * comment on get_start_addr() in vdi_misc.c).
     */
    color = planar_get_pixel(x, y);
    *xrightout = planar_search_right(clip, x, y, color);
    *xleftout = planar_search_left(clip, x, y, color);
#endif
```

`vdi/vdi_fill.c` `put_pix()` — two gates (1147 and 1162):

The NULL-address guard becomes dispatch-only (a direct-call address can never be NULL):

```c
    addr = get_start_addr(x, y);
#if CONF_WITH_VDI_BACKEND_DISPATCH
    /* see the comment in get_start_addr() (vdi_misc.c) -- addr can be NULL
     * here, and comparing a NULL pointer against v_bas_ad below would be
     * undefined behaviour, not just a wrong answer */
    if (!addr)
        return;
#endif
```

The write site becomes three-way:

```c
#if CONF_WITH_VDI_BACKEND_DISPATCH
    {
        const vdi_backend_ops *backend = vdi_screen_backend();

        /* see the comment in get_start_addr() (vdi_misc.c) */
        if (backend)
            backend->put_pixel(x, y, color);
    }
#elif CONF_WITH_VDI_BACKEND_TRUECOLOR
    /* see the comment in get_start_addr() (vdi_misc.c) */
    truecolor_put_pixel(x, y, color);
#else
    /* see the comment in get_start_addr() (vdi_misc.c) */
    planar_put_pixel(x, y, color);
#endif
```

`vdi/vdi_line.c` `draw_rect_common()` (368-380):

```c
#if CONF_WITH_VDI_BACKEND_DISPATCH
    const vdi_backend_ops *backend = vdi_screen_backend();

    /* see the comment in get_start_addr() (vdi_misc.c) */
    if (backend)
        backend->fill_rect(attr, rect);
#elif CONF_WITH_VDI_BACKEND_TRUECOLOR
    /* see the comment in get_start_addr() (vdi_misc.c) */
    truecolor_fill_rect(attr, rect);
#else
    /* see the comment in get_start_addr() (vdi_misc.c) */
    planar_fill_rect(attr, rect);
#endif
```

`vdi/vdi_line.c` `abline()` (1423-1442) — keep the NULL-backend guard, drop nothing yet:

```c
#if CONF_WITH_VDI_BACKEND_DISPATCH
    {
        const vdi_backend_ops *backend = vdi_screen_backend();

        /* see the comment in get_start_addr() (vdi_misc.c) */
        if (backend)
            linea_vars.LN_MASK = backend->draw_line(line, wrt_mode, color, linemask);
    }
#elif CONF_WITH_VDI_BACKEND_TRUECOLOR
    /* see the comment in get_start_addr() (vdi_misc.c) */
    linea_vars.LN_MASK = truecolor_draw_line(line, wrt_mode, color, linemask);
#else
    /* see the comment in get_start_addr() (vdi_misc.c) */
    linea_vars.LN_MASK = planar_draw_line(line, wrt_mode, color, linemask);
#endif
```

`vdi/vdi_textblit.c` `text_blt()` (727-737):

```c
#if CONF_WITH_VDI_BACKEND_DISPATCH
    {
        const vdi_backend_ops *backend = vdi_screen_backend();

        /* see the comment in get_start_addr() (vdi_misc.c) */
        if (backend)
            backend->text_blit(vars);
    }
#elif CONF_WITH_VDI_BACKEND_TRUECOLOR
    /* see the comment in get_start_addr() (vdi_misc.c) */
    truecolor_text_blit(vars);
#else
    planar_text_blit(vars);
#endif
```

`vdi/vdi_raster.c` `cpy_raster()` (981-1000):

```c
#if CONF_WITH_VDI_BACKEND_DISPATCH
    {
        const vdi_backend_ops *backend = vdi_screen_backend();

        /* see the comment in get_start_addr() (vdi_misc.c) */
        if (backend)
            backend->raster_copy(raster, info);
    }
#elif CONF_WITH_VDI_BACKEND_TRUECOLOR
    /* see the comment in get_start_addr() (vdi_misc.c) */
    truecolor_raster_copy(raster, info);
#else
    /* see the comment in get_start_addr() (vdi_misc.c) */
    planar_raster_copy(raster, info);
#endif
```

- [x] **Step 9: Convert the truecolor-behavior gates**

These sites test *truecolor-specific behavior*, not dispatch — their `#if CONF_WITH_VDI_TRUECOLOR` becomes `#if CONF_WITH_VDI_BACKEND_TRUECOLOR`. The `vdi_screen_is_truecolor()` calls inside them now resolve to the header inline (compile-time constant in single-renderer builds):

- `vdi/vdi_raster.c:810`, `:836`, `:864` — `#if CONF_WITH_VDI_TRUECOLOR` → `#if CONF_WITH_VDI_BACKEND_TRUECOLOR`.
- `vdi/vdi_fill.c:919` — same.
- `vdi/vdi_textblit.c:899` — same.
- `vdi/vdi_control.c:470` — already done in Step 6.
- `vdi/vdi_backend.c:21` — already done in Step 5.
- `vdi/vdi_mouse.c:29-32` — leave untouched: the include is under `#ifdef MACHINE_RPI` and the `vdi_truecolor_pixel_for_index()` calls (lines 925-926) are inside the `#ifdef MACHINE_RPI` block at line 902, and `CONF_WITH_VDI_BACKEND_TRUECOLOR` is forced `y` on `MACHINE_RPI`.

- [x] **Step 10: Remove the Kconfig alias and any leftovers**

Delete the temporary `CONF_WITH_VDI_TRUECOLOR` alias added in Step 1. Then:

Run: `grep -rn "CONF_WITH_VDI_TRUECOLOR" vdi/ bios/ include/ --include=*.c --include=*.h --include=*.mk`
Expected: no matches (only `docs/superpowers/*` history and `.claude/skills` may still mention the old name — those are updated in Task 3).

- [x] **Step 11: Verify all three build modes**

Run, from a clean tree (`make distclean` once):

1. Planar-only (unchanged objects): `make atari512_defconfig && make`
   Expected: builds; `ls obj/vdi_backend*.o` → **no matches**; image `ptos512k.img` produced.
2. Truecolor-only (RPi drops the dispatcher): `make rpi2_defconfig && make`
   Expected: builds; `ls obj/vdi_backend*.o` → only `obj/vdi_backend_truecolor.o`; `kernel7.img` produced.
3. Dispatch (both renderers on Atari): `make atari512_defconfig && make menuconfig` → enable `CONF_WITH_VDI_BACKEND_TRUECOLOR=y`, save; then `make`
   Expected: builds; `obj/vdi_backend.o`, `obj/vdi_backend_planar.o`, `obj/vdi_backend_truecolor.o` all present; `ptos512k.img` produced.
4. Planar-only ARM: `make virt-arm_defconfig && make`
   Expected: builds; `obj/vdi_backend*.o` → no matches; `virt-arm.elf` produced.

Run `make gitready` at the end.

- [x] **Step 12: Smoke-test the RPi build**

```sh
timeout 30 qemu-system-arm -M raspi2b -bios kernel7.img -d guest_errors -serial stdio
```
Expected: no `guest_errors`; the screen draws (raspi2 boots to the desktop). Note the `vdi_v_opnwk: mode layout=...` serial KDEBUG no longer prints on default rpi2 builds (it is dispatch-only) — the smoketest skill's serial note is updated in Task 3.

- [x] **Step 13: Commit**

```bash
git add vdi/Kconfig vdi/build.mk bios/build.mk vdi/vdi_backend.h vdi/vdi_backend.c \
  vdi/vdi_control.c vdi/vdi_misc.c vdi/vdi_fill.c vdi/vdi_line.c vdi/vdi_textblit.c \
  vdi/vdi_raster.c vdi/vdi_backend_truecolor.c vdi/vdi_defs.h vdi/vdi_textblit.h vdi/vdi_raster.h
git commit -m "feat(vdi): gate the backend dispatcher on the renderer count"
```

---

### Task 2: Extend the ops contract and add generic defaults

Delivers issue #138 part 2: the raw-pixel read/write pair (making XOR and opaque raster-ops expressible by renderer-agnostic code), non-const tables, the `vdi_backend_ops_init()` default-installer, removal of the remaining slot null-checks, and the sparse-table test configuration.

**Files:**
- Modify: `vdi/Kconfig` — add `CONF_VDI_SPARSE_TABLE`
- Modify: `vdi/vdi_backend.h` — raw-pixel members, non-const externs, `vdi_backend_ops_init()` prototype, contract comment
- Modify: `vdi/vdi_backend.c` — `apply_raster_op` copy, generic defaults, `vdi_backend_ops_init()`, call it from `vdi_backend_select()`
- Modify: `vdi/vdi_backend_planar.c` — non-const table + raw-pixel aliases
- Modify: `vdi/vdi_backend_truecolor.c` — non-const table + raw-pixel implementations + sparse initializer
- Modify: `vdi/vdi_line.c:1423-1432`, `vdi/vdi_raster.c:981-990` — drop the slot guards
- Create: `configs/atari512-dispatch_defconfig`, `configs/rpi2-sparse_defconfig`
- Test: `configs/rpi2_defconfig`, `configs/rpi2-sparse_defconfig`, `configs/atari512_defconfig`, `configs/atari512-dispatch_defconfig`

**Interfaces:**
- Consumes: the three-way arms and the exposed `truecolor_*` / `planar_*` primitives from Task 1; `vdi_screen_backend()` (dispatch only).
- Produces: `vdi_backend_ops` members `get_raw_pixel`/`put_raw_pixel`; `vdi_backend_ops_init(vdi_backend_ops *ops)`; static defaults `default_fill_rect` / `default_draw_line` / `default_text_blit` / `default_raster_copy` / `default_search_right` / `default_search_left` / `default_open` / `default_close`; non-const `planar_backend_ops` / `packed_truecolor_backend_ops`; `CONF_VDI_SPARSE_TABLE`.

- [x] **Step 1: Add the sparse-table Kconfig option**

In `vdi/Kconfig`, after the `CONF_WITH_VDI_BACKEND_DISPATCH` block:

```kconfig
config CONF_VDI_SPARSE_TABLE
	bool "Exercise the generic backend defaults"
	depends on CONF_WITH_VDI_BACKEND_DISPATCH
	default n
	help
	  For testing issue #138's generic dispatch-table defaults: make the
	  packed-truecolor backend leave its optional ops slots NULL so the
	  dispatcher's init function fills them with the generic default
	  implementations, which are then exercised against the real RGB565
	  framebuffer.  Only meaningful when both renderers are enabled
	  (otherwise there is no dispatcher).  Do not enable in production
	  images.
```

- [x] **Step 2: Extend `vdi/vdi_backend.h`**

Add the two members to `vdi_backend_ops`, right after `put_pixel`:

```c
    UWORD *(*get_start_addr)(WORD x, WORD y);
    UWORD (*get_pixel)(WORD x, WORD y);
    void (*put_pixel)(WORD x, WORD y, UWORD color);
    /*
     * Raw framebuffer word access, bypassing the palette-index mapping of
     * get_pixel()/put_pixel().  Mandatory: the generic defaults need it to
     * express bitwise operations (XOR write mode, the opaque boolean-raster-
     * op path of raster_copy), which a palette index cannot represent.  On
     * the planar backend the raw value is the composed plane index, i.e.
     * get_pixel()/put_pixel() themselves.
     */
    UWORD (*get_raw_pixel)(WORD x, WORD y);
    void (*put_raw_pixel)(WORD x, WORD y, UWORD raw);
```

Update the struct's contract comment (lines 16-26): "A NULL slot means 'this backend does not implement this primitive' -- never 'fall back to another backend.' The dispatcher's `vdi_backend_ops_init()` (see below) fills every NULL slot with a renderer-agnostic default built only on the mandatory primitives (`get_start_addr`, `get_pixel`, `put_pixel`, `get_raw_pixel`, `put_raw_pixel`), which must be non-NULL. `open`/`close` default to no-ops." Drop the `search_right`/`search_left` "Mandatory, like get_pixel()/put_pixel()" claim — they get defaults now.

Update the `extern` declarations to non-const and add the init prototype:

```c
extern vdi_backend_ops planar_backend_ops;
extern vdi_backend_ops packed_truecolor_backend_ops;

/*
 * Installs a generic default into every NULL slot of a backend ops table
 * (see the defaults in vdi_backend.c).  Mandatory slots must already be
 * non-NULL.  Idempotent: safe to call on every vdi_backend_select().
 */
void vdi_backend_ops_init(vdi_backend_ops *ops);
```

- [x] **Step 3: Non-const tables + raw-pixel entries in the planar backend**

`vdi/vdi_backend_planar.c` (lines 24-36):

```c
vdi_backend_ops planar_backend_ops = {
    planar_open,
    planar_close,
    planar_get_start_addr,
    planar_get_pixel,
    planar_put_pixel,
    planar_get_pixel,       /* get_raw_pixel: a planar pixel's raw value is its composed colour index */
    planar_put_pixel,       /* put_raw_pixel: same for writing */
    planar_fill_rect,
    planar_text_blit,
    planar_raster_copy,
    planar_draw_line,
    planar_search_right,
    planar_search_left,
};
```

(`vdi_backend_ops_init()` may mutate the table, so it is no longer `const`.)

- [x] **Step 4: Non-const truecolor table + raw-pixel entries + sparse initializer**

Add the two raw-pixel primitives before `truecolor_open` (after `truecolor_search_left`, line 768):

```c
static UWORD truecolor_get_raw_pixel(WORD x, WORD y)
{
    return *truecolor_get_start_addr(x, y);
}

static void truecolor_put_raw_pixel(WORD x, WORD y, UWORD raw)
{
    *truecolor_get_start_addr(x, y) = raw;
}
```

Replace the table (lines 781-793):

```c
vdi_backend_ops packed_truecolor_backend_ops = {
    truecolor_open,
    truecolor_close,
    truecolor_get_start_addr,
    truecolor_get_pixel,
    truecolor_put_pixel,
    truecolor_get_raw_pixel,
    truecolor_put_raw_pixel,
#if CONF_VDI_SPARSE_TABLE
    /* The optional slots are left NULL so vdi_backend_ops_init() fills them
     * with the generic defaults -- this exercises issue #138's defaults
     * against the real RGB565 framebuffer. Never in production images. */
    NULL, NULL, NULL, NULL, NULL, NULL,
#else
    truecolor_fill_rect,
    truecolor_text_blit,
    truecolor_raster_copy,
    truecolor_draw_line,
    truecolor_search_right,
    truecolor_search_left,
#endif
};
```

- [x] **Step 5: Generic defaults and the init function in `vdi/vdi_backend.c`**

Extend the includes at the top (after the existing `#include "vdi_backend.h"`):

```c
#include "../bios/tosvars.h"    /* v_bas_ad */
#include "../bios/lineavars.h"  /* linea_vars */
#include "asm.h"                /* rolw1/rorw1 */
#include "kprint.h"             /* KDEBUG */
```

Append the defaults and the init function:

```c
/*
 * Generic backend defaults (issue #138): renderer-agnostic fallbacks built
 * only on the mandatory primitives (get_start_addr, get_pixel, put_pixel,
 * get_raw_pixel, put_raw_pixel).  They run through vdi_screen_backend(),
 * which always returns the backend whose table these defaults are installed
 * in -- only the selected screen backend's table is ever dispatched through.
 */
static BOOL default_open(Vwk *vwk)
{
    (void)vwk;
    return TRUE;
}

static void default_close(Vwk *vwk)
{
    (void)vwk;
}

static void default_fill_rect(const VwkAttrib *attr, const Rect *rect)
{
    const vdi_backend_ops *ops = vdi_screen_backend();
    const UWORD patmsk = attr->patmsk;
    WORD x, y, i;

    for (y = rect->y1; y <= rect->y2; y++) {
        UWORD pattern = attr->patptr[patmsk & y];

        for (x = rect->x1, i = 0; x <= rect->x2; x++, i++) {
            BOOL set = (pattern & ((1 << 15) >> (i & 15))) != 0;

            switch (attr->wrt_mode) {
            case 3:                 /* erase (reverse transparent) */
                if (!set)
                    ops->put_pixel(x, y, attr->color);
                break;
            case 2:                 /* xor -- invert the raw word (palette
                                     * indices cannot express bitwise ops) */
                if (set)
                    ops->put_raw_pixel(x, y, ops->get_raw_pixel(x, y) ^ 0xffff);
                break;
            case 1:                 /* transparent */
                if (set)
                    ops->put_pixel(x, y, attr->color);
                break;
            default:                /* replace -- unset bits paint index 0 */
                ops->put_pixel(x, y, set ? attr->color : 0);
                break;
            }
        }
    }
}

/*
 * fetch the source word in big-endian (Motorola font) byte order -- the
 * same helper the truecolor text blit uses (see vdi_backend_truecolor.c).
 */
static UWORD get_src_word(const UBYTE *p)
{
    return (UWORD)(((UWORD)p[0] << 8) | (UWORD)p[1]);
}

static void default_text_blit(LOCALVARS *vars)
{
    const vdi_backend_ops *ops = vdi_screen_backend();
    UBYTE *src, *p;
    UWORD src_mask, mask;
    WORD w, h, x, y;

    /*
     * No skew support here (skewed text needs a backend-provided
     * text_blit); WM_XOR is handled through the raw pixel pair.
     */
    src = vars->sform;
    src_mask = 0x8000 >> vars->tsdad;
    y = vars->DESTY + vars->DELY - 1;   /* we draw from the bottom up */

    for (h = vars->height; h > 0; h--, src += vars->s_next, y--) {
        p = src;
        x = vars->DESTX;
        mask = src_mask;

        for (w = vars->width; w > 0; w--) {
            switch (vars->WRT_MODE) {
            case WM_REPLACE:
                ops->put_pixel(x, y, (get_src_word(p) & mask) ? (UWORD)vars->forecol : 0);
                break;
            case WM_TRANS:
                if (get_src_word(p) & mask)
                    ops->put_pixel(x, y, (UWORD)vars->forecol);
                break;
            case WM_ERASE:
                if (!(get_src_word(p) & mask))
                    ops->put_pixel(x, y, (UWORD)vars->forecol);
                break;
            case WM_XOR:
                if (get_src_word(p) & mask)
                    ops->put_raw_pixel(x, y, ops->get_raw_pixel(x, y) ^ 0xffff);
                break;
            }
            x++;
            rorw1(mask);
            if (mask == 0x8000)
                p += 2;
        }
    }
}

/*
 * apply a VDI boolean raster-op (see BM_* in vdi_raster.h) to a raw source
 * and destination pixel -- the same semantics as the per-plane blitter and
 * the truecolor backend's word-at-a-time copy.
 */
static UWORD apply_raster_op(WORD op, UWORD src, UWORD dst)
{
    switch (op & 0x0f) {
    case BM_ALL_WHITE:  return 0x0000;
    case BM_S_AND_D:    return (UWORD)(src & dst);
    case BM_S_AND_NOTD: return (UWORD)(src & ~dst);
    case BM_S_ONLY:     return src;
    case BM_NOTS_AND_D: return (UWORD)(~src & dst);
    case BM_D_ONLY:     return dst;
    case BM_S_XOR_D:    return (UWORD)(src ^ dst);
    case BM_S_OR_D:     return (UWORD)(src | dst);
    case BM_NOT_SORD:   return (UWORD)~(src | dst);
    case BM_NOT_SXORD:  return (UWORD)~(src ^ dst);
    case BM_NOT_D:      return (UWORD)~dst;
    case BM_S_OR_NOTD:  return (UWORD)(src | ~dst);
    case BM_NOT_S:      return (UWORD)~src;
    case BM_NOTS_OR_D:  return (UWORD)(~src | dst);
    case BM_NOT_SANDD:  return (UWORD)~(src & dst);
    case BM_ALL_BLACK:  return 0xffff;
    default:            return dst;
    }
}

static void default_raster_copy(struct raster_t *raster, struct blit_frame *info)
{
    const vdi_backend_ops *ops = vdi_screen_backend();
    WORD y;

    /*
     * The default operates on the screen through the ops primitives, so a
     * destination (or opaque source) that is not the screen framebuffer has
     * no generic fallback -- a backend that needs it implements raster_copy.
     */
    if (info->d_form != (UWORD *)v_bas_ad)
        return;

    if (raster->transparent) {
        /* 1bpp icon source to packed screen; fg/bg are palette indices */
        for (y = 0; y < info->b_ht; y++) {
            const UBYTE *srow = (const UBYTE *)info->s_form
                + (LONG)(info->s_ymin + y) * info->s_nxln;
            const UBYTE *p = srow + (LONG)(info->s_xmin >> 4) * info->s_nxwd;
            UWORD mask = 0x8000 >> (info->s_xmin & 0x0f);
            WORD x;
            const WORD dy = info->d_ymin + y;

            for (x = 0; x < info->b_wd; x++) {
                BOOL set = (*(const UWORD *)p & mask) != 0;
                const WORD dx = info->d_xmin + x;

                switch (raster->mode) {
                case MD_REPLACE:
                    ops->put_pixel(dx, dy, set ? raster->fg_col : raster->bg_col);
                    break;
                case MD_TRANS:
                    if (set)
                        ops->put_pixel(dx, dy, raster->fg_col);
                    break;
                case MD_XOR:
                    if (set)
                        ops->put_raw_pixel(dx, dy, ops->get_raw_pixel(dx, dy) ^ 0xffff);
                    break;
                case MD_ERASE:
                    if (!set)
                        ops->put_pixel(dx, dy, raster->bg_col);
                    break;
                }
                rorw1(mask);
                if (mask == 0x8000)
                    p += 2;
            }
        }
        return;
    }

    /* opaque: packed screen to packed screen via the raw pixel pair */
    if (info->s_form != (UWORD *)v_bas_ad)
        return;

    {
        BOOL forward_y = TRUE, forward_x = TRUE;

        /* never overwrite source pixels before they're read */
        if (info->d_ymin > info->s_ymin)
            forward_y = FALSE;
        else if ((info->d_ymin == info->s_ymin) && (info->d_xmin > info->s_xmin))
            forward_x = FALSE;

        for (y = 0; y < info->b_ht; y++) {
            WORD row = forward_y ? y : (info->b_ht - 1 - y);
            WORD sy = info->s_ymin + row;
            WORD dy = info->d_ymin + row;
            WORD x;

            for (x = 0; x < info->b_wd; x++) {
                WORD col = forward_x ? x : (info->b_wd - 1 - x);
                WORD sx = info->s_xmin + col;
                WORD dx = info->d_xmin + col;

                ops->put_raw_pixel(dx, dy, apply_raster_op(info->op_tab[0],
                    ops->get_raw_pixel(sx, sy), ops->get_raw_pixel(dx, dy)));
            }
        }
    }
}

static UWORD default_draw_line(const Line *line, WORD wrt_mode, UWORD color, UWORD linemask)
{
    const vdi_backend_ops *ops = vdi_screen_backend();
    WORD x, y, dx, dy, sx, sy, loopcnt;

    if (line->x2 < line->x1) {
        x = line->x2; y = line->y2;
        dx = line->x1 - line->x2;
        dy = line->y1 - line->y2;
    } else {
        x = line->x1; y = line->y1;
        dx = line->x2 - line->x1;
        dy = line->y2 - line->y1;
    }
    if (dy < 0) {
        dy = -dy;
        sy = -1;
    } else {
        sy = 1;
    }
    sx = 1;

    if (dx >= dy) {
        WORD eps = -dx, e1 = 2 * dy, e2 = 2 * dx;

        for (loopcnt = dx; loopcnt >= 0; loopcnt--) {
            rolw1(linemask);
            switch (wrt_mode) {
            case 3: if (linemask & 1) ops->put_pixel(x, y, (UWORD)(~color & 0xff)); break;
            case 2: if (linemask & 1) ops->put_raw_pixel(x, y, ops->get_raw_pixel(x, y) ^ 0xffff); break;
            case 1: if (linemask & 1) ops->put_pixel(x, y, color); break;
            default: ops->put_pixel(x, y, (linemask & 1) ? color : 0); break;
            }
            x += sx;
            eps += e1;
            if (eps >= 0) {
                eps -= e2;
                y += sy;
            }
        }
    } else {
        WORD eps = -dy, e1 = 2 * dx, e2 = 2 * dy;

        for (loopcnt = dy; loopcnt >= 0; loopcnt--) {
            rolw1(linemask);
            switch (wrt_mode) {
            case 3: if (linemask & 1) ops->put_pixel(x, y, (UWORD)(~color & 0xff)); break;
            case 2: if (linemask & 1) ops->put_raw_pixel(x, y, ops->get_raw_pixel(x, y) ^ 0xffff); break;
            case 1: if (linemask & 1) ops->put_pixel(x, y, color); break;
            default: ops->put_pixel(x, y, (linemask & 1) ? color : 0); break;
            }
            y += sy;
            eps += e1;
            if (eps >= 0) {
                eps -= e2;
                x += sx;
            }
        }
    }

    return linemask;
}

static WORD default_search_right(const VwkClip *clip, WORD x, WORD y, UWORD search_col)
{
    const vdi_backend_ops *ops = vdi_screen_backend();

    while (x++ < clip->xmx_clip) {
        if (ops->get_pixel(x, y) != search_col)
            break;
    }
    return x - 1;       /* output x coord -1 to endxright. */
}

static WORD default_search_left(const VwkClip *clip, WORD x, WORD y, UWORD search_col)
{
    const vdi_backend_ops *ops = vdi_screen_backend();

    while (x-- > clip->xmn_clip) {
        if (ops->get_pixel(x, y) != search_col)
            break;
    }
    return x + 1;       /* output x coord + 1 to endxleft. */
}

void vdi_backend_ops_init(vdi_backend_ops *ops)
{
    if (!ops->open) ops->open = default_open;
    if (!ops->close) ops->close = default_close;
    if (!ops->fill_rect) ops->fill_rect = default_fill_rect;
    if (!ops->text_blit) ops->text_blit = default_text_blit;
    if (!ops->raster_copy) ops->raster_copy = default_raster_copy;
    if (!ops->draw_line) ops->draw_line = default_draw_line;
    if (!ops->search_right) ops->search_right = default_search_right;
    if (!ops->search_left) ops->search_left = default_search_left;

    if (!ops->get_start_addr || !ops->get_pixel || !ops->put_pixel
        || !ops->get_raw_pixel || !ops->put_raw_pixel)
        KDEBUG(("vdi_backend_ops_init: backend is missing a mandatory primitive\n"));
}
```

Update `vdi_backend_select()` to install the defaults before returning (both arms):

```c
    if (mode->layout == SCREEN_LAYOUT_PLANAR && mode->color_model == SCREEN_COLOR_INDEXED) {
        vdi_backend_ops_init(&planar_backend_ops);
        return &planar_backend_ops;
    }

#if CONF_WITH_VDI_BACKEND_TRUECOLOR
    if (mode->layout == SCREEN_LAYOUT_PACKED
        && mode->color_model == SCREEN_COLOR_TRUECOLOR
        && mode->pixel_format == SCREEN_PIXEL_RGB565) {
        vdi_backend_ops_init(&packed_truecolor_backend_ops);
        return &packed_truecolor_backend_ops;
    }
#endif
```

- [x] **Step 6: Drop the remaining slot guards at call sites**

`vdi/vdi_line.c` (1423-1432) — the NULL-slot guard becomes just the NULL-backend guard:

```c
#if CONF_WITH_VDI_BACKEND_DISPATCH
    {
        const vdi_backend_ops *backend = vdi_screen_backend();

        /* see the comment in get_start_addr() (vdi_misc.c); every optional
         * slot is filled with a generic default by vdi_backend_ops_init() */
        if (backend)
            linea_vars.LN_MASK = backend->draw_line(line, wrt_mode, color, linemask);
    }
```

`vdi/vdi_raster.c` (981-990):

```c
#if CONF_WITH_VDI_BACKEND_DISPATCH
    {
        const vdi_backend_ops *backend = vdi_screen_backend();

        /* see the comment in get_start_addr() (vdi_misc.c); every optional
         * slot is filled with a generic default by vdi_backend_ops_init() */
        if (backend)
            backend->raster_copy(raster, info);
    }
```

- [x] **Step 7: Create the two test configurations**

Build each with `make menuconfig` (or edit `.config`), then `make savedefconfig` and copy `./defconfig` to `configs/`:

1. `configs/atari512-dispatch_defconfig` — from `atari512_defconfig` with `CONF_WITH_VDI_BACKEND_TRUECOLOR=y`. Exercises the dispatcher on a real m68k Hatari boot (the planar backend is selected through dispatch).
2. `configs/rpi2-sparse_defconfig` — from `rpi2_defconfig` with `CONF_WITH_VDI_BACKEND_PLANAR=y` and `CONF_VDI_SPARSE_TABLE=y`. Exercises the generic defaults against the real RGB565 framebuffer.

Each resulting defconfig will look like:

```
# atari512 + truecolor backend (dispatcher on)
CONF_WITH_VDI_BACKEND_TRUECOLOR=y
```

```
# rpi2 + planar backend + sparse tables (dispatcher on, defaults exercised)
CONF_WITH_VDI_BACKEND_PLANAR=y
CONF_VDI_SPARSE_TABLE=y
```

(plus the machine/target lines; `make savedefconfig` writes the exact minimal set.)

- [x] **Step 8: Verify the dispatch + sparse build**

```sh
make rpi2-sparse_defconfig && make
timeout 30 qemu-system-arm -M raspi2b -bios kernel7.img -d guest_errors -serial stdio
```
Expected: builds; `obj/vdi_backend.o` present; no `guest_errors`; boots to the desktop with `fill_rect`, `draw_line`, `text_blit`, `search_*` and `raster_copy` all running through the generic defaults. (A correctly drawn desktop — text, icons, mouse — is the evidence the defaults work; the XOR rubber-band and window drags go through `default_fill_rect`/`default_raster_copy`.)

- [x] **Step 9: Verify the other three build modes still work**

```sh
make atari512_defconfig && make          # planar-only, direct calls
make rpi2_defconfig && make              # truecolor-only, direct calls
make atari512-dispatch_defconfig && make # dispatch on m68k
```
Expected: all build. `atari512-dispatch_defconfig` must fit in the 512 KB image (`ptos512k.img` produced) — if it overflows, drop the Hatari boot check to a build-only check for that config and note it in the PR.

Smoke-test the dispatch-on-m68k config under Hatari (desktop reached, `vdi_v_opnwk: ... backend=selected` KDEBUG appears):

```sh
hatari --tos ptos512k.img --machine ste --memsize 4 --sound off --run-vbls 3000
```

- [x] **Step 10: Run `make gitready` and commit**

```bash
make gitready
git add vdi/Kconfig vdi/vdi_backend.h vdi/vdi_backend.c vdi/vdi_backend_planar.c \
  vdi/vdi_backend_truecolor.c vdi/vdi_line.c vdi/vdi_raster.c configs/
git commit -m "feat(vdi): fill NULL dispatch slots with generic defaults"
```

---

### Task 3: Documentation and full verification sweep

Closes out the issue: stale comments, the smoketest skill's serial note, and a full build/smoke matrix across all three build modes.

**Files:**
- Modify: `.claude/skills/ptos-smoketest/SKILL.md` — serial pass-signal note for raspi1/raspi2
- Modify: `vdi/vdi_raster.h:105` — stale `CONF_WITH_VDI_TRUECOLOR` comment
- Modify: `vdi/vdi_fill.c:697-699` — stale comment if it names the old symbol
- Test: full matrix below

- [x] **Step 1: Update the smoketest skill's raspi serial note**

The `vdi_v_opnwk: mode layout=1 color_model=1 bpp=16 backend=selected` KDEBUG only prints on dispatch builds now (it is gated on `CONF_WITH_VDI_BACKEND_DISPATCH`). Update the two raspi rows of the pass-signal list to say: serial KDEBUG shows the mode line only when the dispatcher is built (both renderers enabled); the reliable pass signal is no `guest_errors` + the screen drawing. Note `rpi2-sparse_defconfig` prints `backend=selected`.

- [x] **Step 2: Fix stale comments**

- `vdi/vdi_raster.h` (around line 105): update any text saying `CONF_WITH_VDI_TRUECOLOR` → the dispatch symbol (match the surrounding sentence's intent).
- Grep the whole tree for `CONF_WITH_VDI_TRUECOLOR` outside `docs/superpowers/*` and fix any remaining code comment.

- [x] **Step 3: Full verification matrix**

From a clean tree:

| Config | Renderers | Expected |
|---|---|---|
| `atari512_defconfig` | planar only | builds; no `obj/vdi_backend*.o`; boots under Hatari STE to desktop |
| `rpi2_defconfig` | truecolor only | builds; only `obj/vdi_backend_truecolor.o`; boots under QEMU `raspi2b` |
| `virt-arm_defconfig` | planar only | builds; no `obj/vdi_backend*.o`; survives `timeout 5` QEMU run, no `guest_errors` |
| `atari512-dispatch_defconfig` | both (dispatch) | builds; all three `obj/vdi_backend*.o`; boots under Hatari to desktop (the `backend=selected` KDEBUG is gated on `ENABLE_KDEBUG`, off by default) |
| `rpi2-sparse_defconfig` | both (dispatch, sparse) | builds; boots under QEMU `raspi2b`, defaults exercised |

Each QEMU/Hatari run per the smoketest skill invocations. Record real output in the PR description.

- [x] **Step 4: Final checks and commit**

```bash
make gitready
git add .claude/skills/ptos-smoketest/SKILL.md vdi/vdi_raster.h vdi/vdi_fill.c
git commit -m "docs(vdi): update backend dispatch docs and smoke-test notes"
```

Then mark PR #139 ready for review (`gh pr ready`) and post the build/smoke results as a PR comment.

---

## Self-Review

**Spec coverage (issue #138):**
- Part 1 (gate on renderer count): Task 1 Steps 1-3 (Kconfig + build.mk), Steps 5-9 (caller arms), Step 12 (smoke). ✓
- Part 2 (defaults instead of NULL slots): Task 2 Steps 2-6 (raw-pixel ops + init + defaults), Step 7 (sparse test). ✓
- Acceptance "no vdi_backend*.o with one renderer": Task 1 Step 11 rows 1,2,4. ✓
- Acceptance "dispatch exercised": Task 2 Step 9 / Task 3 matrix. ✓
- Acceptance "sparse table renders via defaults": Task 2 Step 8. ✓
- `get_start_addr` stays mandatory: kept in the mandatory set, no default. ✓
- Init function: `vdi_backend_ops_init()` called idempotently from `vdi_backend_select()`. ✓

**Placeholder scan:** every step carries exact code or exact expected output; no TBD/TODO.

**Type consistency:** `get_raw_pixel`/`put_raw_pixel` use `UWORD` for the raw word throughout; `vdi_backend_ops_init(vdi_backend_ops *)` matches its prototype in `vdi_backend.h`; `default_*` functions match the `vdi_backend_ops` member signatures; the truecolor/planar direct-call prototypes in `vdi_defs.h`/`vdi_textblit.h`/`vdi_raster.h` match the non-static definitions. `WM_REPLACE`/`WM_TRANS`/`WM_XOR`/`WM_ERASE` and `MD_*`/`BM_*` constants exist in `vdi/vdi_defs.h` and `vdi/vdi_raster.h` (verified). `vdi_screen_backend()` is declared only under `CONF_WITH_VDI_BACKEND_DISPATCH`; the defaults live in `vdi_backend.c` which only builds under dispatch, so the coupling is sound.
