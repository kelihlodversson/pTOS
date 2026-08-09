# Remove Remaining CONF_CHUNKY_PIXELS Forks and v_planes != 8 Guards Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the `CONF_CHUNKY_PIXELS` Kconfig option and its code forks (dead in every configuration), and convert the `bios/raspi_screen.c` BIOS text console to 16bpp so the three PR #71 `v_planes != 8` fail-safe guards can go away (issue #95).

**Architecture:** Two independent parts. Part A deletes the chunky forks: `screen_blit()` always uses the planar `nextwrd`, `planar_text_blit()` keeps only its planar body, `normal_blit()` in `vdi/arch/arm/vdi_tblit.c` drops the byte-at-a-time chunky glyph blit, the Kconfig option and a stale `vdi_fill.c` comment go. Part B rewrites `raspi_blank_out()`/`raspi_cell_xfer()`/`raspi_neg_cell()` to write RGB565 `UWORD`s (mapping `v_col_fg`/`v_col_bg` palette indices through the existing `raspi_dflt_palette[]`) and fixes `raspi_cell_addr()`'s x-stride, then deletes the guards. Design: `docs/superpowers/specs/2026-08-09-remove-chunky-pixels-forks-design.md`.

**Tech Stack:** GNU make + kconfiglib, GCC C90 (`-std=gnu90`) for `arm-none-eabi-` and `m68k-atari-mintelf-`. Verification via the full config matrix and QEMU (`raspi2b`), per `.claude/skills/ptos-smoketest/SKILL.md`.

## Global Constraints

- C90 with GNU extensions (`-std=gnu90`): declarations at the top of a block; `/* */` comments; 4 spaces, never a hard tab. Run `make gitready` before committing.
- **`int` is 16 bits on m68k** (`-mshort`). Use `WORD`/`LONG`/`UBYTE`/`UWORD`/`ULONG`.
- `-Wundef` is on: every `#if` symbol must be defined. Never reference `CONF_CHUNKY_PIXELS` after Task 1.
- `bios/raspi_screen.c` is `MACHINE_RPI`-only (`#error` otherwise) and the framebuffer is always packed 16bpp RGB565 — the console code never needs an 8bpp path after Part B.
- The console colours `v_col_fg`/`v_col_bg` are ST default-palette indices (0-15), set by `bios/vt52.c`; `raspi_dflt_palette[]` in the same file is the 256-entry ST palette in `0x00BBGGRR` byte order (least-significant byte = red, e.g. `PRGB_RED = 0x000000ff`, `PRGB_BLUE = 0x00ff0000`).
- `v_lin_wr` = pitch in bytes, `v_cel_wr` = `v_lin_wr * form_height`, `v_cel_ht` = font height — already 16bpp-correct; only the per-pixel and per-cell x addressing are 8bpp-assuming.
- Verification before completion: build every config in `configs/`, smoke-test an RPi image, report real output.

---

### Task 1: Remove the CONF_CHUNKY_PIXELS forks and the Kconfig option

**Files:**
- Modify: `vdi/Kconfig` — delete the `CONF_CHUNKY_PIXELS` config
- Modify: `vdi/vdi_textblit.c:709-715` (`screen_blit`), `vdi/vdi_textblit.c:749-767` (`planar_text_blit`)
- Modify: `vdi/arch/arm/vdi_tblit.c:37-96` (`normal_blit`)
- Modify: `vdi/vdi_fill.c:692-700` — stale comment

- [x] **Step 1: Delete the Kconfig option**

In `vdi/Kconfig`, delete the whole `config CONF_CHUNKY_PIXELS` block (lines 12-29, from `config CONF_CHUNKY_PIXELS` through the line `bodies against it, corrupting memory instead of just being slow.`) plus the blank line before `config CONF_WITH_VDI_BACKEND_PLANAR`.

^- [x] **Step 2: Unconditional `nextwrd` in `screen_blit()`**

`vdi/vdi_textblit.c:709-715`:

