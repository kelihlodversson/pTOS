# RSC Loader Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split the two mutually exclusive RSC loader families out of `aes/gemrslib.c` into `aes/rsload_legacy.c` and `aes/rsload_portable.c`, selected by `build.mk`, with no behaviour change.

**Architecture:** Three source files replace the one. `aes/gemrslib.c` keeps only the shared loader infrastructure and public API; the m68k in-place loader moves to `aes/rsload_legacy.c`; the disk parser + native materializer + `rs_loadmem()` move to `aes/rsload_portable.c`. A private header `aes/rsload.h` declares the cross-file boundary. A new invisible derived Kconfig symbol `CONF_WITH_PORTABLE_RSC_LOAD` selects the portable file; it is the logical inverse of `CONF_WITH_LEGACY_RSC_LOAD`, so exactly one loader links on every configuration.

**Tech Stack:** C90 (`-std=gnu90`), Kconfig/kconfiglib, GNU make Kbuild-style `build.mk`, m68k-atari-mintelf and arm-none-eabi cross toolchains.

## Global Constraints

- Pure refactor: no logic changes, no user-visible options.  The split
  inherently costs a small amount of code size: the `rsload.h` interface
  promotes `rs_readit()`/`fix_objects()` and the shared helpers from `static`
  to external, disabling GCC whole-function inlining across translation units
  (no LTO).  This drift was accepted by the plan owner; the post-Task-2
  measured sizes are the authoritative comparison baseline (see Progress
  Ledger).
- The derived `CONF_WITH_PORTABLE_RSC_LOAD` symbol is invisible: no prompt, no help text.
- The two loader files must contain **no** `#if CONF_WITH_LEGACY_RSC_LOAD` inside; they are selected by `build.mk`. `#if CONF_WITH_COLOUR_ICONS` and `#if CONF_WITH_VDI_BACKEND_TRUECOLOR` feature guards remain.
- Exactly one loader links on every configuration; no duplicate `rs_readit()`/`fix_objects()` symbols.
- Source basenames remain unique tree-wide (`gemrslib.c`, `rsload_legacy.c`, `rsload_portable.c`).
- C90: declarations at the top of a block; 4-space indent, no tabs; `/* */` comments. `make gitready` must pass.
- `-Wmissing-prototypes` is on: every non-static function defined in the new files must have its prototype declared in `aes/rsload.h` (or `gemrslib.h`) *before* the definition.
- No change to the public `gemrslib.h` interface other than the existing `rs_loadmem()` guard, which is unchanged.
- All line ranges below are relative to `aes/gemrslib.c` at current HEAD (`0a999b11`).

---

### Task 1: Add the derived symbol and record the baseline

**Files:**
- Modify: `aes/Kconfig:64-79` (append after `CONF_WITH_LEGACY_RSC_LOAD`)
- Verify: `obj/autoconf.h` across the matrix, plus recorded image sizes (baseline)

**Interfaces:**
- Produces: `CONF_WITH_PORTABLE_RSC_LOAD` — a bool, always emitted 0 or 1, `1` exactly when `CONF_WITH_LEGACY_RSC_LOAD` is `0`.

- [ ] **Step 1: Add the derived symbol to `aes/Kconfig`**

