# Generic Desktop Alert Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port upstream EmuTOS commit `47f05896` into this fork: replace the three desktop alert helpers (`fun_alert_merge`, `fun_alert_long`, `fun_alert_string`) with a single variadic `fun_alert_merge(WORD defbut, WORD stnum, ...)` and rename the two remaining call sites, keeping the fork's `rsrc_gaddr_rom` string fetch and `G.g_1text` buffer (issue #118).

**Architecture:** The fork already lacks the upstream `deskapp.c`/`deskdir.c` retry loops, so only the `deskfun.c`/`deskfun.h` consolidation plus two `desksupp.c` renames apply. The merged function reads the RSC string with `rsrc_gaddr_rom` (as the fork's `fun_alert` does) and `sprintf`s the `va_arg(ap, void *)` merge value into `G.g_1text`. Both `#if CONF_WITH_FORMAT` / `#if CONF_WITH_DESKTOP_SHORTCUTS` guards on the deleted helpers disappear; the variadic function is unconditional, matching upstream. See `docs/superpowers/specs/2026-08-08-port-generic-desktop-alert-design.md`.

**Tech Stack:** GCC C90 (`-std=gnu90`), `-Wundef`, `portab.h` types (`WORD`, `BYTE`, `LONG`, `void *`), `<stdarg.h>` (already used in `bios/kprint.c:16`, `desk/gembind.c:30`). Verification via `make` + `make gitready` and the `.claude/skills/ptos-smoketest/SKILL.md` invocations.

## Global Constraints

- C90 with GNU extensions: declarations at the top of a block; `/* */` comments. 4 spaces, never a hard tab. Run `make gitready` before committing.
- **`int` is 16 bits on m68k** (`-mshort`), 32 bits on ARM. `void *` is 32-bit on both targets.
- `-Wundef` is on: every `#if` symbol must be defined. Feature symbols are always defined `0`/`1`. Never edit `obj/autoconf.h` / `obj/auto.conf`.
- The fork has **no `desktop_str_addr`** — always fetch strings with `rsrc_gaddr_rom(R_STRING, stnum, (void **)&G.a_alert)`, exactly as `fun_alert()` at `desk/deskfun.c:68` does.
- The fork buffers merged alerts into `G.g_1text` (there is no `G.g_work` in this fork).
- `_Static_assert` is already used in this fork (`aes/gemaplib.c:203`) and is accepted by the toolchains.
- `deskdir.c:437` (`STDISKFU`) and `deskdir.c:878` (`STDELDIS`) already call `fun_alert_merge` — do not touch them.
- Verification before completion: build the affected configs and run the smoke tests listed in each task; report real output, not assumptions.
- The two renamed callers stay inside their existing `#if` blocks (`CONF_WITH_DESKTOP_SHORTCUTS` at `desksupp.c:306`, `CONF_WITH_FORMAT` at `desksupp.c:1147`). No Kconfig change.

---

### Task 1: Collapse the three alert helpers into variadic `fun_alert_merge`

Delivers the whole port: `deskfun.c`/`deskfun.h` consolidation and the two `desksupp.c` renames, verified by a clean build and a stale-reference grep.

**Files:**
- Modify: `desk/deskfun.c:22-29` (add `#include <stdarg.h>`), `desk/deskfun.c:73-110` (replace three functions)
- Modify: `desk/deskfun.h:20-29` (replace three prototypes + two `#if` guards)
- Modify: `desk/desksupp.c:344` (rename `fun_alert_string` → `fun_alert_merge`)
- Modify: `desk/desksupp.c:1410` (rename `fun_alert_long` → `fun_alert_merge`)
- Test: `configs/atari512_defconfig`, `configs/rpi2_defconfig`, `configs/virt-arm_defconfig`, `configs/atari512-dispatch_defconfig`, `configs/rpi2-sparse_defconfig`

**Interfaces:**
- Consumes: existing `rsrc_gaddr_rom` (from `aesbind.h`/`deskbind.h`, already used at `deskfun.c:68`), `form_alert()`, `G.a_alert`, `G.g_1text`, `filename_start()`, `sprintf`.
- Produces: single prototype `WORD fun_alert_merge(WORD defbut, WORD stnum, ...);` in `desk/deskfun.h`; variadic definition in `desk/deskfun.c`. Removes `fun_alert_long` and `fun_alert_string` entirely (no other references remain after the `desksupp.c` renames).

- [x] **Step 1: Add `#include <stdarg.h>` to deskfun.c**

In `desk/deskfun.c`, add `#include <stdarg.h>` immediately after the `/* #define ENABLE_KDEBUG */` block, before `#include "config.h"` (line 24):

```c
#include <stdarg.h>
#include "config.h"
```

The `#include <stdarg.h>` line is placed directly above the existing `#include "config.h"`.

- [x] **Step 2: Replace the three alert helpers in deskfun.c**

Replace the whole block from `desk/deskfun.c:73` (the comment `/*\n *  Issue an alert after merging in an optional character variable\n */`) through line 110 (`#endif` closing `CONF_WITH_DESKTOP_SHORTCUTS`) with:

```c

/*
 *  Issue an alert after merging in a variable
 *
 *  The following way of handling multiple types for the variable to be
 *  merged is a bit of a kludge, but at least we make an attempt to
 *  avoid obvious problems ...
 */
WORD fun_alert_merge(WORD defbut, WORD stnum, ...)
{
    va_list ap;
    _Static_assert(sizeof(void *) >= sizeof(long), "incompatible type sizes");

    va_start(ap, stnum);
    rsrc_gaddr_rom(R_STRING, stnum, (void **)&G.a_alert);
    sprintf(G.g_1text, G.a_alert, va_arg(ap, void *));
    va_end(ap);

    return form_alert(defbut, G.g_1text);
}
```

The result must look exactly like the upstream commit's `deskfun.c` hunk, except `rsrc_gaddr_rom(...)` replaces upstream's `desktop_str_addr(stnum)` and `G.g_1text` replaces upstream's `G.g_work`.

- [x] **Step 3: Replace the three prototypes in deskfun.h**

In `desk/deskfun.h`, replace lines 20-29 (the three prototypes plus both `#if` guard blocks) with a single prototype:

```c
WORD fun_alert(WORD defbut, WORD stnum);
WORD fun_alert_merge(WORD defbut, WORD stnum, ...);
```

The `fun_alert` line stays; `fun_alert_merge` becomes variadic; the `#if CONF_WITH_FORMAT` / `#if CONF_WITH_DESKTOP_SHORTCUTS` blocks around `fun_alert_long` / `fun_alert_string` are deleted.

- [x] **Step 4: Rename the two desksupp.c call sites**

In `desk/desksupp.c`:

- Line 344: `rc = fun_alert_string(1, STRMVLOC, filename_start(pa->a_pdata));` → `rc = fun_alert_merge(1, STRMVLOC, filename_start(pa->a_pdata));`
- Line 1410: `if (fun_alert_long(2, STFMTINF, avail) == 2)` → `if (fun_alert_merge(2, STFMTINF, avail) == 2)`

Do not change anything else on those lines or around them.

- [x] **Step 5: Grep for stale references**

Run:

```sh
grep -rn "fun_alert_string\|fun_alert_long" --include="*.c" --include="*.h" . | grep -v "\.git\|obj/"
```

Expected: no output (exit status 1). If any hits remain outside `obj/`, the renames were missed.

- [x] **Step 6: Build atari512 and rpi2**

```sh
make atari512_defconfig && make clean && make
make rpi2_defconfig && make clean && make
```

Expected: both build successfully with no warnings related to `deskfun` / `stdarg` / `fun_alert`. `rpi2` exercises the ARM toolchain; `atari512` exercises m68k with the guarded `CONF_WITH_FORMAT` caller (`STFMTINF`) compiled in.

- [x] **Step 7: Build with the guards toggled (atari512)**

Prove the merged function is unconditional — build with `CONF_WITH_DESKTOP_SHORTCUTS` and `CONF_WITH_FORMAT` both off:

```sh
make atari512_defconfig
sed -i 's/^CONF_WITH_DESKTOP_SHORTCUTS=y/# CONF_WITH_DESKTOP_SHORTCUTS is not set/; s/^CONF_WITH_FORMAT=y/# CONF_WITH_FORMAT is not set/' .config
make clean && make
```

Expected: builds clean. The guards now only gate the *callers* (the `STRMVLOC` block at `desksupp.c:306-…` and the format dialog at `desksupp.c:1147-…`), while the variadic `fun_alert_merge` itself always compiles. `.config` uses Kconfiglib format; commenting out the `=y` lines with the `# CONF_WITH_... is not set` form is the correct way to disable a symbol (there is no `scripts/config` tool in this fork).

- [x] **Step 8: Restore the config and run gitready**

```sh
make atari512_defconfig
make gitready
```

Expected: `make gitready` reports no whitespace/format problems.

- [x] **Step 9: Commit**

```bash
git add desk/deskfun.c desk/deskfun.h desk/desksupp.c
git commit -m "feat(desk): port generic desktop alert refactor (#118)"
```

---

### Task 2: Full configuration matrix and smoke tests

Delivers the spec's verification section: all five configs build with the expected object sets, and the GEM desktop is reachable under Hatari (atari512, atari512-dispatch) and QEMU (rpi2-sparse, virt-arm), with no new `guest_errors`.

**Files:**
- Test: `configs/atari512_defconfig`, `configs/rpi2_defconfig`, `configs/virt-arm_defconfig`, `configs/atari512-dispatch_defconfig`, `configs/rpi2-sparse_defconfig`
- Reference: `.claude/skills/ptos-smoketest/SKILL.md` (pass signals and QEMU/Hatari invocations)

**Interfaces:**
- Consumes: the variadic `fun_alert_merge` from Task 1 and the renamed callers.
- Produces: verified evidence for the PR that the port builds and boots on planar (m68k + ARM) and truecolor (m68k dispatch + ARM) configurations.

- [x] **Step 1: Build the full matrix from a clean tree**

```sh
make distclean
make atari512_defconfig && make
make rpi2_defconfig && make clean && make
make virt-arm_defconfig && make clean && make
make atari512-dispatch_defconfig && make clean && make
make rpi2-sparse_defconfig && make clean && make
```

Expected: all five build cleanly. `atari512` / `virt-arm` build the planar backend only (no `obj/vdi_backend*.o`); `rpi2` builds truecolor only (`obj/vdi_backend_truecolor.o`); `atari512-dispatch` / `rpi2-sparse` build both plus `obj/vdi_backend.o`.

- [x] **Step 2: Hatari boot atari512**

Follow `.claude/skills/ptos-smoketest/SKILL.md` for the atari512 Hatari invocation (after rebuilding `ptos512k.img` — the matrix leaves `rpi2-sparse`'s image as the last built, so run `make atari512_defconfig && make clean && make` first if `ptos512k.img` is absent).

Expected: GEM desktop reachable; all-pixel checkerboard counts `green > 40000 && white > 50000`. The known pre-existing `WARN : Bus Error reading at $4fffff/$cc03c3, PC=$e00c20` may appear — it is noise, identical on stock builds.

- [x] **Step 3: Hatari boot atari512-dispatch**

Run the atari512-dispatch Hatari invocation from the smoketest skill. Expected: desktop reachable (same checkerboard pass signal).

- [x] **Step 4: QEMU boot rpi2-sparse and virt-arm**

Rebuild each config (`make rpi2-sparse_defconfig && make clean && make`, then `make virt-arm_defconfig && make clean && make`) and boot under QEMU per the smoketest skill.

Expected: both reach the GEM desktop (`rc=124` after the timeout is the expected way to stop a healthy boot); `guest_errors` log empty or containing only pre-existing, unrelated messages.

- [x] **Step 5: Confirm no stale references in the final tree**

```sh
grep -rn "fun_alert_string\|fun_alert_long" --include="*.c" --include="*.h" . | grep -v "\.git\|obj/"
```

Expected: no output.

- [x] **Step 6: Final gitready and commit**

```sh
make atari512_defconfig
make gitready
git add -A
git commit -m "chore(desk): verify generic desktop alert port across all configs (#118)"
git push
```

Expected: `make gitready` clean; commit pushed; PR #141 left in draft for the maintainer to review and mark ready.