```c
    vars->forecol = linea_vars.TEXTFG;
    vars->ambient = 0;          /* logically TEXTBG, but that isn't set up by the VDI */
    vars->nbrplane = linea_vars.v_planes;
    vars->nextwrd = vars->nbrplane * sizeof(WORD);
    vars->height = vars->DELY;
    vars->width = vars->DELX;
```

^- [x] **Step 3: Planar-only `planar_text_blit()`**

`vdi/vdi_textblit.c:749-767`: drop the `#if CONF_CHUNKY_PIXELS` arm; keep:

```c
void planar_text_blit(LOCALVARS *vars)
{
    vars->tddad = vars->DESTX & 0x000f;
    vars->dform = v_bas_ad;
    vars->dform += (vars->DESTX&0xfff0)>>shift_offset[linea_vars.v_planes];
    vars->dform += (UWORD)(vars->DESTY+vars->DELY-1) * (ULONG)linea_vars.v_lin_wr;
    vars->d_next = -linea_vars.v_lin_wr;

    normal_blit(vars+1, vars->sform, vars->dform);  /* call assembler helper function */
}
```

^- [x] **Step 4: Drop the chunky branch of ARM `normal_blit()`**

`vdi/arch/arm/vdi_tblit.c`: delete the `#if CONF_CHUNKY_PIXELS` block (the `WORD src_bitoffset = vars->tsdad;` declaration through the `else` before `#endif`, i.e. the whole `nbrplane == 8 && nextwrd == sizeof(WORD)` body), then flatten the outer `{`/`}` that was the `#else` arm so the function body is:

```c
void normal_blit(LOCALVARS *vars, UBYTE *src, UBYTE *dst)
{
    int x,y;
    // The caller passes a pointer to the end of the vars structure as that how
    // the original assembler version consumes it, so we simply subtract one.
    vars--;
    if (vars->nbrplane == 1 && vars->nextwrd == 2)
    {
        ...
    }
    else
    {
        // TODO implement other bitplane blits here
    }
}
```

(Inner body unchanged.)

^- [x] **Step 5: Reword the `get_color()` comment**

`vdi/vdi_fill.c:692-700`:

```c
/*
 * get_color - Get color value of requested pixel.
 *
 * NOTE: besides the planar scan-line fill paths in this file (see
 * end_pts() below), it is also called unconditionally by
 * planar_get_pixel(), which every configuration links -- either directly,
 * in a planar-only build (see the #else branch in pixelread() below), or
 * through the planar VDI backend's ops table, when the dispatcher is
 * built (see vdi_backend_planar.c).
 */
```

^- [x] **Step 6: Verify no CONF_CHUNKY_PIXELS references remain**

```sh
grep -rn "CHUNKY" vdi/ bios/ include/ configs/ Kconfig* --include=*.c --include=*.h --include=*.mk --include=Kconfig
```
Expected: no matches outside `docs/superpowers/*` history.

---

### Task 2: Convert the raspi BIOS console to 16bpp and remove the guards

**Files:**
- Modify: `bios/raspi_screen.c` — `raspi_cell_addr()`, three console routines, a new colour helper

^- [x] **Step 1: Fix `raspi_cell_addr()`'s x-stride**

`bios/raspi_screen.c:306-317` — change `x*8` to `x*8*(linea_vars.v_planes >> 3)`:

```c
UBYTE * raspi_cell_addr(int x, int y)
{
    ULONG cell_wr = linea_vars.v_cel_wr;
     /* check bounds against screen limits */
    if ( x >= linea_vars.v_cel_mx )
        x = linea_vars.v_cel_mx;           /* clipped x */

    if ( y >= linea_vars.v_cel_my )
        y = linea_vars.v_cel_my;           /* clipped y */

    return raspi_screenbase + x*8*(linea_vars.v_planes >> 3) + (cell_wr * y);
}
```

^- [x] **Step 2: Add the palette-index -> RGB565 helper**

Place directly after `raspi_cell_addr()`:

```c
/*
 * Map a console colour index -- an ST default-palette index, as stored in
 * v_col_fg/v_col_bg by vt52.c -- to the RGB565 pixel value the 16bpp
 * framebuffer expects, via raspi_dflt_palette[].
 */
static UWORD raspi_console_color(UWORD index)
{
    ULONG prgb = raspi_dflt_palette[index & 0x0f];
    UBYTE r = (UBYTE)(prgb & 0xffUL);
    UBYTE g = (UBYTE)((prgb >> 8) & 0xffUL);
    UBYTE b = (UBYTE)((prgb >> 16) & 0xffUL);

    return (UWORD)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
```

^- [x] **Step 3: Rewrite `raspi_blank_out()`**

Drop the guard and comment; write `UWORD`s; drop the dead trailing `color = (color+1) % 64;`:

```c
void raspi_blank_out (int topx, int topy, int botx, int boty)
{
    UWORD color = raspi_console_color(linea_vars.v_col_bg); /* bg colour value */
    int width, height, row, px;

    width = (botx - topx + 1) * 8;              /* pixels */
    height = (boty - topy + 1) * linea_vars.v_cel_ht;
    UWORD * addr = (UWORD *) raspi_cell_addr(topx, topy);

    if (width * sizeof(UWORD) >= raspi_screen_width_in_bytes)
    {
        for (px = 0; px < height * (raspi_screen_width_in_bytes / sizeof(UWORD)); px++)
            addr[px] = color;
    }
    else
    {
        for (row = 0; row < height; row++)
        {
            UWORD * line = addr + row * (raspi_screen_width_in_bytes / sizeof(UWORD));
            for (px = 0; px < width; px++)
                line[px] = color;
        }
    }
}
```

^- [x] **Step 4: Rewrite `raspi_cell_xfer()`**

```c
void raspi_cell_xfer(UBYTE * src, UBYTE * dst)
{
    UWORD fg;
    UWORD bg;
    int fnt_wr, line_wr, y;

    fg = raspi_console_color(linea_vars.v_col_fg);
    bg = raspi_console_color(linea_vars.v_col_bg);

    /* check for reversed foreground and background colors */
    if ( linea_vars.v_stat_0 & M_REVID ) {
        UWORD tmp = fg;
        fg = bg;
        bg = tmp;
    }

    fnt_wr = linea_vars.v_fnt_wr;
    line_wr = linea_vars.v_lin_wr / sizeof(UWORD);

    for(y = 0; y < linea_vars.v_cel_ht; y++)
    {
        UBYTE cel = *src;
        UWORD * drow = (UWORD *) dst + line_wr*y;
        int pixel;
        for(pixel = 0; pixel < 8; pixel++) {
            drow[pixel] = (cel & (0x80>>pixel))?fg:bg;
        }
        src+=fnt_wr;
    }
}
```

^- [x] **Step 5: Rewrite `raspi_neg_cell()`**

Toggle the full 8-pixel cell width per row (fixes the old one-byte-per-row sliver):

Also fix the glyph mask in Step 4: the original `(cel & (256>>pixel))` is an off-by-one (`256>>0` = `0x100` never matches a `UBYTE`, so pixel 0 was always background and bit 0 never tested); the tree-wide MSB-first convention is `0x80 >> pixel`. Neither bug was ever visible behind the 16bpp guard.

```c
void raspi_neg_cell(UBYTE * cell)
{
    int len, pixel;
    UWORD * c = (UWORD *) cell;

    linea_vars.v_stat_0 |= M_CRIT;                 /* start of critical section. */
    for(len = 0; len < linea_vars.v_cel_ht; len++)
    {
        UWORD * drow = c + (linea_vars.v_lin_wr / sizeof(UWORD)) * len;
        for(pixel = 0; pixel < 8; pixel++)
            drow[pixel] = ~drow[pixel];
    }
    linea_vars.v_stat_0 &= ~M_CRIT;                /* end of critical section. */
}
```

^- [x] **Step 6: Confirm no `v_planes != 8` guard remains**

```sh
grep -rn "v_planes" bios/raspi_screen.c
```
Expected: no `!= 8` comparison.

---

### Task 3: Verify the full config matrix and smoke-test

**Files:**
- Test: all `configs/*_defconfig`

^- [x] **Step 1: Build every configuration**