Append directly after the `CONF_WITH_LEGACY_RSC_LOAD` entry (line 79's closing `help` text), before `CONF_WITH_NICELINES`:

```kconfig
config CONF_WITH_PORTABLE_RSC_LOAD
	bool
	default y
	depends on !CONF_WITH_LEGACY_RSC_LOAD
```

- [ ] **Step 2: Build the baseline matrix and verify the symbol**

Build each configuration in order; every build must succeed. The expected free-bytes/RAM figures below come from the design doc; **record the actual values** — if one differs from the expected figure, the actual becomes the comparison baseline for Tasks 2-4 (a mismatch may indicate a toolchain change since the design doc; note it in the report, it is not a failure).

```sh
make atari192_defconfig && make          # expect: # ptos192us.img done (2068 bytes free)
make atari256_defconfig && make          # expect: # ptos256us.img done (7255 bytes free)
make atari512_defconfig && make          # expect: # ptos512k.img done (16461 bytes free)
```

For the atari512-with-CICON-TEST baseline, append the symbol to `.config` and rebuild:

```sh
make atari512_defconfig
printf 'CONF_WITH_VDI_CICON_TEST=y\n' >> .config
make                                     # record "# ptos512k.img done (N bytes free)"
```

```sh
make rpi1_defconfig && make              # record "# kernel.img is ready" + "# RAM used: N bytes"
```

For each of atari192, atari512, rpi1, confirm the derived symbol in the generated header:

```sh
grep -E "LEGACY_RSC_LOAD|PORTABLE_RSC_LOAD" obj/autoconf.h
```

Expected:

| config | CONF_WITH_LEGACY_RSC_LOAD | CONF_WITH_PORTABLE_RSC_LOAD |
| --- | --- | --- |
| atari192 | 1 | 0 |
| atari512 | 0 | 1 |
| rpi1 | 0 | 1 |

- [ ] **Step 3: Commit**

```bash
git add aes/Kconfig
git commit -m "kconfig: add derived portable RSC loader option"
```

---

### Task 2: Extract the legacy loader

**Files:**
- Create: `aes/rsload.h`
- Create: `aes/rsload_legacy.c`
- Modify: `aes/gemrslib.c`
- Modify: `aes/build.mk`

**Interfaces:**
- Consumes: `CONF_WITH_PORTABLE_RSC_LOAD` from Task 1 (only for the `build.mk` line; the legacy file itself carries no loader-guards).
- Produces: `aes/rsload.h` (below), `WORD rs_readit(AESGLOBAL *pglobal, UWORD fd)` and `void fix_objects(void)` defined in `rsload_legacy.c`, shared helpers `get_addr`/`get_sub`/`get_ciconblkptr`/`transform_all_cicons` and globals `rs_hdr`/`rs_global` promoted from `static` to external.

- [ ] **Step 1: Create `aes/rsload.h`**

The private header every one of the three files includes. Content, exactly:

```c
/*
 *  rsload.h - private interface between the shared RSC library code and
 *  the two alternative resource loaders (legacy in-place and portable
 *  canonical).  Exactly one of the loaders is linked, selected by build.mk.
 */

#ifndef RSLOAD_H
#define RSLOAD_H

#include "config.h"
#include "portab.h"
#include "rsdefs.h"
#include "gemrslib.h"

#define R_TREE      0
#define R_OBJECT    1
#define R_TEDINFO   2
#define R_ICONBLK   3
#define R_BITBLK    4
#define R_STRING    5               /* gets pointer to free strings */
#define R_IMAGEDATA 6               /* gets pointer to free images  */
#define R_OBSPEC    7
#define R_TEPTEXT   8               /* sub ptrs in TEDINFO  */
#define R_TEPTMPLT  9
#define R_TEPVALID  10
#define R_IBPMASK   11              /* sub ptrs in ICONBLK  */
#define R_IBPDATA   12
#define R_IBPTEXT   13
#define R_BIPDATA   14              /* sub ptrs in BITBLK   */
#define R_FRSTR     15              /* gets addr of ptr to free strings     */
#define R_FRIMG     16              /* gets addr of ptr to free images      */

extern RSHDR   *rs_hdr;
extern AESGLOBAL *rs_global;

WORD rs_readit(AESGLOBAL *pglobal, UWORD fd);
void fix_objects(void);

void *get_addr(UWORD rstype, UWORD rsindex);
void *get_sub(UWORD rsindex, UWORD offset, UWORD rsize);
CICONBLK **get_ciconblkptr(RSHDR *hdr);
void transform_all_cicons(LONG num_cicons, CICONBLK **ciconblkptr);

#endif
```

Note the R_* type constants move here from `gemrslib.c` (see Step 3) so all three files see them.

- [ ] **Step 2: Create `aes/rsload_legacy.c`**

Start with the header (no `#if CONF_WITH_LEGACY_RSC_LOAD` anywhere in this file):

```c
/*
 *  rsload_legacy.c - m68k in-place RSC loader
 *
 *  The former TOS resource loader: read the file into memory and fix
 *  resource offsets in place, relying on the m68k native structure layout
 *  matching Atari's big-endian resource-file layout.  Selected by build.mk
 *  when CONF_WITH_LEGACY_RSC_LOAD is set (the 192/256 KB ROMs).
 *
 *  Copyright (C) 2004-2017 The EmuTOS development team
 *  This file is distributed under the GPL, version 2 or at your
 *  option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "obdefs.h"
#include "rsdefs.h"
#include "gemdos.h"
#include "gemrslib.h"
#include "string.h"

#include "rsload.h"
```

Then move, verbatim (only the stated guard stripping/adding), these regions from `gemrslib.c`, in this order:

1. **CICON fixups** — current lines 581-658 (the body of the `#if CONF_WITH_LEGACY_RSC_LOAD` block 580-659 inside the `#if CONF_WITH_COLOUR_ICONS` block): `fixup_colour_icons()`, `fixup_all_ciconblks()`. Wrap this whole moved block in `#if CONF_WITH_COLOUR_ICONS` / `#endif` so the file is:

   ```c
   #if CONF_WITH_COLOUR_ICONS
   static CICONBLK *fixup_colour_icons(LONG num_cicons, LONG mono_words, CICON *start)
   {
       ...              /* verbatim, current lines 586-627 */
   }

   static void fixup_all_ciconblks(LONG num_blks, CICONBLK **ciconblkptr, CICON *cicondata)
   {
       ...              /* verbatim, current lines 628-658 */
   }
   #endif
   ```

2. **`fix_cicons()`** — current lines 1003-1017 (the `#if CONF_WITH_LEGACY_RSC_LOAD` arm of the dual 1002-1034). Wrap in `#if CONF_WITH_COLOUR_ICONS` / `#endif`:

   ```c
   #if CONF_WITH_COLOUR_ICONS
   static void fix_cicons(void)
   {
       ...              /* verbatim, current lines 1003-1017 */
   }
   #endif
   ```

3. **Fixup helpers** — current lines 1039-1090 (the body of the `#if CONF_WITH_LEGACY_RSC_LOAD` block 1038-1091), no guard: `fix_long()`, `fix_trindex()`, `fix_nptrs()`, `fix_ptr()`, `fix_tedinfo()`. They call `get_sub()`, `get_addr()`, `strlen()`, and the globals `rs_hdr`/`rs_global`, all available via `rsload.h`/`gemdos.h`/`string.h`.

4. **`fix_objects()`**, the legacy arm of the current dual at lines 1093-1139, as a plain `void fix_objects(void)` (no `static`):

   ```c
   void fix_objects(void)
   {
       WORD ii;
       WORD obtype;
       OBJECT *obj;
   #if CONF_WITH_COLOUR_ICONS
       CICONBLK **ciconblkptr = get_ciconblkptr(rs_hdr);
       OBSPEC *spec;
   #endif

       for (ii = 0; ii < rs_hdr->rsh_nobs; ii++)
       {
           obj = (OBJECT *)get_addr(R_OBJECT, ii);
           rs_obfix(obj, 0);
           obtype = obj->ob_type & 0x00ff;
           switch (obtype)
           {
           case G_CICON:
   #if CONF_WITH_COLOUR_ICONS
               if (ciconblkptr)
               {
                   if (obj->ob_flags & INDIRECT)
                       fix_long(&obj->ob_spec.index);
                   spec = (obj->ob_flags & INDIRECT) ? obj->ob_spec.indirect : &obj->ob_spec;
                   spec->ciconblk = ciconblkptr[spec->index];
               }
   #endif
               break;
           case G_BOX:
           case G_IBOX:
           case G_BOXCHAR:
               break;
           default:
               fix_long(&obj->ob_spec.index);
               break;
           }
       }
   }
   ```

5. **`rs_readit()`**, the legacy arm — current lines 1603-1648 (the body of the `#if CONF_WITH_LEGACY_RSC_LOAD` block 1602-1649), as a plain `WORD rs_readit(AESGLOBAL *pglobal, UWORD fd)` (no `static`).

The file ends after item 5. There must be no `#if CONF_WITH_LEGACY_RSC_LOAD` left in it.

- [ ] **Step 3: Trim `aes/gemrslib.c` and widen the interface**

Apply, in this order:

1. **Remove the R_* type constants** (current lines 46-62) — they now live in `rsload.h`.
2. **Add the include** `#include "rsload.h"` after the `#include "gemrslib.h"` block (after line 32/33 area, before `endian.h`).
3. **Globals**: change `static RSHDR *rs_hdr;` (line 68) and `static AESGLOBAL *rs_global;` (line 69) to non-`static` (drop the `static` keyword). `tmprsfname` and `free_str` stay `static`.
4. **Promote the four shared helpers to external** by dropping `static`: `get_sub()` (line 502), `get_addr()` (line 512), `transform_all_cicons()` (line 835), `get_ciconblkptr()` (line 937).
5. **Remove the legacy CICON fixups** — lines 580-659 entirely (the inner `#if CONF_WITH_LEGACY_RSC_LOAD` / `#endif` plus both functions). Line 579 `#if CONF_WITH_COLOUR_ICONS` stays; line 1035 `#endif` stays.
6. **Replace the dual at 1002-1034** (`#if CONF_WITH_LEGACY_RSC_LOAD` / `fix_cicons()` / `#else` / `transform_cicons()` / `#endif`) with the portable arm only, guarded so legacy builds skip it. It stays inside the surrounding `#if CONF_WITH_COLOUR_ICONS` block (opened at line 579), so only the inner guard is needed:

   ```c
   #if !CONF_WITH_LEGACY_RSC_LOAD
   static void transform_cicons(RSHDR *hdr)
   {
       ...              /* verbatim, current lines 1019-1033 */
   }
   #endif
   ```
7. **Remove the legacy fixup helpers** — lines 1038-1091 entirely (`#if CONF_WITH_LEGACY_RSC_LOAD` ... `#endif`).
8. **Replace `fix_objects()` (current 1093-1139)** with the portable arm only, non-`static`, guarded for legacy builds:

   ```c
   #if !CONF_WITH_LEGACY_RSC_LOAD
   void fix_objects(void)
   {
       WORD ii;
       WORD obtype;
       OBJECT *obj;
   #if CONF_WITH_COLOUR_ICONS
       OBSPEC *spec;
   #endif

       for (ii = 0; ii < rs_hdr->rsh_nobs; ii++)
       {
           obj = (OBJECT *)get_addr(R_OBJECT, ii);
           rs_obfix(obj, 0);
           obtype = obj->ob_type & 0x00ff;
           switch (obtype)
           {
           case G_CICON:
   #if CONF_WITH_COLOUR_ICONS
               spec = (obj->ob_flags & INDIRECT) ? obj->ob_spec.indirect : &obj->ob_spec;
               spec->ciconblk = get_ciconblkptr(rs_hdr)[spec->index];
   #endif
               break;
           case G_BOX:
           case G_IBOX:
           case G_BOXCHAR:
               break;
           default:
               break;
           }
       }
   }
   #endif
   ```

9. **Remove the legacy `rs_readit()`** — lines 1602-1649 entirely. The portable `rs_readit()` in the `#if !CONF_WITH_LEGACY_RSC_LOAD` region (1651+) must now be **non-`static`** (drop the `static` on line 1652's `static WORD rs_readit(...)`).

Do not touch the portable disk parser (73-446), the materializer (1141-1530), or the portable `rs_readit`/`rs_own_global`/`rs_loadmem` (1651-1725) — those move in Task 3.

- [ ] **Step 4: Wire the legacy loader into `aes/build.mk`**

```make
obj-$(CONF_WITH_LEGACY_RSC_LOAD) += rsload_legacy.o
```

- [ ] **Step 5: Build and compare against baseline**

```sh
make atari192_defconfig && make          # expect: # ptos192us.img done (1914 bytes free)
make atari256_defconfig && make          # expect: # ptos256us.img done (7115 bytes free)
make atari512_defconfig && make          # expect: # ptos512k.img done (16375 bytes free)
make rpi1_defconfig && make              # expect: identical RAM-used figure to baseline
```

Baseline (post-split, authoritative after owner acceptance of the inlining
drift): atari192 1914, atari256 7115, atari512 16375, rpi1 RAM 570496.  If a
size differs from this, stop: a move went wrong (e.g. a guard dropped on code
that should stay).

- [ ] **Step 6: `make gitready` and commit**

```sh
make gitready && git add aes/rsload.h aes/rsload_legacy.c aes/gemrslib.c aes/build.mk
git commit -m "aes: move legacy RSC loader into its own file"
```

---

### Task 3: Extract the portable loader

**Files:**
- Create: `aes/rsload_portable.c`
- Modify: `aes/gemrslib.c`
- Modify: `aes/build.mk`

**Interfaces:**
- Consumes: `aes/rsload.h` and the external shared interface from Task 2; `rs_readit()`/`fix_objects()` are currently defined (non-`static`) in `gemrslib.c`'s `#if !CONF_WITH_LEGACY_RSC_LOAD` regions.
- Produces: `rs_readit()`, `fix_objects()`, `transform_cicons()` (file-static), `rs_loadmem()`, `rs_own_global` (file-static) all defined in `rsload_portable.c`; `gemrslib.c` ends with no `#if CONF_WITH_LEGACY_RSC_LOAD` at all.

- [ ] **Step 1: Create `aes/rsload_portable.c`**

Start with the header:

```c
/*
 *  rsload_portable.c - portable canonical RSC loader
 *
 *  The bounds-checked big-endian disk parser plus a native materializer,
 *  with rs_loadmem().  Selected by build.mk whenever CONF_WITH_LEGACY_RSC_LOAD
 *  is off (CONF_WITH_PORTABLE_RSC_LOAD).
 *
 *  Copyright (C) 2004-2017 The EmuTOS development team
 *  This file is distributed under the GPL, version 2 or at your
 *  option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "obdefs.h"
#include "rsdefs.h"
#include "gemdos.h"
#include "gemrslib.h"
#include "string.h"

#include "rsload.h"
```

Then move, verbatim (only the stated guard stripping), these regions from `gemrslib.c`, in this order:

1. **Disk parser** — current lines 74-445 (the body of the `#if !CONF_WITH_LEGACY_RSC_LOAD` block 73-446): the `DISK_*` record constants, `struct disk_rsc`, `struct native_rsc_layout`, `struct disk_cicon_info`, and `disk_range`, `disk_uword`, `disk_ulong`, `disk_word`, `disk_decode_rshdr`, `disk_header`, `align_long`, `layout_add`, `userblk_offset`, `scan_disk_ciconblk`, `scan_disk_cicons`, `layout_ordinary`. No guard (the file is only built when portable).
2. **`transform_cicons()`** — the portable arm of the dual, current lines 1019-1033, wrapped in `#if CONF_WITH_COLOUR_ICONS` / `#endif`, kept `static`:

   ```c
   #if CONF_WITH_COLOUR_ICONS
   static void transform_cicons(RSHDR *hdr)
   {
       ...              /* verbatim, current lines 1019-1033 */
   }
   #endif
   ```
3. **`fix_objects()`** — the portable arm from Task 2 Step 3, now with **no** `#if !CONF_WITH_LEGACY_RSC_LOAD` guard and kept `void fix_objects(void)` (non-`static`). It is the same body that currently sits in `gemrslib.c` after Task 2.
4. **Materializer** — current lines 1142-1529 (the body of the `#if !CONF_WITH_LEGACY_RSC_LOAD` block 1141-1530): `native_disk_ptr`, `native_tedinfo_ptr`, `native_iconblk_ptr`, `native_bitblk_ptr`, `native_userblk_ptr`, `disk_string`, `disk_string_capacity`, `copy_disk_words`, `materialize_cicons`, `decode_object_spec`, `materialize_rsc`. No guard. `materialize_rsc()` calls `transform_cicons()` — defined above at item 2, so no forward declaration is needed.
5. **`rs_readit()`, `rs_own_global`, `rs_loadmem()`** — current lines 1652-1724 (the body of the `#if !CONF_WITH_LEGACY_RSC_LOAD` block 1651-1725): `rs_readit()` non-`static`, `rs_own_global` `static`, `rs_loadmem()` non-`static`.

There must be no `#if CONF_WITH_LEGACY_RSC_LOAD` left in this file.

- [ ] **Step 2: Trim `aes/gemrslib.c`**

Remove, entirely (including their guards):
- Lines 73-446: the `#if !CONF_WITH_LEGACY_RSC_LOAD` disk parser.
- The `#if !CONF_WITH_LEGACY_RSC_LOAD` + `transform_cicons()` + `#endif` block introduced in Task 2 Step 3 item 6.
- The `#if !CONF_WITH_LEGACY_RSC_LOAD` + portable `fix_objects()` + `#endif` block introduced in Task 2 Step 3 item 8.
- Lines 1141-1530: the `#if !CONF_WITH_LEGACY_RSC_LOAD` materializer.
- Lines 1651-1725: the `#if !CONF_WITH_LEGACY_RSC_LOAD` `rs_readit()`/`rs_own_global`/`rs_loadmem()`.

`gemrslib.c` must now contain **no** `#if CONF_WITH_LEGACY_RSC_LOAD` and **no** `rs_readit()` or `fix_objects()` definition; both come from the loader files. The `#if CONF_WITH_COLOUR_ICONS` block (still from line 579 to 1035) now holds only `best_match`, `expand_cicondata`, `transform_cicon`, `pack_planes`, `pack_cicon`, `transform_all_cicons`, `get_ciconblkptr`, `free_cicon_buffers`.

- [ ] **Step 3: Wire the portable loader into `aes/build.mk`**

```make
obj-$(CONF_WITH_PORTABLE_RSC_LOAD) += rsload_portable.o
```

- [ ] **Step 4: Build the full matrix and compare against baseline**

```sh
make atari192_defconfig && make          # expect: # ptos192us.img done (2068 bytes free)
make atari256_defconfig && make          # expect: # ptos256us.img done (7255 bytes free)
make atari512_defconfig && make          # expect: # ptos512k.img done (16461 bytes free)
make atari512_defconfig
printf 'CONF_WITH_VDI_CICON_TEST=y\n' >> .config
make                                     # expect: identical to Task 1's CICON_TEST baseline
make rpi1_defconfig && make              # expect: identical RAM-used figure to baseline
```

Note the atari512+CICON_TEST build exercises `rs_loadmem()` (called by `desk/deskmain.c`) from its new file, and rpi1 exercises `pack_planes()`/`pack_cicon()` via the truecolor backend.

- [ ] **Step 5: `make gitready` and commit**

```sh
make gitready && git add aes/rsload_portable.c aes/gemrslib.c aes/build.mk
git commit -m "aes: move portable RSC loader into its own file"
```

---

### Task 4: Final verification

**Files:**
- Verify only; no source changes expected. If `make gitready` or a build fails, fix and amend the relevant Task 2/3 commit, or add a follow-up commit.

- [ ] **Step 1: The French 256 KB variant still fits**

```sh
make atari256_defconfig && make COUNTRY=fr UNIQUE=fr
```

Must complete with `# ptos256fr.img done (N bytes free)` and N >= 0 (this is the Release archives CI constraint from the design doc).

- [ ] **Step 2: Re-run the full matrix once more from a clean tree**

```sh
make distclean
# then Tasks 2-4's builds in order; every image size / free byte / RAM figure
# must equal the post-split baseline exactly (atari192 1914, atari256 7115,
# atari512 16399, atari512+CICON_TEST 15255, rpi1 RAM 570368)
```

- [ ] **Step 3: Final review**

Confirm against the design doc (`docs/superpowers/specs/2026-08-10-rsrc-loader-split-design.md`):
- `gemrslib.c` no longer contains either `rs_readit()`.
- Neither loader file contains `#if CONF_WITH_LEGACY_RSC_LOAD`.
- `aes/build.mk` selects exactly one loader via the two `obj-$(CONF_...)` lines.
- `make gitready` passes.

Optionally smoke-boot the result with the `ptos-smoketest` skill (QEMU `raspi1ap` for rpi1; Hatari for atari256, which exercises the legacy loader at boot).

---

## Progress Ledger

### Task 1: Add the derived symbol and record the baseline — DONE (approved)

- Commit `c9d5d12f` "kconfig: add derived portable RSC loader option", task review approved (no issues).
- New authoritative baseline (design-doc figure in parens, when different):
  - atari192: 2036 bytes free (doc: 2068, −32, toolchain drift)
  - atari256: 7221 bytes free (doc: 7255, −34, toolchain drift)
  - atari512: 16461 bytes free (exact match)
  - atari512 + CONF_WITH_VDI_CICON_TEST=y: 15317 bytes free (not in doc)
  - rpi1: RAM-used 570368 (not in doc)
- `CONF_WITH_PORTABLE_RSC_LOAD` verified in `obj/autoconf.h`: 0 on atari192/256, 1 on atari512 and rpi1.
- Parking: `aes/Kconfig` gains the derived symbol; `#if CONF_WITH_LEGACY_RSC_LOAD` sites in `gemrslib.c` are at lines 580, 1002, 1038, 1093(dual), 1602, 1651+ (portable `#if !...`).

### Task 2: Extract the legacy loader — DONE (approved)

- Commit `fd49b3ac` "aes: move legacy RSC loader into its own file" — the split itself is faithful (byte-for-byte verified, `make gitready` passes, no `#if CONF_WITH_LEGACY_RSC_LOAD` in `rsload_legacy.c`).
- Plan contradiction resolved by owner: the `rsload.h` interface's static→external promotion disables cross-TU inlining (no LTO), so sizes drift. Owner decision: **accept the drift; post-split measured sizes are the new authoritative baseline** (design doc and plan updated accordingly).
- New authoritative baseline: atari192 **1914** free (was 2036), atari256 **7115** free (was 7221), atari512 **16375** free (was 16461), rpi1 RAM **570496** (was 570368).
- Root cause: baseline inlined static `rs_readit`/`fix_objects`/`get_sub` into `rs_load`/`rs_fixit`; external linkage forces standalone copies (+122/+106/+86 bytes on m68k, +128 on ARM).
- Review: approved. Task quality Approved, no Critical/Important.
- Task 2: minor (deferred): stale orphaned comment at `aes/gemrslib.c:44-45` ("type definitions for use by an application when calling rsrc_gaddr and rsrc_saddr") — was the banner for the removed R_* constants, now dangles above the LOCALS banner. Delete or reword to point at `rsload.h`. To fix before merge.

### Task 3: Extract the portable loader — DONE (owner ruling, pending review)

- Commit `e824c903` "aes: move portable RSC loader into its own file" (only `rsload_portable.c`, `gemrslib.c`, `build.mk`). Move is verbatim; `make gitready` passes; no `CONF_WITH_LEGACY_RSC_LOAD` in `rsload_portable.c`, none at all left in `gemrslib.c`.
- Deviation: added `#include "endian.h"` to `rsload_portable.c` (the brief's starter header omitted it; the moved disk parser uses `be2cpu16`/`be2cpu32` — verified, needed to link).
- Deviation: cosmetic — collapsed run-on blank lines and deleted two comments belonging to moved functions at the removal sites. No code changed.
- Size drift (same accepted TU-boundary class as Task 2): atari192/256 EXACT (1914/7115 — legacy path untouched); atari512 16375→**16399** (+24 free); atari512+CICON_TEST **15255** newly recorded (delta vs plain atari512 = 1144, identical to pre-split); rpi1 RAM 570496→**570368** (−128).
- Verified by implementer: clean worktree at `183abd93` reproduces both Task-2 baselines exactly; symbol-level nm on both arches shows every symbol moved intact, each of `rs_readit`/`fix_objects`/`rs_loadmem`/`rs_hdr`/`rs_global` defined exactly once; cross-TU undefined sets are precisely the documented loader↔library interface.
- Smoke tests pass: rpi1 boots to `evnt_multi()` under QEMU (16-bit truecolor, zero guest_errors); atari512 boots to GEM desktop under Hatari.
- New authoritative baseline (post-Task-3): atari192 **1914**, atari256 **7115**, atari512 **16399**, atari512+CICON_TEST **15255**, rpi1 RAM **570368**. Task 4 verifies against these.
- TODO: task review (spec + quality) still pending.

- Review: approved. Task quality Approved, no Critical/Important.
- Task 3: minor (deferred): `aes/rsload_portable.c` — the doc comment that preceded `rs_readit()` ("Read resource file into memory and fix everything up except the x,y,w,h parts…") was deleted instead of moving with the function; `rs_readit` now has no comment while `rs_loadmem` (whose comment was moved) does. Restore it.
- Task 3: minor (deferred): `aes/gemrslib.c` — the "initialise the colour icon stuff" block comment (CICONBLK pointer-table filling / pointer fixing / resolution selection / expansion / device conversion) was deleted, but it describes the functions that REMAIN in gemrslib.c's `#if CONF_WITH_COLOUR_ICONS` block (`transform_all_cicons`, `get_ciconblkptr`, `expand_cicondata`, …), leaving them undocumented. Re-attach above `transform_all_cicons`. To fix before merge.

---

## Self-Review

- **Spec coverage** (design doc `2026-08-10-rsrc-loader-split-design.md`):
  - Three-file architecture (context/architecture sections) -> Tasks 2, 3.
  - Loader selection via `build.mk` + invisible derived symbol (Loader Selection) -> Task 1 (symbol), Tasks 2/3 (build.mk lines).
  - Cross-file interface `rsload.h`, extern promotion, `-Wmissing-prototypes` (Cross-File Interface) -> Task 2 Steps 1-3.
  - `rs_loadmem()` guard in `gemrslib.h` unchanged; definition moves to portable file -> Task 3 Step 1 item 5.
  - Verification matrix incl. atari512+VDI_CICON_TEST and rpi1 truecolor, `make gitready`, French 256K fit -> Tasks 1-4.
  - Constraints (unique basenames, C90, no loader-guards in the new files, no public interface change) -> Global Constraints + Task steps.
- **Placeholder scan**: The only "verbatim, current lines N-M" references are for whole-function moves where the content is copied unchanged by construction; every re-written piece (header, both `fix_objects`, `transform_cicons`, guard skeletons, Kconfig, build.mk) is spelled out in full. No TBD/TODO.
- **Type consistency**: `rs_readit(AESGLOBAL*, UWORD) -> WORD`, `fix_objects(void) -> void`, `get_addr/get_sub/get_ciconblkptr/transform_all_cicons` signatures match the current `static` declarations being promoted (gemrslib.c lines 502, 512, 835, 937) and the design doc's header.
