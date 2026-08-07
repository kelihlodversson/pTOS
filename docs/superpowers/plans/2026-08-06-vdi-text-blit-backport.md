# VDI text-blit backport + backend dispatch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Backport upstream EmuTOS's portable-C text-blit helpers into `vdi/vdi_textblit.c` (removing the `#if !ARCH_ARM` forks) and route glyph output through `vdi_backend_ops`, so the Raspberry Pi's RGB565 truecolor mode renders text correctly (issue #86).

**Architecture:** Transplant the upstream `43adfabf` (Oct 2019) `vdi_textblit.c`/`vdi_textblit.h` into pTOS, which moves `outline`/`rotate`/`scale` to portable C and adds scratch-buffer management. Delete those routines from the m68k/coldfire `vdi_tblit.S`. Then add a `text_blit` slot to `vdi_backend_ops`: the planar backend keeps calling the (asm/C) `normal_blit`, while the packed-truecolor backend gets a port of upstream's `screen_blit16()`. The m68k `normal_blit` asm is kept unchanged; the ARM `normal_blit` gains the 1-plane (skew/thicken) path its `pre_blit` buffer copies need.

**Tech Stack:** GNU make + kconfiglib, GCC C90 (`-std=gnu90`) for `arm-none-eabi-` and `m68k-atari-mintelf-`, m68k/coldfire GNU as. Verification via QEMU (raspi1/raspi2/virt-arm/virt-m68k) and Hatari (STE).

**Reference files (read-only inputs, never edited):**
- `/tmp/opencode/vdi_textblit_2019.c` — upstream `vdi/vdi_textblit.c` at `43adfabf` (1084 lines). The target body for Task 1.
- `/tmp/opencode/vdi_textblit_2024.c` — upstream at `21df385a`. `screen_blit16()` (lines 974-1187) is the template for the truecolor backend's `text_blit` (Task 2).
- `/tmp/opencode/upstream_tblit_43adfabf.S` — upstream `vdi/vdi_tblit.S` at `43adfabf` (1394 lines). The target shape for pTOS's m68k `vdi_tblit.S` after the Task 1 asm deletion.

---

## Global Constraints

- C90 with GNU extensions (`-std=gnu90`): declarations at the top of a block; `/* */` comments.
- 4 spaces, never a hard tab, in `.c`, `.h`, `.S`. Run `make gitready` before committing.
- **`int` is 16 bits on m68k** (`-mshort`). Use `WORD`/`LONG`/`UBYTE`/`UWORD`/`ULONG` from `portab.h`; suffix constants that must survive on m68k.
- `-Wundef` is on: every `#if` symbol must be defined (use the `CONF_*`/`MACHINE_*`/`ARCH_*` naming rules from `CLAUDE.md`).
- Never edit `obj/autoconf.h` or `obj/auto.conf`; they are generated from `.config`.
- `LOCALVARS` field **offsets** are read by the m68k/coldfire `normal_blit` asm via literal `(a6)` offsets. The struct may be renamed/re-commented but the field order and types in Task 1 MUST match the current header exactly (verify with `git diff` on the struct block).
- Backend contract (`vdi_backend.h`): a NULL ops slot means "not implemented", never a fallback. Planar primitives are called directly only when `!CONF_WITH_VDI_TRUECOLOR` (see `draw_rect_common()` at `vdi/vdi_line.c:368`).
- `CONF_CHUNKY_PIXELS` is forced `y` on `MACHINE_RPI` and its forks in `vdi_fill.c`/`vdi_line.c`/`arch/arm/vdi_tblit.c` are separate issue-#35 work — do not touch them. The text path drops its `CONF_CHUNKY_PIXELS` fork only when the backend dispatch replaces it (Task 2).
- pTOS accesses line-A variables as `linea_vars.<name>` (the asm uses the `LA()` macro); `v_bas_ad` is a global `UBYTE *`. Upstream's bare `STYLE`, `SOURCEX`, `DESTX`, … names must be rewritten with the `linea_vars.` prefix.
- Upstream's `v_planes_shift` does not exist in pTOS; the equivalent is `shift_offset[linea_vars.v_planes]` (`bios/lineainit.c:26`, declared `bios/lineavars.h:46`).
- Built-in 8x16 font metrics (from `bios/fnt_gr_8x16.c:297-309`): `max_cell_width=8`, `left_offset=1`, `right_offset=7`, `form_height=16`, `form_width=256`.
- The `normal_blit` call contract is `normal_blit(vars+1, src, dst)` — the asm and the ARM C shim both compensate (ARM does `vars--` at entry). The portable C helpers `outline`/`rotate`/`scale` take the plain `LOCALVARS *`.
- No comments in code unless the surrounding file style uses them (it does — match the file).
- Verification before completion: build the affected configs and run the smoke tests listed in each task; report real output, not assumptions.

---

### Task 1: Core backport — portable-C text helpers + 2019 text-blit engine

The whole `vdi_textblit.*` pair becomes the upstream `43adfabf` implementation, adapted to pTOS. This is the "issue #35 Part 2a" deliverable.

**Files:**
- Modify: `vdi/vdi_defs.h` — add `WM_*`, `OUTLINE_THICKNESS`, and the scratch-buffer size macros
- Modify: `include/arch/m68k/asm.h`, `include/arch/arm/asm.h` — add `roll`/`rorl` (m68k with `__mcoldfire__` fallback)
- Modify: `vdi/vdi_textblit.h` — LOCALVARS layout (renames only), C-only helper prototypes
- Modify: `vdi/vdi_textblit.c` — full rewrite (see below)
- Modify: `vdi/arch/m68k/vdi_tblit.S`, `vdi/arch/coldfire/vdi_tblit.S` — delete the `_deftxbuf`/`_scrtsiz`/size-calc block AND the `_outline`/`_rotate`/`_scale` glue + `outlin`/`rotation`/`replicat` bodies so the C definitions/helpers can take over without duplicate symbols (see Step 5 items 8 and 9)
- Reference: `/tmp/opencode/vdi_textblit_2019.c`
- Test: `vdi/vdi_textblit.c` no longer contains `#if ARCH_ARM` or `#if !ARCH_ARM`