```sh
for config in configs/*_defconfig; do
  name=$(basename "$config" _defconfig)
  make "${name}_defconfig" >/dev/null && make >/dev/null || { echo "FAIL $name"; break; }
done
```
Expected: all 25 build clean (m68k via `m68k-atari-mintelf-`, ARM via `arm-none-eabi-`). Note `.config` changes trigger a full rebuild each time. **Result: all 25 `configs/*_defconfig` PASS** (amiga, amigaflop, amigaflop-vampire, amiga-kickdisk, amiga-vampire, aranym, atari192, atari256, atari512, atari512-dispatch, cartridge, firebee, firebee-prg, floppy, m548x-bas, m548x-dbug, m548x-prg, prg, rpi1, rpi2, rpi2-sparse, rpi3, rpi4, virt-arm, virt-m68k).

^- [x] **Step 2: Smoke-test an RPi build**

```sh
make rpi2_defconfig && make
timeout 30 qemu-system-arm -M raspi2b -bios kernel7.img -d guest_errors -serial stdio
```
Expected: no `guest_errors`; boots to the desktop. Boot banner text (the BIOS console) should now be visible on screen — the first time the console draws on 16bpp RPi.

**Result:** boots clean (only benign `bcm2835_systmr_write: read-only ofs 0x4`). Screendump analysis of the framebuffer confirms the welcome screen renders correctly and centered: white bg (ST index 0), black text (index 15), red logo/values (index 1), gray box borders; logo at rows 12-17, box at rows 18/26, messages at 28-31, "Hold <Shift>" inverse bar at row 33 — all exact 8x16 cell rows, so cell addressing, glyph bit order (`0x80>>pixel`, MSB-first) and colours are all correct. The one line of text at cell row 0 is the leftover `Scanning USB devices.... Please wait...` from `usb_hub_init()` (`usb/usb_hub.c:635`); `initinfo()` centers the banner below without clearing above, so it remains visible — faithful to upstream EmuTOS behavior on real hardware, not a rendering bug.

^- [x] **Step 3: Final greps and gitready**

```sh
grep -rn "CHUNKY" . --exclude-dir=obj --exclude-dir=.git --exclude-dir=docs/superpowers
grep -rn "v_planes != 8" .
make gitready
```
Expected: no matches.

^- [x] **Step 4: Commit**

```bash
git add vdi/Kconfig vdi/vdi_textblit.c vdi/arch/arm/vdi_tblit.c vdi/vdi_fill.c bios/raspi_screen.c \
  docs/superpowers/specs/2026-08-09-remove-chunky-pixels-forks-design.md
git commit -m "chore(vdi): remove CONF_CHUNKY_PIXELS forks and raspi console guards"
```

Then mark the PR ready for review (`gh pr ready`) and post the build/smoke results as a PR comment.

---

## Self-Review

**Spec coverage (issue #95):**
- Acceptance "`CONF_CHUNKY_PIXELS` no longer appears anywhere in the tree": Task 1 Steps 1-5, verified in Task 3 Step 3. ✓
- Acceptance "no `v_planes != 8` fail-safe guard remains": Task 2, verified Task 3 Step 3. ✓
- Acceptance "full config matrix still builds clean": Task 3 Step 1. ✓
- Part B guard removal is safe only because the console is converted to 16bpp in the same task — deleting the guard without the conversion would re-corrupt the framebuffer (see the design doc). ✓
- `nextwrd` readers: only `normal_blit()` (`vdi/arch/arm/vdi_tblit.c`), reached only via `planar_text_blit()`; `truecolor_text_blit()` computes its own addressing, so the unconditional `nextwrd = nbrplane * sizeof(WORD)` cannot affect RPi text. ✓

**Placeholder scan:** every step carries exact code or exact expected output; no TBD/TODO (the `// TODO implement other bitplane blits here` is pre-existing and out of scope).

**Type consistency:** `raspi_console_color(UWORD)` returns `UWORD`; `raspi_blank_out`/`raspi_cell_xfer`/`raspi_neg_cell` declare locals at block top (C90). `v_planes >> 3` = 2 at 16bpp, 1 at 8bpp; `sizeof(UWORD)` = 2 on both m68k and ARM.
