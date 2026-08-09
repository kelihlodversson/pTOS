# Truecolor-Correct Color Icon (CICON) Rendering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make colour icons (CICONBLK, from issue #106) render correctly on packed truecolor screens (Raspberry Pi), using the backend abstraction rather than depth checks, and ship a config-gated desktop hook that verifies both the truecolor path (rpi2/QEMU) and the previously-unexercised planar path (#106, Hatari STE) end-to-end (issue #107).

**Architecture:** In `transform_all_cicons()` (aes/gemrslib.c) add a truecolor branch that converts the selected CICON's standard-format colour planes directly to a packed buffer of `w*h` RGB565 `UWORD`s (`num_planes = 1`), instead of expanding to `gl_nplanes` (=16 on RPi) planes and hitting the backend's plane-count sanity check. Three supporting pieces are needed: (a) `setup_info()` recomputes the packed source row stride from `fd_w` (the AES's `gsx_fix()` gives memory MFDBs a planar `fd_wdwidth = w/16`, which would make the opaque copy read `w/8` bytes per row instead of `2w`); (b) a backend-owned write-mode helper `vdi_colour_blit_mode()` so `gr_colourblit()` stops hard-coding `S_OR_D` (whose packed meaning `apply_raster_op` = `src|dst` corrupts RGB565) and uses `BM_S_ONLY` (=3, `D'=S`) on truecolor; (c) a small extern `vdi_truecolor_screen()` so the AES can query the backend (the `static inline vdi_screen_is_truecolor()` in `vdi/vdi_backend.h` is not linkable from the AES, whose include path never reaches `vdi/`). The test hook loads an embedded 'new format' RSC via a new in-memory loader `rs_loadmem()` and `objc_draw()`s its G_CICON over the desktop background; the RSC byte array is produced by a host generator (`tools/mkciconrsc.py`) and checked in. The standalone m68k `.prg` (design B4) is **deferred** per user decision — the desktop hook verifies both backends.

**Tech Stack:** GNU make + kconfiglib, GCC C90 (`-std=gnu90`) for `arm-none-eabi-` and `m68k-atari-mintelf-`. Verification via QEMU (`raspi2b`, screendump through the monitor) and Hatari STE (`--avirecord`), per `.claude/skills/ptos-smoketest/SKILL.md`.

## Global Constraints

- C90 with GNU extensions (`-std=gnu90`): declarations at the top of a block; `/* */` comments. 4 spaces, never a hard tab. Run `make gitready` before committing.
- **`int` is 16 bits on m68k** (`-mshort`). Use `WORD`/`LONG`/`UBYTE`/`UWORD`/`ULONG`; suffix constants that must survive on m68k (`(LONG)w * h`).
- `-Wundef` is on: every `#if` symbol must be defined. Feature symbols are always defined `0`/`1` and tested with `#if` (see `CLAUDE.md` naming table). Never edit `obj/autoconf.h` / `obj/auto.conf`.
- `-Wmissing-prototypes` is on: every new non-static function needs a prototype in a header the defining TU includes.
- The planar path must stay **byte-identical**: the A1 truecolor branch is `#if CONF_WITH_VDI_BACKEND_TRUECOLOR` + runtime query, and `vdi_colour_blit_mode()` returns `S_OR_D` (=`BM_S_OR_D`=7) on planar.
- No new depth checks in AES icon code: `grep -rn "v_planes" aes/gemrslib.c aes/gemgraf.c` must show no *new* uses, and `grep -rn "planes > 8" aes/` no matches.
- The truecolor decision belongs to the backend: AES code queries `vdi_truecolor_screen()` (extern helper), never `CONF_WITH_VDI_BACKEND_TRUECOLOR` alone except as the compile-time `#if` guard on the A1 branch.
- `bb_save`/`bb_restore` must not change behaviour: the `gl_tmp` buffer is `fd_stand=TRUE` (gemgsxif.c:322), and its save/restore strides are self-consistent — the `setup_info()` stride fix is gated on `!src->fd_stand`.
- Verification before completion: build the affected configs and run the smoke tests listed in each task; report real output, not assumptions.

---

### Task 1: A1 — truecolor branch in `transform_all_cicons()` + `pack_cicon()` + the AES-side backend queries

Delivers the core conversion. The branch runs only on a packed-truecolor screen; planar builds compile it out entirely.

**Files:**
- Modify: `aes/gemrslib.c` — A1 branch in `transform_all_cicons()` (after line 407), new `pack_cicon()`/`pack_planes()` helpers
- Modify: `vdi/vdi_backend.h` — declare `vdi_truecolor_screen()` and `vdi_colour_blit_mode()` (Task 2 defines the latter; declare both here so `-Wmissing-prototypes` is satisfied in the VDI)
- Modify: `include/gsxdefs.h` — AES-side declarations of `vdi_truecolor_screen()`, `vdi_colour_blit_mode()`, `vdi_truecolor_pixel_for_index()` (the AES cannot include `vdi/vdi_backend.h`; `include/` is on the AES include path and `gemrslib.c`/`gemgraf.c` include `gsxdefs.h` via `gemgraf.h`)
- Test: `configs/rpi2_defconfig` (truecolor-only), `configs/atari512_defconfig` (planar-only, must stay byte-identical)

**Interfaces:**
- Consumes: `cicon->col_data`/`sel_data` (standard plane-major layout: per scanline `w/16` words per plane, MSB-first), `cicon->num_planes` (before it is overwritten), `cicon->sel_data` (nonzero = selected variant present), `vdi_truecolor_pixel_for_index()` (extern, `vdi/vdi_backend_truecolor.c:269`, reads the seeded `tc_palette[]`).
- Produces: static `pack_cicon(CICON *cicon, WORD w, WORD h)` returning BOOL; static `pack_planes(const WORD *data, UWORD *pix, WORD planes, WORD w, WORD h)`; AES-visible externs `BOOL vdi_truecolor_screen(void)`, `WORD vdi_colour_blit_mode(void)`.

- [ ] **Step 1: AES-side declarations in `include/gsxdefs.h`**

Append (near the other VDI-facing externs, e.g. after the `vrn_trnfm`/`gsx_fix` prototypes):

```c
/*
 * Backend-owned screen queries for the packed-truecolor backend.  The
 * AES may not include vdi/vdi_backend.h (its inline vdi_screen_is_truecolor()
 * is not linkable), so these thin externs are the interface -- the
 * definitions live in vdi/vdi_raster.c (vdi_colour_blit_mode) and
 * vdi/vdi_backend_truecolor.c (vdi_truecolor_screen).
 */
#if CONF_WITH_VDI_BACKEND_TRUECOLOR
BOOL vdi_truecolor_screen(void);
UWORD vdi_truecolor_pixel_for_index(WORD index);
#endif
WORD vdi_colour_blit_mode(void);
```

(`vdi_colour_blit_mode()` is declared unconditionally: `gr_colourblit()` calls it in every build.)

- [ ] **Step 2: Declarations in `vdi/vdi_backend.h`**

After the existing `vdi_truecolor_pixel_for_index()` block (line ~210), add:

```c
/*
 * Extern backend query + write-mode helpers, visible to the AES via
 * include/gsxdefs.h.  vdi_colour_blit_mode() is defined in vdi/vdi_raster.c
 * (always built); vdi_truecolor_screen() in vdi_backend_truecolor.c (built
 * exactly when this backend is).  See gr_colourblit() (aes/gemgraf.c).
 */
BOOL vdi_truecolor_screen(void);
WORD vdi_colour_blit_mode(void);
```

- [ ] **Step 3: The A1 branch in `transform_all_cicons()`**

In `aes/gemrslib.c`, insert between line 407 (`h = ciconblk->monoblk.ib_hicon;`) and line 408 (`data_size = ...`):

```c
#if CONF_WITH_VDI_BACKEND_TRUECOLOR
        /*
         * Packed-truecolor screen: there are no bitplanes to expand to.
         * Convert the standard-format colour planes straight to one
         * RGB565 pixel per bit (w*h UWORDs, num_planes=1), the layout
         * truecolor_raster_copy()'s opaque path reads.  Skipping
         * expand_cicondata()/transform_cicon() here is what avoids the
         * 16-plane interleaved form that setup_info() cannot interpret.
         * Pixels whose colour code is 0 keep their own palette colour
         * (the icon background -- the mask blit in gr_gicon() is what
         * paints the object's background over the shape); this is the
         * deliberate truecolor look, see the design doc.
         */
        if (vdi_truecolor_screen())
        {
            cicon->next_res = NULL;
            if (!pack_cicon(cicon, w, h))
                ciconblk->mainlist = NULL;  /* no colour for this icon */
            continue;
        }
#endif
```

- [ ] **Step 4: `pack_cicon()` and `pack_planes()`**

Add these static helpers in `aes/gemrslib.c` just before `transform_all_cicons()` (line 390), inside `#if CONF_WITH_VDI_BACKEND_TRUECOLOR`:

```c
#if CONF_WITH_VDI_BACKEND_TRUECOLOR
/*
 *  pack one plane-major colour array (the RSC layout transform_cicon()
 *  reads) into w*h packed pixels: each pixel's colour code is the OR of
 *  its bit across the planes, then mapped to RGB565 via the physical
 *  workstation's palette.
 *
 *  The bit order below -- plane p contributes bit (1<<p) of the colour
 *  code -- is the calibration constant the design calls out; the first
 *  screencap (Task 6) confirms or flips it to (1 << (planes-1-p)).
 */
static void pack_planes(const WORD *data, UWORD *pix, WORD planes, WORD w, WORD h)
{
    WORD mono_words = w / 16;
    WORD x, y, p;

    for (y = 0; y < h; y++)
    {
        const WORD *rowbase = data + (LONG)y * mono_words;

        for (x = 0; x < w; x++)
        {
            UWORD mask = 0x8000 >> (x & 0x0f);
            WORD code = 0;

            for (p = 0; p < planes; p++)
            {
                const WORD *plane = rowbase + (LONG)p * mono_words * h;
                if (plane[x >> 4] & mask)
                    code |= (WORD)(1 << p);
            }
            *pix++ = vdi_truecolor_pixel_for_index(code);
        }
    }
}

/*
 *  pack_cicon: convert the selected CICON's standard-format colour data
 *  to the packed-truecolor layout.  The normal and (optional) selected
 *  buffers are packed back-to-back in a single allocation so
 *  free_cicon_buffers() (which frees only cicon->col_data) still works.
 */
static BOOL pack_cicon(CICON *cicon, WORD w, WORD h)
{
    LONG pixels = (LONG)w * h;
    UWORD *packed;

    packed = dos_alloc_anyram(pixels * (cicon->sel_data ? 2 : 1) * sizeof(UWORD));
    if (!packed)
        return FALSE;

    pack_planes(cicon->col_data, packed, cicon->num_planes, w, h);
    cicon->col_data = packed;

    if (cicon->sel_data)
    {
        UWORD *selbuf = packed + pixels;

        pack_planes(cicon->sel_data, selbuf, cicon->num_planes, w, h);
        cicon->sel_data = selbuf;
    }

    cicon->num_planes = 1;
    return TRUE;
}
#endif
```

- [ ] **Step 5: Verify both build modes still compile**

```sh
make distclean
make atari512_defconfig && make    # planar-only: A1 branch compiled out
make rpi2_defconfig && make        # truecolor-only: branch live, helpers resolve
```
Expected: both build. On atari512, `aes/gemrslib.c` must contain no reference to `pack_cicon`/`vdi_truecolor_screen` (compiled out).

---

### Task 2: A2 — packed source-stride fix in `setup_info()` + `vdi_colour_blit_mode()` + A3

Delivers the two backend-side pieces the packed blit needs, and routes `gr_colourblit()` through the backend-owned mode.

**Files:**
- Modify: `vdi/vdi_raster.c` — stride fix in `setup_info()` (source-MFDB branch, lines 801-806); define `vdi_colour_blit_mode()` (near `setup_info()`)
- Modify: `aes/gemgraf.c` — `gr_colourblit()` (line 714) uses the helper
- Test: `configs/rpi2_defconfig` (build + boot), `configs/atari512_defconfig` (build; `gr_colourblit` must still pass 7)

**Interfaces:**
- Consumes: `MFDB.fd_w` / `fd_stand` / `fd_nplanes` (set by `gsx_fix()`, gemgraf.c:339-357), `raster->transparent` (opaque iff `vdi_vro_cpyfm`), `vdi_screen_is_truecolor()` inline (vdi_backend.h).
- Produces: `WORD vdi_colour_blit_mode(void)` — `BM_S_ONLY` on truecolor, `BM_S_OR_D` otherwise; corrected `s_nxln` for opaque packed memory sources.

- [ ] **Step 1: `setup_info()` source-stride fix**

In `vdi/vdi_raster.c`, inside `setup_info()`'s `if (src->fd_addr)` branch (lines 801-806), after the existing three assignments:

```c
#if CONF_WITH_VDI_BACKEND_TRUECOLOR
        /*
         * Packed-truecolor source forms hold one whole pixel per word,
         * so a row of fd_w pixels spans fd_w*2 bytes.  gsx_fix() sizes
         * memory MFDBs with the planar convention fd_wdwidth = fd_w/16;
         * using that here would make s_nxln 8 times too small and the
         * opaque copy would read w/8 bytes per row instead of 2w.  Only
         * the opaque device-dependent case (the AES's packed colour-icon
         * data -- gr_colourblit()) wants the packed stride: transparent
         * sources are 1bpp masks whose fd_wdwidth stride is correct, and
         * fd_stand sources (bb_save/bb_restore's gl_tmp) keep their own
         * consistent fd_wdwidth-based layout.
         */
        if (vdi_screen_is_truecolor() && !raster->transparent && !src->fd_stand)
            info->s_nxln = src->fd_w * 2;
#endif
```

- [ ] **Step 2: define `vdi_colour_blit_mode()` in `vdi/vdi_raster.c`**

Add this function (e.g. right before `setup_info()`); it must be built in every configuration:

```c
/*
 *  The write mode gr_colourblit() must pass for colour-icon data on the
 *  current screen (see aes/gemgraf.c).  With packed data the blit must
 *  replace pixels outright: S_OR_D would OR RGB565 values
 *  (apply_raster_op's BM_S_OR_D = src|dst), corrupting them.  The
 *  planar backend needs S_OR_D (its data planes are mask-ANDed and
 *  composed over the mask blit).  BM_S_ONLY/S_OR_D are numerically equal
 *  to the AES's S_ONLY/S_OR_D.
 */
WORD vdi_colour_blit_mode(void)
{
#if CONF_WITH_VDI_BACKEND_TRUECOLOR
    if (vdi_screen_is_truecolor())
        return BM_S_ONLY;
#endif
    return BM_S_OR_D;
}
```

- [ ] **Step 3: `gr_colourblit()` uses the helper**

In `aes/gemgraf.c` (line 714), replace `vro_cpyfm(S_OR_D, ...)` with:

```c
    vro_cpyfm(vdi_colour_blit_mode(), pxyarray, &gl_src, &gl_dst);
```

Update the comment above `gr_colourblit()` to note the mode is backend-owned (planar `S_OR_D`, packed truecolor `BM_S_ONLY`).

- [ ] **Step 4: Verify build + boot smoke**

```sh
make distclean
make atari512_defconfig && make
make rpi2_defconfig && make
timeout 30 qemu-system-arm -M raspi2b -bios kernel7.img -d guest_errors -serial stdio
```
Expected: both build; rpi2 boots to the desktop with no `guest_errors` (nothing exercises the new stride yet — the desktop has no CICONs — so this is a regression check that the OLD mono-icon rendering is untouched).

---

### Task 3: B2 — `rs_loadmem()`, an in-memory RSC load (aes/gemrslib.c)

Extract the parsing body of `rs_readit()` into a shared helper so an already-in-memory RSC image can be parsed without a filesystem (rpi2 has none in any config). `rs_readit()` becomes a thin file-reading wrapper.

**Files:**
- Modify: `aes/gemrslib.c` — extract `rs_parse()`, add `rs_own_global` static + `rs_loadmem()`
- Modify: `aes/gemrslib.h` — declare `OBJECT *rs_loadmem(AESGLOBAL *pglobal, const void *rsmem)`

**Interfaces:**
- Consumes: the existing `fix_trindex()`/`fix_cicons()`/`fix_tedinfo()`/`fix_nptrs()` chain; `rs_fixit()` (which runs `fix_objects()`, needed to set the G_CICON's `ob_spec` to the ciconblk pointer).
- Produces: `OBJECT *rs_loadmem(AESGLOBAL *pglobal, const void *rsmem)` — parses a complete 'new format' RSC image held in memory (copies it, fixes it up exactly like `rs_readit()`, runs `rs_fixit()`), returns the tree (`ap_ptree[0]`) or NULL. `pglobal` may be NULL: an internal static AESGLOBAL is used (the desktop hook has no app context; the desktop never calls the AES resource manager afterward, and a real app's `rsrc_load` re-sets `rs_global`, so the static `rs_global` pointing at `rs_own_global` in between is harmless).

- [ ] **Step 1: extract `rs_parse()`**

Move everything after the "read it all in" `dos_read` in `rs_readit()` (lines 764-786: the `/* init global */` block at 764-767, `fix_trindex()`, the `#if CONF_WITH_COLOUR_ICONS` `fix_cicons()`, `fix_tedinfo()`, and the six `fix_nptrs()` calls, including the `WORD ibcnt;` declaration) into:

```c
/*
 *  parse & fix up a complete resource image already held in memory
 *  (shared by rs_readit() and rs_loadmem())
 */
static void rs_parse(AESGLOBAL *pglobal, RSHDR *hdr, LONG rslsize)
{
    WORD ibcnt;

    /* init global */
    rs_global = pglobal;
    rs_global->ap_rscmem = hdr;
    rs_global->ap_rsclen = rslsize;

    /*
     * transfer RT_TRINDEX to global and turn all offsets from
     * base of file into pointers
     */
    fix_trindex();
#if CONF_WITH_COLOUR_ICONS
    fix_cicons();
#endif
    fix_tedinfo();
    ibcnt = hdr->rsh_nib;
    fix_nptrs(ibcnt, R_IBPMASK);
    fix_nptrs(ibcnt, R_IBPDATA);
    fix_nptrs(ibcnt, R_IBPTEXT);
    fix_nptrs(hdr->rsh_nbb, R_BIPDATA);
    fix_nptrs(hdr->rsh_nstring, R_FRSTR);
    fix_nptrs(hdr->rsh_nimages, R_FRIMG);
}
```

Replace the removed tail of `rs_readit()` with a call to `rs_parse(pglobal, rs_hdr, rslsize);` and delete the now-unused `WORD ibcnt;` from `rs_readit()`.

- [ ] **Step 2: add `rs_loadmem()`**

Add the static global and the function after `rs_readit()`:

```c
static AESGLOBAL rs_own_global;

/*
 *  rs_loadmem: load a resource image already held in memory
 *
 *  rsmem must be a complete 'new format' resource file image -- the same
 *  bytes rsrc_load() would read from disk; it is copied to freshly
 *  allocated memory and parsed exactly like rs_readit() does, then fixed
 *  up like rs_load() (including fix_objects(), which wires G_CICON
 *  ob_spec's to their CICONBLKs).  Returns the root tree, or NULL.
 *
 *  pglobal may be NULL: an internal AESGLOBAL is then used.  This is the
 *  desktop test hook's path (CONF_WITH_VDI_CICON_TEST) -- it has no
 *  application context of its own.
 */
OBJECT *rs_loadmem(AESGLOBAL *pglobal, const void *rsmem)
{
    const RSHDR *hdr = (const RSHDR *)rsmem;
    RSHDR *buf;
    LONG rslsize;

    rslsize = hdr->rsh_rssize;

#if CONF_WITH_COLOUR_ICONS
    /* for 'new format' resources, rsh_rssize is the offset of the
     * extension array, whose first LONG is the true total length (see
     * get_ciconblkptr()) -- mirroring rs_readit()'s size probe */
    if (hdr->rsh_vrsn & NEW_FORMAT_RSC)
        rslsize = *(const LONG *)((const BYTE *)rsmem + hdr->rsh_rssize);
#endif

    buf = (RSHDR *)dos_alloc_anyram(rslsize);
    if (!buf)
        return NULL;

    memcpy(buf, rsmem, rslsize);

    if (!pglobal)
        pglobal = &rs_own_global;
    rs_parse(pglobal, buf, rslsize);
    rs_fixit(pglobal);

    return pglobal->ap_ptree[0];
}
```

- [ ] **Step 3: declare it in `aes/gemrslib.h`**

After `WORD rs_load(AESGLOBAL *pglobal, BYTE *rsfname);` (line 35):

```c
OBJECT *rs_loadmem(AESGLOBAL *pglobal, const void *rsmem);
```

(`OBJECT` is already required by the `ap_ptree` member in this header; callers include `obdefs.h`.)

- [ ] **Step 4: build check**

```sh
make rpi2_defconfig && make
make atari512_defconfig && make
```
Expected: both build (no callers yet, so the change is a pure refactor; `rs_readit()`/`rs_load()` semantics unchanged).

---

### Task 4: B1 — the test RSC generator + checked-in byte array

A host-side generator produces the minimal 'new format' RSC for the test tree (ROOT + one G_CICON) as a C byte array, emitted in both target byte orders (m68k is big-endian, ARM little-endian; the parser reads native structs/words, so the embedded bytes must match the running target).

**Files:**
- Create: `tools/mkciconrsc.py`
- Create (generated, checked in): `desk/cicontest_rsc.c`

**Interfaces:**
- Consumes (implicitly): the parser's exact expectations in gemrslib.c — RSHDR layout (rsdefs.h, `rsh_rssize` at byte offset 32 is a UWORD holding the extension-array offset in new-format files); `fixup_all_ciconblks()`'s contiguous layout after each CICONBLK struct (`mono pdata`, `mono pmask`, 12-byte text, then per CICON: struct + `col_data` (`mono_words*num_planes`), `col_mask` (`mono_words`), optional `sel_data`/`sel_mask`); `fixup_colour_icons()` reading `next_res == 1L` for "more CICONs follow"; `get_ciconblkptr()` reading `extarray[0]=true length`, `extarray[1]=CICONBLK ptr table offset`, `extarray[2]=0`; `fix_cicons()`/`fix_objects()` walking the ptr table until `-1L`.
- Produces: `desk/cicontest_rsc.c` defining `extern const UBYTE cicontest_rsc[];` (and a length macro), with the array selected by `__BYTE_ORDER__`.

- [ ] **Step 1: write `tools/mkciconrsc.py`**

Layout the RSC with the section offsets in the RSHDR. Structure (all LONGs little/big as selected by the emit):

```
RSHDR (34 bytes):
  rsh_vrsn    = 0x0004                    /* NEW_FORMAT_RSC */
  rsh_object  = offset of OBJECT array
  rsh_tedinfo/iconblk/bitblk/frstr/string/imdata/frimg = 0
  rsh_trindex = offset of tree-index array (one LONG)
  rsh_nobs    = 2, rsh_ntree = 1, all other counts 0
  rsh_rssize  = offset of the extension array

OBJECT[0] ROOT:  G_IBOX (invisible box, not drawn), ob_flags NONE,
                 ob_spec 0, ob_x/y = 0,0, ob_w/h = 10,6 (char units),
                 ob_head = 1, ob_tail = 1, ob_next = 0
OBJECT[1] CICON: G_CICON, ob_flags NONE, ob_spec.index = 0 (ciconblkptr
                 table index), ob_x/y = 2,1, ob_w/h = 4,4 (char units),
                 ob_head = -1, ob_tail = -1, ob_next = 0

trindex: LONG offset of OBJECT[0]

CICONBLK ptr table: LONG offset_of_ciconblk, LONG 0xFFFFFFFFL

CICONBLK block (contiguous):
  CICONBLK (32 bytes) = monoblk (28) + mainlist (LONG count = 1)
    monoblk: ib_char = 0x0000 (fg=0,bg=0,ch=0), ib_xchar/ychar = 0,
             ib_xicon/yicon = 0, ib_wicon/hicon = 32,
             ib_xtext = 0, ib_ytext = 32, ib_wtext = 32, ib_htext = 8,
             ib_pmask/ib_pdata/ib_ptext = 0 (fixed up)
  mono pdata: mono_words words            (mono_words = 32/16*32 = 64)
  mono pmask: mono_words words
  icon text:  12 bytes (e.g. "CICON test", NUL padded)
  CICON (24 bytes): num_planes = 4, col_data/col_mask/sel_data/sel_mask
             = 0, next_res = 0   /* no selected variant, single CICON */
  col_data: mono_words * 4 words, plane-major
  col_mask: mono_words words

Extension array (after everything):
  extarray[0] = total RSC length
  extarray[1] = offset of CICONBLK ptr table
  extarray[2] = 0L
```

Icon content, chosen so the plane->code bit order (pack_planes, Task 1) is observable on the first screencap:
- 4 colour planes, one colour per plane: pixels in the top-left quarter of the 32x32 icon set plane 0 (code 1), top-right plane 1 (code 2), bottom-left plane 2 (code 4), bottom-right plane 3 (code 8). Pick two palette indices per code from the standard 16 so both planar (direct indices) and truecolor (palette-mapped RGB565) show distinct colours — e.g. codes 1/2/4/8 map to RED/GREEN/BLUE/YELLOW (indices 2/3/4/6) with the icon background (code 0) = the desktop's background colour.
- `col_mask`/mono pmask: filled 32x32 (all bits set) — simplest shape; background pixels then come from the code-0 colour.
- Mono pdata: filled square (all bits set).
- Both `cicontest_rsc_bigendian[]` and `cicontest_rsc_littleendian[]` are emitted; `cicontest_rsc` is `#if`-selected by `__BYTE_ORDER__` (the pattern already used for `bfobspec` in include/obdefs.h:227). Add a header comment with the regenerate command (`python3 tools/mkciconrsc.py > desk/cicontest_rsc.c`) and a note that the background colour is set to match the desktop's background (calibrate in Task 6).

- [ ] **Step 2: generate and check in `desk/cicontest_rsc.c`**

Run the generator, eyeball the output (RSHDR offsets must be internally consistent), and check it in. Size target: ~1-2 KB per byte order.

- [ ] **Step 3: self-check the layout against the parser**

Walk `fixup_all_ciconblks()`/`fixup_colour_icons()` (gemrslib.c:251-280, 209-243) with the emitted offsets and confirm: `mono_words` from `ib_wicon/16 * ib_hicon` = 64; the CICON block starts right after the 12-byte text; `col_data`/`col_mask`/`sel_data` offsets land on the emitted words; the ptr table ends in `-1L`; `extarray[0]` equals the total file length. Fix the generator until the arithmetic matches.

---

### Task 5: B3 — config-gated desktop hook

Draws the embedded test CICON over the desktop background so the normal `obj_draw()` -> `gr_gicon()` -> `gr_colourblit()` path runs on both backends.

**Files:**
- Modify: `desk/Kconfig` — `CONF_WITH_VDI_CICON_TEST`
- Modify: `desk/build.mk` — gate `cicontest_rsc.o`
- Modify: `desk/deskmain.c` — include `../aes/gemrslib.h`, hook call at line 1684, `desktop_cicon_test()` function
- Test: `configs/rpi2_defconfig` + `make menuconfig` (enable the option)

**Interfaces:**
- Consumes: `rs_loadmem(NULL, cicontest_rsc)` (Task 3); the desk's own `objc_draw(OBJECT*, WORD, WORD, WORD, WORD, WORD, WORD)` wrapper (gembind.c:425, opcode OBJC_DRAW = 42); `gl_width`/`gl_height` (gsxdefs.h, already included by deskmain.c); `ROOT`/`MAX_DEPTH` (obdefs.h).
- Produces: `CONF_WITH_VDI_CICON_TEST` (Kconfig, default n, no shipped defconfig); `desk_cicon_test` in deskmain.c.

- [ ] **Step 1: Kconfig option**

In `desk/Kconfig`, before `endmenu`:

```kconfig
config CONF_WITH_VDI_CICON_TEST
	bool "Embedded colour-icon rendering test"
	depends on CONF_WITH_AES && CONF_WITH_COLOUR_ICONS
	default n
	help
	  Test hook for issue #107: embeds a hand-built 'new format' RSC
	  with a single G_CICON and draws it over the desktop background at
	  boot, exercising gr_gicon() -> gr_colourblit() on both the packed
	  truecolor (RPi) and the planar (Atari) paths.  For verification
	  only -- do not enable in production images.
```

(`CONF_WITH_COLOUR_ICONS` defaults y in aes/Kconfig:52; the planar/truecolor backends are covered by vdi/Kconfig defaults.)

- [ ] **Step 2: build.mk**

In `desk/build.mk`, add to the object list (the generated RSC is only linked when the test is enabled):

```make
obj-$(CONF_WITH_VDI_CICON_TEST) += cicontest_rsc.o
```

- [ ] **Step 3: the hook in `desk/deskmain.c`**

Add the include (with the other `../bios/`-style includes, after line 40):

```c
#if CONF_WITH_VDI_CICON_TEST
#include "../aes/gemrslib.h"    /* rs_loadmem() */
#endif
```

Add the hook function before `main()` (or near the desktop startup helpers; it needs `objc_draw`, `KDEBUG` from `kprint.h`):

```c
#if CONF_WITH_VDI_CICON_TEST
/*
 *  issue #107 test hook: draw the embedded colour icon over the desktop
 *  background.  No appl_init()/appl_exit() here -- the desktop already
 *  called appl_init() (ap_exit() would tear the desktop down).  Loads
 *  the RSC in memory (rpi2 has no filesystem) and draws via the normal
 *  obj_draw() path so the truecolor and planar renderings both go
 *  through gr_gicon().
 */
static void desk_cicon_test(void)
{
    OBJECT *tree;

    tree = rs_loadmem(NULL, cicontest_rsc);
    if (!tree)
    {
        KDEBUG(("desk_cicon_test: in-memory RSC load failed\n"));
        return;
    }

    objc_draw(tree, ROOT, MAX_DEPTH, 0, 0, gl_width, gl_height);
}
#endif
```

Insert the call at line 1684, between `wind_update(END_UPDATE);` (line 1683) and the `/* get ready for main loop */` comment:

```c
#if CONF_WITH_VDI_CICON_TEST
    desk_cicon_test();
#endif
```

- [ ] **Step 4: build with the option on**

```sh
make rpi2_defconfig
make menuconfig   # enable CONF_WITH_VDI_CICON_TEST, save
make
```
Expected: builds; `obj/cicontest_rsc.o` present. (Do not save a defconfig for this.)

---

### Task 6: Verification sweep + bit-order calibration

**Files:** none (may tweak `tools/mkciconrsc.py` and `desk/cicontest_rsc.c` if calibration flips the bit order; may tweak the icon background colour).

- [ ] **Step 1: truecolor end-to-end (rpi2, QEMU)**

From `configs/rpi2_defconfig` + `CONF_WITH_VDI_CICON_TEST=y`:

```sh
make
(echo "screendump /tmp/cicon.ppm"; sleep 2; echo "quit") | \
  timeout 45 qemu-system-arm -M raspi2b -bios kernel7.img \
    -d guest_errors -serial file:/tmp/cicon.serial.log -display none -monitor stdio
```

Expected: no `guest_errors`; serial log shows no `desk_cicon_test: in-memory RSC load failed`; `/tmp/cicon.ppm` shows the icon (convert to PNG for viewing, or sample the pixel at the icon's location). Record the observed colours.

- [ ] **Step 2: calibrate the plane bit order**

If the four icon regions show the wrong colour mapping (e.g. plane 0 renders as the colour that was assigned to plane 3), flip `(1 << p)` to `(1 << (planes - 1 - p))` in `pack_planes()` (Task 1 Step 4), or change which generator plane carries which colour, rebuild, and re-screendump. **Record the final mapping in this plan and in the PR** (this is the design's calibration constant).

- [ ] **Step 3: planar end-to-end (Hatari STE, atari512)**

From `configs/atari512_defconfig` + `CONF_WITH_VDI_CICON_TEST=y`:

```sh
make
hatari --tos ptos512k.img --machine ste --memsize 4 --sound off \
  --avirecord --avi-vcodec png --avi-file /tmp/cicon-planar.avi --run-vbls 3000
```

Expected: desktop reached (~VBL 954 baseline per smoketest skill; allow ~60 s for the extra draw); extract a frame and confirm the icon renders in its known colours through the #106 planar path (this is the first-ever exercise of that path). If the icon's code-0 background doesn't blend with the STE desktop background, adjust the generator's background colour and regenerate.

- [ ] **Step 4: full config matrix**

```sh
make distclean
for c in atari512 rpi1 rpi2 virt-arm virt-m68k; do
  make ${c}_defconfig && make || echo "FAIL: $c"
done
```
Expected: all build (test hook off by default; `cicontest_rsc.o` absent). Also `make atari512_defconfig` + `CONF_WITH_VDI_CICON_TEST=y` must still fit `ptos512k.img` (used in Step 3).

- [ ] **Step 5: no-depth-check greps**

```sh
grep -rn "v_planes" aes/gemrslib.c aes/gemgraf.c
grep -rn "planes > 8" aes/
```
Expected: the `grep -rn "v_planes"` output must show only the pre-existing uses (none added by this work; the AES additions use `vdi_truecolor_screen()`/`vdi_colour_blit_mode()` only); the second grep has no matches.

- [ ] **Step 6: `make gitready` and commit**

Commit in logical pieces (Tasks 1+2 backend/AES rendering; Task 3 loader; Tasks 4+5 test hook; docs), each with `make gitready` first. Then mark the draft PR ready (`gh pr ready`) and post the two screencaps plus the calibration note as a PR comment.

---

## Self-Review

**Spec coverage (issue #107):**
- "Colour icons render correctly on packed truecolor": A1 (Task 1) + stride fix + `BM_S_ONLY` (Task 2). ✓
- "No depth checks in AES icon code": A1 gates on `#if CONF_WITH_VDI_BACKEND_TRUECOLOR` + `vdi_truecolor_screen()`, A3 on `vdi_colour_blit_mode()`; Task 6 Step 5 verifies. ✓
- "Planar path stays byte-identical": A1 branch compiled out / runtime-false; `vdi_colour_blit_mode()` returns `BM_S_OR_D` (=7) on planar; Task 2 Step 4 + Task 6 Step 3 verify. ✓
- "Config-gated test hook for both backends": Tasks 4+5; rpi2 screendump (truecolor) + Hatari STE (planar). ✓
- B4 (standalone .prg) deferred by user decision — the hook covers both backends. ✓

**Deviation from the design spec (flag for the PR):** the design (spec lines 58-59, 86-87) assumed setting `num_planes = 1` alone suffices; it did not account for `setup_info()` deriving `s_nxln` from `fd_wdwidth` (= `w/16` via `gsx_fix()`), which would make the opaque copy read `w/8` bytes/row. Task 2 Step 1 adds the `fd_w`-based stride override, gated `truecolor && !transparent && !fd_stand` so bb_save/bb_restore and the transparent mask path are untouched. Also: the AES cannot link `vdi_screen_is_truecolor()` (static inline in a `vdi/` header), so the plan adds the extern `vdi_truecolor_screen()` + `vdi_colour_blit_mode()` pair declared in `include/gsxdefs.h`. And the design's B3 hook assumed `appl_init()`/`appl_exit()` around the test draw; this plan deliberately drops both — `ap_exit()` would tear the desktop down, so the hook only calls `rs_loadmem()` + `objc_draw()` (noted in the Task 5 Step 3 comment).

**Placeholder scan:** every step carries exact code or exact expected output; the only open item is the bit-order calibration, which is an explicit iterative step (Task 6 Step 2) with a defined flip. The icon's background colour (generator) may need one iteration to match the desktop, also covered.

**Type consistency:** `pack_cicon`/`pack_planes` use `UWORD` pixels, `WORD` planes/dims, `LONG` for the `w*h` product (m68k-safe); `vdi_colour_blit_mode()` is `WORD`, matching `vro_cpyfm`'s first parameter and the AES's `S_OR_D`/`BM_*` types; `vdi_truecolor_screen()` is `BOOL`, matching `vdi_screen_is_truecolor()`; `rs_loadmem` returns `OBJECT *`, and the prototype in `gemrslib.h` matches the definition (OBJECT is already visible there via `ap_ptree`). `BM_S_ONLY`/`BM_S_OR_D` exist in `vdi/vdi_raster.h`; `S_OR_D`/`S_ONLY` in `include/obdefs.h` (numerically identical, both = 3/7).

**Linkability:** `vdi_colour_blit_mode()` is defined in `vdi/vdi_raster.c` (always built) so `gr_colourblit()` links in single-planar builds too (where `vdi_backend_planar.o` does not exist); `vdi_truecolor_screen()` is defined in `vdi/vdi_backend_truecolor.c`, which exists exactly when the A1 `#if` guard and its call compile in. `rs_loadmem` is in `aes/gemrslib.c` (always built), and its desk caller is gated on the same Kconfig symbol that adds `cicontest_rsc.o` and the hook.