**Interfaces:**
- Consumes: existing `linea_vars` fields, `v_bas_ad`, `shift_offset[]`, `normal_blit()` (asm, unchanged), `SCRTCHP`/`SCRPT2`.
- Produces: `void outline(LOCALVARS *vars)`, `void rotate(LOCALVARS *vars)`, `void scale(LOCALVARS *vars)` (portable C, all arches); `WORD deftxbuf[]` + `const WORD scrtsiz` defined in C for all architectures (the asm definitions are deleted in this same task). `SCRATCHBUF_SIZE`/`SCRATCHBUF_OFFSET` macros consumed here and by Task 2.

- [ ] **Step 1: Add `WM_*`, `OUTLINE_THICKNESS`, and scratch-buffer size macros to `vdi/vdi_defs.h`**

`vdi/vdi_defs.h` already defines `MD_REPLACE 1` … `MD_ERASE 4` (lines 79-82). Append after the `F_*` style defines (around line 90):

```c
/* Write modes used by the line-A text engine (MD_* minus one). */
#define WM_REPLACE      (MD_REPLACE-1)
#define WM_TRANS        (MD_TRANS-1)
#define WM_XOR          (MD_XOR-1)
#define WM_ERASE        (MD_ERASE-1)

#define OUTLINE_THICKNESS   1   /* outline thickness in pixels (vdi_text.c uses it too) */

/*
 * Text scratch buffer sizing, calculated from the built-in 8x16 font
 * metrics exactly as the original assembler code did (see EmuTOS commits
 * 6d833b0b/e7fa27c8/7d157b8a and the (now deleted) size-calc comments in
 * vdi/arch/m68k/vdi_tblit.S).  SCRATCHBUF_OFFSET is the size of each of the
 * two half-buffers used for rotation/outlining/effects; the buffer must
 * hold two of them (rotation can use either half, outline needs both).
 * Keep the #error guard so a future larger font cannot silently overflow.
 */
#define FORM_HT         16          /* form height of the 8x16 font */
#define MX_CEL_WD       8           /* maximum cell width */
#define SKEW_OFFS       (1+7)       /* left_offset + right_offset of the 8x16 font */

#define CEL2_WW     ((((2*(SKEW_OFFS+MX_CEL_WD))+3+15)/16)*2)
#define CEL2_WH     ((2*(SKEW_OFFS+MX_CEL_WD))+2)
#define CEL2_HH     ((2*FORM_HT)+2)
#define CEL2_HW     ((((2*FORM_HT)+3+15)/16)*2)
#define CEL2_SZ0    (CEL2_WW*CEL2_HH)
#define CEL2_SZ9    (CEL2_WH*CEL2_HW)
#if CEL2_SZ0 >= CEL2_SZ9
# define CEL2_SIZ   (CEL2_SZ0)
#else
# define CEL2_SIZ   (CEL2_SZ9)
#endif
#if CEL2_WW >= CEL2_HW
# define OUT_ADD    (CEL2_WW+2)
#else
# define OUT_ADD    (CEL2_HW+2)
#endif
#define SCRATCHBUF_OFFSET   (CEL2_SIZ+OUT_ADD)
#define SCRATCHBUF_SIZE     (2*212)         /* 424 bytes */
#if SCRATCHBUF_SIZE < (2*SCRATCHBUF_OFFSET)
# error SCRATCHBUF_SIZE is too small for the built-in 8x16 font
#endif
```

Sanity check (do it with `echo`/a scratch file, record the value): `SCRATCHBUF_OFFSET` must evaluate to `212` (this is upstream's "212 bytes for the large buffer" — see the comment at the top of upstream `vdi_text.c`; the old m68k asm split it differently, `scrtsiz=cel_siz=64` + `deftxbuf=buf_siz=276` bytes, but that whole scheme is replaced by the upstream one in this backport).

- [ ] **Step 2: Add `roll`/`rorl` to both arch `asm.h` files**

`outline()` (backported in Step 5) needs 32-bit rotates with count 1 and 2. Upstream defines them in `include/asm.h` (m68k asm; coldfire/portable C fallback). pTOS splits `asm.h` per arch.

`include/arch/m68k/asm.h` — add after the `rorw1` block (line 152). ColdFire has **no ROL/ROR instructions**, so the portable fallback must be guarded with `#ifdef __mcoldfire__` exactly like `rolw1`/`rorw1` above:

```c
/*
 * roll(ULONG x, WORD count);
 *  rotates x leftwards by count bits
 */
#ifndef __mcoldfire__
#define roll(x,n)                   \
    __asm__ volatile                \
    ("rol.l %2,%1"                  \
    : "=d"(x)       /* outputs */   \
    : "0"(x),"I"(n) /* inputs */    \
    : "cc"          /* clobbered */ \
    )
#else
#define roll(x,n)   ((x)=(((x)>>(32-(n)))|((x)<<(n))))
#endif

/*
 * rorl(ULONG x, WORD count);
 *  rotates x rightwards by count bits
 */
#ifndef __mcoldfire__
#define rorl(x,n)                   \
    __asm__ volatile                \
    ("ror.l %2,%1"                  \
    : "=d"(x)       /* outputs */   \
    : "0"(x),"I"(n) /* inputs */    \
    : "cc"          /* clobbered */ \
    )
#else
#define rorl(x,n)   ((x)=(((x)<<(32-(n)))|((x)>>(n))))
#endif
```

`include/arch/arm/asm.h` — add after the `rorw1` define (line 62):

```c
/* 32-bit rotates; the outline() text helper needs count 1 and 2 */
#define roll(x,n)   ((x)=(((x)>>(32-(n)))|((x)<<(n))))
#define rorl(x,n)   ((x)=(((x)<<(32-(n)))|((x)>>(n))))
```

Note: with `-Wundef` these are plain macros, no issue. `n` is a constant at every call site.

- [ ] **Step 3: Update `vdi/vdi_textblit.h` to the 2019 LOCALVARS layout**

Take the struct from `/tmp/opencode/vdi_textblit_2019.c` lines 34-98 verbatim (it renames pTOS's `chup_flag`→`unused5`, `YMX_CLIP/XMX_CLIP/YMN_CLIP/XMN_CLIP/CLIP`→`unused6..10`, `CHUP`→`unused11`, `buffb`→`unused4`, and changes `void *dform/sform` to `UBYTE *`). **Do not reorder fields and do not change types** — offsets must stay identical to today, because `vdi/arch/m68k/vdi_tblit.S` and `vdi/arch/coldfire/vdi_tblit.S` read them by literal offset (still true after Task 1's asm deletion, which keeps `normal_blit`).

Then replace the prototype block (current lines 90-100) with the upstream set, without the `MACHINE_RPI` guard:

```c
/* assembler functions in vdi_tblit.S (m68k/coldfire) */
void normal_blit(LOCALVARS *vars, UBYTE *src, UBYTE *dst);

/* portable C implementations, provided on every target */
void outline(LOCALVARS *vars);
void rotate(LOCALVARS *vars);   /* actually local, but non-static improves performance */
void scale(LOCALVARS *vars);
```

Verify with `git diff --word-diff` that only field NAMES and the `void *`→`UBYTE *` change appear in the struct — no reordering, no size change.

- [ ] **Step 4: Add the `reverse_nybble` table and `merge_byte()`**

These belong in `vdi/vdi_textblit.c` (Step 5 copies the whole file, so just confirm they are present there). No separate action.

**Endianness verification (done, T1S4/T1S5 will apply the fix):** upstream's `merge_byte()` reinterprets `UWORD *` memory as a `ULONG` and is therefore big-endian-only — running it on little-endian ARM produces a corrupted outline. Verified empirically: the unmodified algorithm yields a correct outline on m68k (BE, QEMU virt-m68k) but garbage on x86 (LE). The `bottom_left = (*(ULONG *)nextline) >> 1;` line in `outline()` has the same problem (and is also an unaligned long load that faults on ARM). The endian-neutral replacement, verified to produce byte-identical output on BE and LE (matching upstream's BE behavior exactly), is:

```c
static ULONG merge_byte(UWORD *p, UWORD n)
{
    /* this is the 2019 EmuTOS version, but written to be endian-neutral:
     * on big-endian it is exactly equivalent to the original
     * `*(ULONG *)p` union trick, and it works on little-endian ARM too */
    return ((ULONG)p[0] << 16) | ((ULONG)(p[1] >> 8) << 8) | (n & 0xFF);
}
```

and in `outline()`:

```c
    /* endian-neutral (and alignment-safe) replacement for
     * `bottom_left = (*(ULONG *)nextline) >> 1;` */
    bottom_left = (((ULONG)nextline[0] << 16) | (ULONG)nextline[1]) >> 1;
```

Both changes are `#if`-free and apply on every architecture; m68k behavior is unchanged (identical output).

- [ ] **Step 5: Rewrite `vdi/vdi_textblit.c`**

Start from `/tmp/opencode/vdi_textblit_2019.c` (all 1084 lines) and apply these pTOS adaptations, then overwrite `vdi/vdi_textblit.c`:

1. Header block: keep pTOS's copyright header; change the comment `2017-2019` as the file itself dictates. Include list becomes:
   ```c
   #include "config.h"
   #include "portab.h"
   #include "intmath.h"
   #include "asm.h"

   #include "../bios/tosvars.h"
   #include "vdi_defs.h"
   #include "vdi_textblit.h"
   #include "../bios/lineavars.h"
   #include "kprint.h"
   ```
   (Drop upstream's `emutos.h`/`vdistub.h`/`biosext.h`; keep `asm.h` for `roll`/`rorl`/`rolw1`/`rorw1`.)
2. Delete the `#if CONF_WITH_VDI_TEXT_SPEEDUP` block (`direct_screen_blit`, lines 745-843) and the speedup branch inside `screen_blit` — pTOS has no `CONF_WITH_VDI_TEXT_SPEEDUP`.
3. Rewrite every bare line-A access with the `linea_vars.` prefix. Full checklist of identifiers (each occurs one or more times): `STYLE`, `WRT_MODE`, `DELX`, `DELY`, `DESTX`, `DESTY`, `CHUP`, `SOURCEX`, `SOURCEY`, `FBASE`, `FWIDTH`, `WEIGHT`, `MONO`, `LOFF`, `ROFF`, `SKEWMASK`, `LITEMASK`, `SCALDIR`, `DDAINC`, `XDDA`, `SCALE`, `CLIP`, `XMINCL`, `YMINCL`, `XMAXCL`, `YMAXCL`, `TEXTFG`, `SCRTCHP`, `SCRPT2`, `v_planes`, `v_lin_wr`. (`v_bas_ad` stays bare — it is a global in `tosvars.h`.) Do not prefix `SOURCEX +=` style compound assignments differently — `linea_vars.SOURCEX += n;` is fine.
4. In `screen_blit()`, keep pTOS's `#if CONF_CHUNKY_PIXELS` dest-address fork (current lines 314-326) instead of upstream's planar-only version. `vars->nextwrd` keeps its fork too. **Do not** adopt `v_planes_shift`. The `normal_blit(vars+1, vars->sform, vars->dform)` call stays.
5. Replace upstream's `check_clip()`/`do_clip()` direct global reads as per item 3. The `!CLIP` early-return in `do_clip()` (upstream behavior, commit 04376451 — no automatic clip-to-screen) is intentional and must stay.
6. `pre_blit()`: `outline(vars)` call replaces the removed asm call; the `normal_blit(vars+1, src, dst)` call stays.
7. `text_blt()`: `rotate(&vars)` / `scale(&vars)` replace the removed asm calls; `vars.buffa = 0;` at the top as in upstream. The `switch(CHUP)` uses `linea_vars.CHUP` and upstream's `FALLTHROUGH;` (already defined in `include/portab.h`). **Adopt upstream's call ordering `scale(&vars)` → `pre_blit(&vars)` → `rotate(&vars)`** (upstream commit 6260508a "Improve text output quality"): pTOS's current file runs `pre_blit` → `rotate` → `scale`, which was the pre-6260508a order. The 2019 target scales first, exactly as TOS 2/3/4 do, and consequently `pre_blit()` drops the old `#if !ARCH_ARM` `if (!SCALE)` outline guards and the `max(weight/2,1)` skew/thicken halving — those existed only to compensate for scaling-*after*-effects, which this backport ends. This changes m68k behavior only for scaled+styled text, which pTOS never rendered correctly (the RPi port was explicitly "unscaled and unrotated only"). Plain text is untouched.
8. Delete the old `#if ARCH_ARM` stub (`const WORD scrtsiz = 99; WORD deftxbuf[1000];`). In its place, define the buffer in C **unconditionally** (all architectures) at the top of the file, outside any `#if`:

   ```c
   /*
    * the text scratch buffer, used by the rotation/outline/scale helpers
    * (see the SCRATCHBUF_* sizing macros in vdi_defs.h)
    */
   const WORD scrtsiz = SCRATCHBUF_OFFSET;
   WORD deftxbuf[SCRATCHBUF_SIZE/sizeof(WORD)];
   ```

   **Do this in the same commit as the m68k/coldfire asm buffer deletion (Task 1 Step 5 must include deleting `_deftxbuf`/`_scrtsiz` + the size-calc block from both `vdi_tblit.S` files).** Rationale: the m68k/coldfire asm *also* defines `_deftxbuf`/`_scrtsiz`; if both the C file and the asm define them, the m68k link fails with duplicate symbols. The asm buffer scheme (`scrtsiz=cel_siz=64`, `deftxbuf=buf_siz=276` bytes) is entirely replaced by the upstream one (`scrtsiz=212`, `deftxbuf=424` bytes); nothing in the (soon-deleted, see item 9) asm outline/rotate/scale bodies references `_deftxbuf`/`_scrtsiz` by symbol, so removing the asm data definitions does not break them. This keeps every commit green on all architectures.

9. **Delete the asm `_outline`/`_rotate`/`_scale` glue and the `outlin`/`rotation`/`replicat` bodies from BOTH `vdi_tblit.S` files (m68k and coldfire), in this same Task 1 commit.** This is mandatory, not optional: the C file now defines `outline`/`rotate`/`scale` unconditionally, and on m68k those C symbols are `_outline`/`_rotate`/`_scale` — the exact symbols the asm glue still defines. Leaving them in produces a duplicate-symbol link error on every m68k/coldfire build. (This is precisely how upstream did it: commits b657563f / 6752e168 / 80d92353 / c202b59a rewrote each function in C and deleted its asm counterpart in the *same* commit; upstream never had a state where the C helpers and the asm bodies coexisted.)

   Removal checklist (from the former Task 2, now merged here), m68k first, then coldfire (locate by symbol, not line number):

   1. The `_outline`/`_rotate`/`_scale` `.globl` lines (m68k around lines 116-123).
   2. The `_outline`/`_rotate`/`_scale` glue routines (m68k lines 408-434): they pass `&vars+1` as `a6` — that ABI is gone once the C helpers take plain `LOCALVARS *`.
   3. The `outlin`/`rotation`/`replicat` bodies and any helper subroutines referenced ONLY by them (per the diff, m68k lines ~1581-2102). Verify exclusivity: `grep` each symbol in the deleted tail against `norm_blt`'s code first; anything shared must stay.
   4. The `#define OUTLINE 4` once its only users (the `outlin` bodies) are gone. The `buffb`/`chup_flag`/`CLIP`/`X*CLIP`/`CHUP` `#define`s (m68k lines 297-389) become unused by `norm_blt` but were referenced by the deleted code paths — if they are all inside the deleted region, remove them too; otherwise leave them (harmless) rather than risk an offset mismatch. Keep `buffa`, `tddad`, `tsdad`, `sform`, `dform`, etc. that `norm_blt` uses.

   Do **not** touch: `norm_blt`, `do_rot`, `get_mask`, `plane_loop`, `wrmappin`, `toptable`, `twoptble`, `skewop*`, `thknop*`, `liteop*`, `fshft`, `do_sh`, `mlt_left`, `mlt_rite`, `do_ritem`, `msk_done`. `normal_blit(vars+1, src, dst)` must remain callable from C on m68k/coldfire.

- [ ] **Step 6: Build all four configs**

```sh
make rpi1_defconfig && make            # ARM, chunky, 8bpp->16bpp path
make virt-arm_defconfig && make
make virt-m68k_defconfig && make       # m68k, mintelf toolchain (already pinned)
```

For the classic Atari target (uses the m68k asm `normal_blit`; also builds coldfire configs `firebee_defconfig`/`m548x-bas_defconfig` if you want asm coverage):

```sh
make atari512_defconfig
# non-interactive switch to the installed mintelf toolchain:
sed -i 's/^CONFIG_BUILD_TOOLCHAIN_MINT=y/CONFIG_BUILD_TOOLCHAIN_MINTELF=y/' .config
make olddefconfig && make
```

Expected: all four build (the C buffer definitions are unconditional, so no arch needs the old stub or the asm buffer). Fix any `-Wundef`/C90 warnings. `git diff --check` clean.

   **Execution notes (2026-08-07):** all four built clean. The atari512 "toolchain revert / malformed .config line" symptoms from earlier were an artifact of running `tools/genconfig.py` from a bare shell: kconfig.mk exports `CONFIG_=` (empty), which kconfiglib reads as its `config_prefix`; without it, kconfiglib defaults to the `CONFIG_` prefix and rejects every unprefixed `.config` line as malformed. Run genconfig as `CONFIG_= python3 tools/genconfig.py ...` (or via `make`, which exports it). `make olddefconfig` is still broken with kconfiglib 14.1.0 (`--kconfig` vs positional arg) but is not needed: the atari512 `.config` keeps `BUILD_TOOLCHAIN_MINTELF=y` across `make atari512_defconfig` + direct genconfig runs. Also note the atari512 build hits a pre-existing `make -j` RSC race (issue #53); pre-generate `desk/desk_rsc.c desk/desk_rsc.h aes/gem_rsc.* desk/icons.* aes/mforms.*` serially, or build on current master where #54's grouped targets fix it.

- [ ] **Step 7: Smoke test the m68k target (behavior unchanged)**

```sh
make virt-m68k_defconfig && make
timeout 5 qemu-system-m68k -M virt -m 128 -cpu m68020 -kernel virt-m68k.elf \
  -d guest_errors,unimp -D /tmp/qemu.log -display none -serial stdio
cat /tmp/qemu.log
```

Expected: rc=124 (survived the window), at most one benign `Illegal Instruction` entry (the `_detect_fpu` probe), the boot prints `VDI video mode = ...`, `AES: EMUDESK: appl_init()`, `AES: EMUDESK: evnt_multi()`.

Optionally (Hatari, full desktop + the asm path, ~95 s wall):
```sh
hatari --tos ptos512k.img --machine ste --memsize 4 --sound off \
  --avirecord --avi-vcodec png --avi-file /tmp/boot.avi --run-vbls 1200
```
and run the desktop-detection Python from the ptos-smoketest skill (`green > 1000` ⇒ desktop). The desktop uses plain text, so this verifies the common path end-to-end; styled-text parity is verified by diff-review against `/tmp/opencode/vdi_textblit_2019.c`.

- [x] **Step 8: Self-review the transplant**

`diff -u /tmp/opencode/vdi_textblit_2019.c vdi/vdi_textblit.c` and walk every hunk: each must be one of (a) the `linea_vars.` prefix, (b) include-list change, (c) `CONF_CHUNKY_PIXELS` fork, (d) speedup block removal, (e) the `deftxbuf`/`scrtsiz` definitions, or (f) a pTOS-comment tweak. Any hunk that changes logic needs justification.

   Done: after normalizing `linea_vars.` and the struct/include/comment noise, the only remaining deltas are (c) the two `CONF_CHUNKY_PIXELS` blocks, (d) the `direct_screen_blit` speedup block (absent in pTOS), (e) `scrtsiz`/`deftxbuf`, (f) comment/`/* */`-style tweaks, plus the three approved logic changes: endian-neutral `merge_byte`/`bottom_left` (T1S4), `check_clip`/`do_clip` reading `linea_vars` globals directly, and `v_planes_shift` → `shift_offset[v_planes]` (matches `vdi/vdi_misc.c:213`).

- [ ] **Step 9: Commit**

```bash
git add vdi/vdi_defs.h include/arch/m68k/asm.h include/arch/arm/asm.h \
        vdi/vdi_textblit.h vdi/vdi_textblit.c \
        vdi/arch/m68k/vdi_tblit.S vdi/arch/coldfire/vdi_tblit.S
git commit -m "vdi: port text outline/rotate/scale to C, backport 2019 text_blt"
```

---

### ~~Task 2~~ — merged into Task 1 (Step 5 item 9)

The former "drop outline/rotate/scale from the m68k/coldfire assembler" task is folded into Task 1 Step 5 item 9: the asm glue + bodies must be deleted **in the same commit** as the C transplant, because the C helpers define the exact symbols (`_outline`/`_rotate`/`_scale` on m68k) that the asm still exports — leaving them in fails the m68k link with duplicate symbols. The m68k/coldfire `vdi_tblit.S` files keep only `_normal_blit` and its `norm_blt` machinery, and `normal_blit(vars+1, src, dst)` remains callable from C. The final `git commit` for Task 1 therefore covers both files together with `vdi_textblit.c` (see Task 1 Step 9).

---

### Task 2: Route glyph output through `vdi_backend_ops`

Add a `text_blit` slot to the backend ops, implement it for planar and packed-truecolor, and dispatch from `screen_blit()`/`text_blt()`.

**Files:**
- Modify: `vdi/vdi_backend.h` (ops slot + `vdi_screen_is_truecolor()`)
- Modify: `vdi/vdi_backend.c` (implement `vdi_screen_is_truecolor()`)
- Modify: `vdi/vdi_backend_planar.c` (wire `planar_text_blit`)
- Modify: `vdi/vdi_backend_truecolor.c` (implement `truecolor_text_blit`, port of upstream `screen_blit16`)
- Modify: `vdi/vdi_textblit.c` (`screen_blit()` + `text_blt()` dispatch)
- Reference: `/tmp/opencode/vdi_textblit_2024.c` lines 974-1187 (`screen_blit16`), 1198-1223 (`screen_blit` dispatch), 1375-1387 (`need_preblit`)

**Interfaces:**
- Consumes: `LOCALVARS` (from Task 1), `v_bas_ad`/`linea_vars.v_lin_wr`, `truecolor_pixel_for_index()` (static helper in the truecolor backend file), `normal_blit()`.
- Produces: `const vdi_backend_ops` grows a `void (*text_blit)(LOCALVARS *vars)` member; `BOOL vdi_screen_is_truecolor(void)`; `void planar_text_blit(LOCALVARS *vars)` (defined in `vdi_textblit.c`, exported for the planar ops table).

- [x] **Step 1: Extend the ops struct and add the helper**

`vdi/vdi_backend.h`:

```c
#include "vdi_textblit.h"   /* LOCALVARS */
```
(in the include block at the top, alongside `screen_mode.h`), then inside the struct:

```c
    UWORD *(*get_start_addr)(WORD x, WORD y);
    UWORD (*get_pixel)(WORD x, WORD y);
    void (*put_pixel)(WORD x, WORD y, UWORD color);
    void (*fill_rect)(const VwkAttrib *attr, const Rect *rect);
    void (*text_blit)(LOCALVARS *vars);
} vdi_backend_ops;
```

Update the comment above the struct to mention the text-blit slot. Add the prototype near `vdi_screen_backend()`:

```c
/*
 * Is the current screen workstation driven by the packed-truecolor
 * backend?  Used by text_blt() to decide whether styled text must go
 * through pre_blit() (the truecolor path cannot apply skew/thicken at
 * blit time the way the planar assembler does).
 */
BOOL vdi_screen_is_truecolor(void);
```

`vdi/vdi_backend.c`: implement it as
```c
BOOL vdi_screen_is_truecolor(void)
{
    return vdi_screen_backend() == &packed_truecolor_backend_ops;
}
```

- [x] **Step 2: `screen_blit()` dispatches to the backend**

In `vdi/vdi_textblit.c`, replace the dest-address setup + `normal_blit(...)` tail of `screen_blit()` (currently the `#if CONF_CHUNKY_PIXELS`/`#else` block and the call) with the `draw_rect_common()` dispatch pattern:

```c
    vars->forecol = linea_vars.TEXTFG;
    vars->ambient = 0;          /* logically TEXTBG, but that isn't set up by the VDI */
    vars->nbrplane = linea_vars.v_planes;
    vars->nextwrd = vars->nbrplane * sizeof(WORD);
    vars->height = vars->DELY;
    vars->width = vars->DELX;

    /*
     * calculate the starting address for the character to be copied
     */
    vars->tsdad = linea_vars.SOURCEX & 0x000f; /* source dot address */
    offset = (linea_vars.SOURCEY+vars->DELY-1) * (LONG)vars->s_next + ((linea_vars.SOURCEX >> 3) & ~1);
    vars->sform += offset;
    vars->s_next = -vars->s_next;   /* we draw from the bottom up */

#ifdef CONF_WITH_VDI_TRUECOLOR
    {
        const vdi_backend_ops *backend = vdi_screen_backend();

        /* see the comment in get_start_addr() (vdi_misc.c) */
        if (backend)
            backend->text_blit(vars);
    }
#else
    planar_text_blit(vars);
#endif
```

The `#if CONF_CHUNKY_PIXELS` fork around `nextwrd`/dest-address is deleted — the backends own destination addressing now. Add `#include "vdi_backend.h"` to the file.

- [x] **Step 3: Add `planar_text_blit()`**

In `vdi/vdi_textblit.c` (following the `planar_fill_rect` pattern in `vdi/vdi_line.c:382` — shared implementation, called directly when truecolor is off and via the ops table when it is on). This is exactly the old planar dest-address code plus the `normal_blit` call, with the `CONF_CHUNKY_PIXELS` fork retained for safety (it can never run on RPi, where the truecolor backend is selected, but must remain correct if it does):

```c
/*
 * planar text blit: output the current glyph to a bitplane screen
 *
 * this is the historical path; the packed-truecolor backend has its own
 * version (see vdi_backend_truecolor.c)
 */
void planar_text_blit(LOCALVARS *vars)
{
#if CONF_CHUNKY_PIXELS
    vars->tddad = 0;
    vars->dform = v_bas_ad;
    vars->dform += (vars->DESTX * linea_vars.v_planes) >> 3;
    vars->dform += (UWORD)(vars->DESTY+vars->DELY-1) * (ULONG)linea_vars.v_lin_wr;
    vars->d_next = -linea_vars.v_lin_wr;
#else
    vars->tddad = vars->DESTX & 0x000f;
    vars->dform = v_bas_ad;
    vars->dform += (vars->DESTX & 0xfff0) >> shift_offset[linea_vars.v_planes];
    vars->dform += (UWORD)(vars->DESTY+vars->DELY-1) * (ULONG)linea_vars.v_lin_wr;
    vars->d_next = -linea_vars.v_lin_wr;
#endif

    normal_blit(vars+1, vars->sform, vars->dform);  /* call assembler helper function */
}
```

Declare it in `vdi/vdi_textblit.h` next to `normal_blit`:
```c
void planar_text_blit(LOCALVARS *vars);
```

- [x] **Step 4: Wire the ops tables**

`vdi/vdi_backend_planar.c`:
```c
#include "vdi_textblit.h"
...
const vdi_backend_ops planar_backend_ops = {
    planar_open,
    planar_close,
    planar_get_start_addr,
    planar_get_pixel,
    planar_put_pixel,
    planar_fill_rect,
    planar_text_blit,
};
```

`vdi/vdi_backend_truecolor.c`:
```c
#include "vdi_textblit.h"
...
const vdi_backend_ops packed_truecolor_backend_ops = {
    truecolor_open,
    truecolor_close,
    truecolor_get_start_addr,
    truecolor_get_pixel,
    truecolor_put_pixel,
    truecolor_fill_rect,
    truecolor_text_blit,
};
```

- [x] **Step 5: Implement `truecolor_text_blit()`**

Port upstream `screen_blit16` (`/tmp/opencode/vdi_textblit_2024.c` lines 974-1186) into `vdi/vdi_backend_truecolor.c`. Changes from the upstream text:

1. Header/context: function is `static void truecolor_text_blit(LOCALVARS *vars)`.
2. Colours: replace
   ```c
   palette = CUR_WORK->ext->palette;
   fgcol = palette[vars->forecol];
   bgcol = palette[0];
   ```
   with the backend's conversion (consistent with `truecolor_fill_rect`):
   ```c
   const UWORD fgcol = truecolor_pixel_for_index(vars->forecol);
   const UWORD bgcol = truecolor_pixel_for_index(0);
   ```
   (`vars->forecol` was set to `linea_vars.TEXTFG`, which is a hardware palette index on RPi — see the comment on `default_prgb_palette[]`.)
3. `v_lin_wr` → `linea_vars.v_lin_wr`. Keep `v_bas_ad` bare.
4. `muls(y, v_lin_wr)` → `(ULONG)y * linea_vars.v_lin_wr` (muls is not available in pTOS C; the long cast keeps the multiply 32-bit on m68k too).
5. The `if (skew && (vars->STYLE&F_OUTLINE))` adjustment block, the source/dest setup, the four `WRT_MODE` cases (`default`/WM_REPLACE, WM_TRANS, WM_XOR, WM_ERASE) and the per-row skew shift (`rolw1`/`rorw1` on `skew_mask`/`src_mask`) are copied verbatim, using `linea_vars.LOFF`/`linea_vars.ROFF` for `skew`.
6. `KDEBUG(("SOURCEX ..."))` calls: use the pTOS `KDEBUG` from `kprint.h` (already included); keep the messages.
7. `WORD fgcol, bgcol` are `UWORD` after the palette change; declare all locals at the top of the block (C90).

- [x] **Step 6: `text_blt()` — force `pre_blit` for styled text on truecolor**

In `vdi/vdi_textblit.c`, replace the current `if (vars.STYLE & (F_SKEW|F_THICKEN|F_OUTLINE))` nest (Task 1's upstream text) with the 2024 `need_preblit` structure (from `/tmp/opencode/vdi_textblit_2024.c` lines 1372-1387), with the truecolor condition mapped to pTOS:

```c
    /*
     * decide whether to copy the glyph into an intermediate buffer:
     * required when rotating, or when effects cannot be applied at
     * blit time (the truecolor backend has no bitplane engine), or
     * when clipping demands it
     */
    BOOL need_preblit = FALSE;

#ifdef CONF_WITH_VDI_TRUECOLOR
    if (vdi_screen_is_truecolor() && (vars.STYLE & (F_SKEW|F_THICKEN|F_OUTLINE)))
        need_preblit = TRUE;
#endif
    if (linea_vars.CHUP)
        need_preblit = TRUE;
    if ((vars.STYLE & F_SKEW) && clipped)
        need_preblit = TRUE;
    if (vars.STYLE & F_OUTLINE)
        need_preblit = TRUE;

    if (need_preblit)
        pre_blit(&vars);
```

(`BOOL`/`FALSE` come from `portab.h`.) Move the `rotate()`/`scale()`/`smear`/`do_clip()`/`screen_blit()` calls below it exactly as the 2019 file has them after the pre-blit decision.

- [x] **Step 7: Build all configs**

Task 1 Step 6 matrix again. Expected: builds clean. `git diff --check` clean.

- [x] **Step 8: Smoke test — RPi now renders text**

```sh
make rpi1_defconfig && make
timeout 30 qemu-system-arm -M raspi1ap -bios kernel.img \
  -d guest_errors -serial stdio -display none -monitor stdio >/tmp/rpi.log 2>&1 <<'EOF'
EOF
```

Better: run with `-monitor stdio` interactively (or pipe a `sleep` + `screendump` via a here-string to the monitor). Procedure:
1. Boot raspi1 (serial log to a file, monitor on stdio).
2. After the desktop appears (serial shows the AES `evnt_multi()` idle), issue `screendump /tmp/rpi.ppm` on the monitor, then `quit`.
3. Inspect `/tmp/rpi.ppm` with Python/PIL: the menu bar and any window text pixels must not be all-background. Minimal check: count pixels differing from the boot-checkerboard background in the top 20 scan lines (menu-bar text region); `> 0` distinct non-background colors in the menu strip is the pass signal that glyphs are actually being written (previously the truecolor path wrote nothing, so this strip was uniform).

Also run the virt-m68k smoke test (Task 1 Step 7) to confirm the planar path still boots unchanged.

Execution notes (verified):
- `-serial stdio` cannot be combined with `-monitor stdio` on QEMU 10.1 — use `-serial file:/tmp/rpi_serial.log -display none -monitor stdio` and pipe `sleep 60; screendump; quit` into the monitor via stdin.
- rpi1 boots to `VDI video mode = 1280x720 16-bit`, `AES: EMUDESK: appl_init()`, `evnt_multi()` (desktop idle). Screendump analysis: top 18 scan lines are the white menu bar with black "Desk File View Options" glyph pixels (rows 3-15, e.g. 40 black pixels in row 5) — >0 distinct non-background colors in the strip, so glyphs are being written. Rows below show the white desktop background alternating with the green boot checkerboard at odd x — that is the desktop pattern region, not text, and is unrelated to the text path.
- `skew_msk`: upstream 2024 never initializes it in C; it is seeded by the m68k/coldfire `normal_blit` asm (default `0x8000`, or `LA(SKEWMASK)` when F_SKEW — vdi/arch/m68k/vdi_tblit.S lines 411/535) via the `normal_blit(vars+1,...)` call in `pre_blit()`, which always runs before the truecolor `text_blit` for styled text (`need_preblit` forces it). For plain text `skew == LOFF+ROFF == 0`, so `skew_mask` is never used. Task 3's ARM `normal_blit` must seed it too.
- virt-m68k planar smoke: boots to `evnt_multi()`, only the benign `Illegal instruction f35f @ 154e` (`_detect_fpu`) in qemu.log.
- Note: `make atari512_defconfig` resets `.config` toolchain choice to cross-mint (default) which is not installed; the working atari512 build used `.config` edited to `BUILD_TOOLCHAIN_MINTELF=y`. Not caused by this task.

- [ ] **Step 9: Commit**

```bash
git add vdi/vdi_backend.h vdi/vdi_backend.c vdi/vdi_backend_planar.c \
        vdi/vdi_backend_truecolor.c vdi/vdi_textblit.c vdi/vdi_textblit.h
git commit -m "vdi: dispatch text blit through the backend ops table"
```

---

### Task 3: ARM `normal_blit` — 1-plane buffer blit with skew/thicken

The ARM `normal_blit` (currently `vdi/arch/arm/vdi_tblit.c`) only handles the legacy 8bpp chunky case. `pre_blit()`'s intermediate-buffer copy is a 1-plane bitplane blit; on RPi it must be implemented so outlined/rotated/scaled/skewed/thickened text works.

**Files:**
- Modify: `vdi/arch/arm/vdi_tblit.c`
- Reference: the 1-plane semantics of `norm_blt` in `vdi/arch/m68k/vdi_tblit.S` (kept from Task 1)

**Interfaces:**
- Consumes: `LOCALVARS` fields set by `pre_blit()` (Task 1): `nbrplane=1`, `nextwrd=2`, `tddad` (0 or 1), `tsdad` (0-15), `forecol=1`, `ambient=0`, `WRT_MODE=0`, `width`, `height`, `s_next`, `d_next`, `STYLE` (mask of `F_SKEW`/`F_THICKEN`), `skew_msk` (= `linea_vars.SKEWMASK`).
- Produces: correct 1-plane output in the scratch buffer for `nbrplane==1 && nextwrd==2` calls; leaves the 8bpp chunky path (`CONF_CHUNKY_PIXELS && nbrplane==8`) untouched.

- [ ] **Step 1: Understand the semantics (read, don't code yet)**

Read the kept `norm_blt`/`skewop`/`thknop*` sections in `vdi/arch/m68k/vdi_tblit.S` and confirm the model used here:
- Source and destination are sequences of 16-bit big-endian words; bit 15 = leftmost pixel.
- Row `y` source word pointer starts at `src` and each row advances `s_next` bytes; destination similarly with `d_next`. For the buffer copy `d_next`/`s_next` are negative (bottom-up).
- Pixel column `x` reads the bit at word offset `(x+tsdad)>>4`, bit `0x8000 >> ((x+tsdad)&15)`.
- Dest pixel writes per `WRT_MODE`: replace=set/clear to forecol/ambient; transparent=set if set; xor=flip if set; erase=set if clear.
- Thicken: each set source bit becomes `vars->smear` consecutive set pixels; dest width already includes the extra `smear` columns (`pre_blit` adds `weight` to `dest_width`).
- Skew: per row (bottom row first), rotate `skew_msk` left by 1; if the rotated-out bit was set, increase the running row shift; dest column `x` then reads source column `x - shift` (bits shifted out left are background). This reproduces the asm's `lsr.l #1` source packing.

- [ ] **Step 2: Implement the 1-plane path**

Extend `normal_blit()` in `vdi/arch/arm/vdi_tblit.c`. Keep the `vars--` adjustment. Structure:

```c
void normal_blit(LOCALVARS *vars, UBYTE *src, UBYTE *dst)
{
    int x, y;
    vars--;
    WORD src_bitoffset = vars->tsdad;
#if CONF_CHUNKY_PIXELS
    UBYTE tmp;
    if (vars->nbrplane == 8 && vars->nextwrd == sizeof(WORD))
    {
        ...existing 8bpp chunky body unchanged...
    }
    else
#endif
    if (vars->nbrplane == 1 && vars->nextwrd == 2)
    {
        UWORD skew_msk, word, mask, dword, dmask;
        WORD height = vars->height, width = vars->width;
        WORD shift = 0;
        WORD wmode = vars->WRT_MODE;
        WORD fg = vars->forecol, bg = vars->ambient;

        skew_msk = (UWORD)vars->skew_msk;

        for (y = 0; y < height; y++)
        {
            UBYTE *srow = src + y * vars->s_next;
            UBYTE *drow = dst + y * vars->d_next;

            if (vars->STYLE & F_SKEW)
            {
                rolw1(skew_msk);
                if (skew_msk & 0x8000)
                    shift++;
            }

            for (x = 0; x < width; x++)
            {
                int sx = x + vars->tsdad - shift;
                BOOL setbit = FALSE;
                WORD smear = (vars->STYLE & F_THICKEN) ? vars->smear : 1;
                int k;

                if (sx >= 0)
                {
                    word = *(UWORD *)(srow + ((sx >> 4) << 1));
                    mask = 0x8000 >> (sx & 15);
                    setbit = (word & mask) != 0;
                }

                dword = *(UWORD *)(drow + ((x + vars->tddad) >> 4) * 2);
                dmask = 0x8000 >> ((x + vars->tddad) & 15);
                if (setbit)
                {
                    for (k = 0; k < smear; k++)
                    {
                        if (x + k < width)
                            drow[((x+k + vars->tddad) >> 4) * 2] ...
                    }
                }
                ...
            }
        }
    }
}
```

**Do not hand-write the loop in the plan/commit blindly** — the thicken expansion and the `sx < 0` fringe are the two places most likely to diverge from the asm. Write the loop as a clean "for each source bit, write `smear` dest pixels" formulation and validate in Step 4. The key invariants to hold:
- dest pixels outside `[tddad, tddad+width)` are never touched (the buffer is pre-zeroed when `F_OUTLINE|F_SKEW`, and `pre_blit` sizes the row to `dest_width`).
- Thicken writes `smear` pixels per source pixel and the row is `width` pixels wide, so the last `smear-1` source columns may fall off the right edge (compare: `pre_blit` grows `dest_width` by `weight`).
- `WRT_MODE` is applied per dest pixel to the *written* bit only (word read-modify-write via `dword`/`dmask`).

- [ ] **Step 3: Build**

`make rpi1_defconfig && make` (and `make virt-arm_defconfig && make`). Clean build, `git diff --check` clean.

- [ ] **Step 4: Validate against the m68k reference**

Parity check for the styled-text path: on m68k the same `pre_blit` parameters flow through the asm `norm_blt`; on ARM through the new C. Since the two cannot run side by side easily, verify by reasoning + desktop output:
1. Boot rpi1 per Task 2 Step 8; screenshot. The desktop, menus, and any outlined/rotated/scaled text drawn by the AES (menu bar uses plain text; draw styled text only if an app does) must look sane — at minimum plain text is unaffected because it never enters `normal_blit` (screen_blit16 blits the font directly).
2. If styled text cannot be exercised from the desktop, draw it from the line-A emulator with a tiny test: set `STYLE=F_OUTLINE`, `DESTX/Y`, `DELX=8/DELY=16`, `SOURCEX=0`, `FBASE=font`, call `v_put_text`/`text_blt` and screendump. Keep this as an optional, time-boxed check (30 min); do not let it block the task.

- [ ] **Step 5: Commit**

```bash
git add vdi/arch/arm/vdi_tblit.c
git commit -m "vdi: implement 1-plane text buffer blit for ARM normal_blit"
```

---

### Task 4: Final verification + PR ready

**Files:** none (verification only).

- [ ] **Step 1: Full build + smoke matrix**

```sh
make rpi1_defconfig && make
make virt-arm_defconfig && make
make virt-m68k_defconfig && make
make atari512_defconfig   # then the sed + olddefconfig toolchain switch, then make
```

Smoke: raspi1 (`-M raspi1ap`, screendump check for text), virt-arm and virt-m68k (`timeout 5` idle = pass), and the atari512 Hatari desktop AVI check if time permits.

- [ ] **Step 2: `make gitready` and `git diff --check`**

Run `make gitready`; fix anything it flags. Confirm `git status` shows only the intended files.

- [ ] **Step 3: Update the design doc's open-follow-ups**

Edit `docs/superpowers/specs/2026-08-03-vdi-backend-truecolor-design.md`: mark the text-blit backport (issue #35 Part 2a, this issue #86) as done; note the `CONF_CHUNKY_PIXELS` fork that remains in `vdi/arch/arm/vdi_tblit.c` is now unreachable dead code (it predates truecolor and can be removed when the option itself is retired in a later slice).

- [ ] **Step 4: Request review and mark the PR ready**

`git push`, then `gh pr ready 104`. Summarize for the reviewer: what each commit does, the reference files used, and the smoke evidence.

---

## Self-review

- **Spec coverage:** Issue #86's acceptance criteria map 1:1 — (1) ARM no longer compiles out outline/rotate/scale → Task 1; (2) text dispatches through `vdi_backend_ops` like the other primitives → Task 2; (3) RPi RGB565 renders text correctly → Task 2 (plain text) + Task 3 (styled). The listed upstream commits are all inside the two transplanted reference states (`43adfabf` body + `21df385a` `screen_blit16` + `need_preblit`).
- **Placeholders:** Step 2 of Task 3 is the only "sketch" — it is deliberately a design note, not pasted code, because the final loop must be derived from the asm semantics rather than transcribed; the invariants that matter are enumerated so the implementer can't guess. All other tasks give exact content or exact copy-from paths.
- **Dependency order:** Task 1 defines the macros/struct/helpers and deletes the asm duplicates; Task 2 (backend dispatch) relies on Task 1; Task 3 (ARM `normal_blit`) relies on Task 1's `pre_blit` fields; Task 4 verifies everything. Each task ends green.
- **Known deferred items:** coldfire build verification depends on an installed m68k toolchain that may not be present (flagged in Task 1 Step 6); exact visual parity of exotic skew/thicken glyphs is validated by code review rather than a pixel-level test harness (no test infra exists for this freestanding image).
- **External notes (2026-08-07):** PR #54 (issue #53, the `make -j` RSC race) merged to master and was merged into this branch — the `&:` grouped-target + atomic `draft.c` writes are confirmed working (each RSC recipe runs once under `-j`). The `snprintf` implicit-declaration warning that #54 introduced (it builds with `gcc -ansi -pedantic`, where glibc hides `snprintf` without a feature-test macro) is tracked in issue #125, not fixed here.
