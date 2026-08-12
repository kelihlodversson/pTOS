# Invert Pluggable-FS Dispatch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the 12 GEMDOS `x*` filesystem entry points thin shims that call `fs/pfs.c`'s `pfs_do_*()` (option on) or `fs/fatfs_pfs.c` path wrappers (option off); move the real FAT12/16 implementation out of `bdos/` into `fs/fatfs_pfs.c`; delete `pfs_is_fs_call()`/`pfs_dispatch()` and the `osif()` early-return block.

**Architecture:** A move, not a rewrite. The dependency points one way: thin `bdos/` shims call into `fs/`. Each opcode is moved atomically in three pieces that land together — (1) the `(DND, name)`-shaped core relocates from a `bdos/` body to `fs/fatfs_pfs.c`, dropping its `findit()` (the cookie already carries the containing directory); (2) the `fat_pfs_ops` vtable entry is repointed at that core; (3) the `bdos/` `x*` function becomes a `#if`/`#else` shim that calls `pfs_do_*()` (on) or the new `fat_*_path()` wrapper (off). The off wrapper does `findit()` + a transient `PFSCOOKIE` + the same core, so the off path is bit-for-bit unchanged. `osif()` reverts to always dispatching through `funcs[]`; the on-mode dispatch lives in the shims' `#if` branches, replacing the two (ARM/m68k) `pfs_dispatch()` translators.

**Tech Stack:** C90 (`-std=gnu90`), GNU make + Kbuild-style `build.mk`, Kconfig (`pip3 install kconfiglib`), `arm-none-eabi-` and `m68k-atari-mintelf-`/`m68k-elf-` toolchains. No libc; everything via `util/` + `include/`. Types from `portab.h` (`WORD`, `LONG`, `ULONG`, `UBYTE`, `UWORD`, `BOOL`). `int` is 16-bit on m68k (`-mshort`), 32-bit on ARM.

## Global Constraints

Copied verbatim from the approved spec `docs/superpowers/specs/2026-08-11-invert-pluggable-fs-dispatch-design.md`:

- A move, not a rewrite: FAT logic bodies relocate with behaviour intact; only their entry shape changes (path → `(DND, name)` cookie core, `findit()` moved into the off wrappers).
- Off-path behaviour must be bit-for-bit unchanged; the on path must keep working and keep the foreign-driver `pfs_test` passing.
- Source basenames stay unique across the tree (`fatfs_pfs.c`, `pfs.c`, `pfs_test.c` are unique).
- C90 declarations-before-statements, 4-space indent, `/* */` comments, no hard tabs — `make gitready` must pass.
- No changes to the public `bdos/funcs[]` wiring or the `Fread`/`Fwrite`/`Fclose` dispatch (`xread`/`xwrite`/`xclose` keep their existing `#if CONF_WITH_PLUGGABLE_FS` branches).
- `-Wundef` is on; `-Wmissing-prototypes` makes every non-static function need a prototype. Any helper that stays `static` in `bdos/` and is only called by moved code will become "defined-but-not-used" once the move completes — the plan handles this by either exporting the multi-use ones or moving the single-use ones.

### Recursion hazard (read before Task 2)

Several existing `fat_*` vtable adapters call back into `bdos/` `x*` functions (e.g. `fat_dfree` → `xgetfree`, `fat_open` → `xopen`). While a task is in flight, if the `bdos/` shim is converted into a dispatcher but the vtable still points at the old adapter, the on path recurses (`pfs_do_*` → vtable → adapter → `x*` shim → `pfs_do_*` → …). **Each opcode task must land the core + the vtable repoint + the shim conversion together in one commit** so no intermediate `make`/boot ever sees a half-moved opcode. Tasks are ordered so that an opcode's core exists before any other core that cross-calls it.

### Test cycle (no unit test harness — the build IS the gate)

This is a freestanding OS image with no host test runner. The per-task test cycle is:

1. Build an **off** config (`make atari512_defconfig && make`) — the majority case, must stay byte-for-byte.
2. Build an **on** config (`make virt-arm_defconfig && make`) — exercises the vtable + `pfs_test` driver.
3. `make gitready` — enforces style/no-tabs/`-Wundef`/`-Wmissing-prototypes`.
4. Commit on the `feature/174-invert-pluggable-fs-dispatch` topic branch (NOT master).

Functional boot verification (QEMU raspi1ap/raspi2b/virt-arm/virt-m68k, Hatari for m68k Atari targets, per the `ptos-smoketest` skill) is deferred to Task 16, after all opcodes are moved and `osif()` is restored — booting every intermediate state would be wasteful, and the build guarantees the off path is bit-for-bit (no code in the off path changes shape until its opcode's task).

---

## File Structure

### New files
- `fs/fatfs.h` — GEMDOS-shaped `fat_*_path()` entry points called from the `bdos/` shims when the option is off. Kept separate from `fs/pfs.h` (which remains the pluggable-machinery interface) so that `bdos/` shim files depend on `fs/` one way only, and so `pfs.h`'s "pluggable-only" framing from the spec is preserved.

### Modified files
- `Makefile` (~line 112): `core-dirs-y = bios bdos fs util`; drop `fs` from `optional-dirs-$(CONF_WITH_PLUGGABLE_FS)`. (`bdos_copts = -Ifs` and `fs_copts = -Ibdos` are already unconditional at Makefile:234/238.)
- `fs/build.mk`: `obj-y += fatfs_pfs.o`; `obj-$(CONF_WITH_PLUGGABLE_FS) += pfs.o`; `obj-$(CONF_WITH_PLUGGABLE_FS_TEST) += pfs_test.o`. Update the header comment.
- `fs/Kconfig`: update `CONF_WITH_PLUGGABLE_FS` help text to say it gates only the dispatch machinery, not the FAT wrapper.
- `fs/pfs.h`: add `pfs_do_*()` prototypes (12); drop `pfs_is_fs_call()` and `pfs_dispatch()` declarations + their doc comment; update the file header.
- `fs/pfs.c`: drop `static` from the 12 `pfs_do_*()`; delete `pfs_is_fs_call()`, `pfs_dispatch()` (both ARM/m68k bodies), the `FN_*` defines (now unused). Keep the drive table, `pfs_resolve_dir`, cwd tracking, search state, `pfs_handle_read/write/close`, `pfs_register_drive`, `pfs_proc_exit`, `pfs_cwd_addref`.
- `fs/fatfs_pfs.c`: receives the moved FAT cores (always compiled); the existing `fat_root`/`fat_lookup`/`fat_readdir`/pool/`fat_release`/`fat_abspath`/vtable become `#if CONF_WITH_PLUGGABLE_FS`-gated; gains the `fat_*_path()` off wrappers under `#if !CONF_WITH_PLUGGABLE_FS`; gains the moved helpers (`fat_countfree` ex-`countfree16`, `contains_wildcard_characters`, `namlen`, `dots`, `update_fcb`, `is_subdir`, `unpackit`).
- `bdos/fs.h`: add prototypes for `opnfil()`, `makdnd()`, `getdnd()`, `freednd()`, `packit()` (currently `static`); drop `ixcreat()` and `ixsfirst()`'s search role is unchanged so `ixsfirst` keeps its prototype. Actually `ixcreat` prototype is dropped only in the task that deletes the last `ixcreat` caller (Task 13).
- `bdos/fsopnclo.c`: `xopen`/`xcreat`/`xunlink` → shims; `ixopen` deleted (only `xopen` used it); `ixcreat` → shim then deleted in Task 13; `opnfil`/`makopn` stay (exported). `contains_illegal_characters` stays.
- `bdos/fsdir.c`: `xmkdir`/`xrmdir`/`xchmod`/`xrename`/`xchdir`/`xgetdir`/`xsfirst`/`xsnext` → shims; `ixsfirst`/`ixsnext` stay (existence-check + search primitives); helpers `makdnd`/`getdnd`/`freednd`/`packit` stay (exported); `contains_wildcard_characters`/`namlen`/`dots`/`update_fcb`/`is_subdir`/`unpackit` move to `fs/fatfs_pfs.c`; `match`/`snipdnd`/`dcrack`/`getpath`/`dirscan`/`findit`/`scan`/`builds`/`dopath`/`makbuf`/`makofd`/`dirinit` stay.
- `bdos/fsfat.c`: `xgetfree` → shim; `countfree16` moves to `fs/fatfs_pfs.c` as `fat_countfree`.
- `bdos/bdosmain.c`: delete the `#if CONF_WITH_PLUGGABLE_FS` `osif()` early-return block (lines ~674-692) in Task 13; `pfs.h` include stays gated (only `pfs_handle_*`-using files keep gated includes; the shim files include `fatfs.h` always and `pfs.h` under `#if`).
- `configs/virt-arm_defconfig`, `configs/virt-m68k_defconfig`: add `CONF_WITH_PLUGGABLE_FS=y` and `CONF_WITH_PLUGGABLE_FS_TEST=y` (Task 1, regenerated with `make savedefconfig`).

### Helper disposition table (locked in by Task 1's exports, used by Tasks 2–13)

| Symbol | Today | Disposition |
| --- | --- | --- |
| `opnfil` | static, fsopnclo.c | **stay** in bdos, un-static, prototype in `fs.h` (Task 1) |
| `makdnd` | static, fsdir.c | **stay** (used by `scan` + scavenger), un-static, prototype in `fs.h` (Task 1) |
| `getdnd` | static, fsdir.c | **stay** (used by `scan` + `xrename`/`fat_rename` core), un-static, prototype in `fs.h` (Task 1) |
| `freednd` | static, fsdir.c | **stay** (used by `xmkdir`/`fat_mkdir` core + scavenger), un-static, prototype in `fs.h` (Task 1) |
| `packit` | static, fsdir.c | **stay** (used by `dopath`/`makbuf`/`is_subdir`), un-static, prototype in `fs.h` (Task 1) |
| `contains_illegal_characters` | already public, fsopnclo.c | **stay** (already in `fs.h`) |
| `contains_wildcard_characters` | static, fsdir.c (only `xsfirst` + `xchdir`) | **move** to fatfs_pfs.c (Task 7 — first task that moves a user) |
| `namlen`, `dots` | static, fsdir.c (only `xmkdir`) | **move** to fatfs_pfs.c (Task 5) |
| `update_fcb` | static, fsdir.c (only `xrename`) | **move** to fatfs_pfs.c (Task 11) |
| `is_subdir`, `unpackit` | static, fsdir.c (only `xrename`) | **move** to fatfs_pfs.c (Task 11) |
| `match`, `snipdnd`, `dcrack`, `getpath`, `dirscan` | static, fsdir.c | **stay** (used by staying `findit`/`scan`/`ixsnext`) |
| `countfree16` | static, fsfat.c (only `xgetfree`) | **move** to fatfs_pfs.c as `fat_countfree` (Task 2) |
| `ixsfirst`, `ixsnext`, `makbuf` | already public (`fs_internal.h`/`fs.h`) | **stay** (off `Fsfirst`/`Fsnext` + existence check + on `fat_readdir`) |

---

## Task 1: Build split, exports, headers, defconfigs (mechanical scaffolding)

**Files:**
- Modify: `Makefile:112-119`, `fs/build.mk`, `fs/fatfs_pfs.c` (gate ON-only code for the off build), `fs/pfs.h` (add `pfs_do_*` prototypes), `fs/pfs.c` (un-static `pfs_do_*`), `bdos/fs.h` (export 5 helpers), `bdos/fsopnclo.c:74` + `bdos/fsdir.c:141-154` (un-static 5 helpers), `configs/virt-arm_defconfig`, `configs/virt-m68k_defconfig`.
- Create: `fs/fatfs.h`.

**Interfaces:**
- Produces for later tasks: `long pfs_do_dfree(WORD,ULONG*)`, `pfs_do_mkdir(const char*)`, `pfs_do_rmdir(const char*)`, `pfs_do_chdir(const char*)`, `pfs_do_getdir(char*,WORD)`, `pfs_do_open(const char*,WORD)`, `pfs_do_create(const char*,UWORD)`, `pfs_do_unlink(const char*)`, `pfs_do_chmod(const char*,WORD,WORD)`, `pfs_do_rename(const char*,const char*)`, `pfs_do_sfirst(char*,WORD)`, `pfs_do_snext(void)` — declared in `fs/pfs.h`, defined non-static in `fs/pfs.c`.
- Produces: `long opnfil(FCB*,DND*,int)`, `DND *makdnd(DND*,FCB*)`, `DND *getdnd(char*,DND*)`, `void freednd(DND*)`, `char *packit(char*,char*)` — prototypes in `bdos/fs.h`.
- Produces: `fs/fatfs.h` with the 12 `fat_*_path()` prototypes (used by later tasks' off wrappers + shims).

- [ ] **Step 1: Move `fs/` to core dirs.** In `Makefile` change:
```make
core-dirs-y = bios bdos util
```
and delete the line `optional-dirs-$(CONF_WITH_PLUGGABLE_FS) += fs`.

- [ ] **Step 2: Rewrite `fs/build.mk`** to:
```make
#
# fs/build.mk - the filesystem layer
#
# fatfs_pfs.c (the built-in FAT implementation) is always built; pfs.c
# (the pluggable dispatch machinery) only when CONF_WITH_PLUGGABLE_FS is
# set, and pfs_test.o only with the self-test driver.  See
# docs/superpowers/specs/2026-08-11-invert-pluggable-fs-dispatch-design.md
#
obj-y += fatfs_pfs.o
obj-$(CONF_WITH_PLUGGABLE_FS) += pfs.o
obj-$(CONF_WITH_PLUGGABLE_FS_TEST) += pfs_test.o
```

- [ ] **Step 3: Make `fs/fatfs_pfs.c` compile when the option is off.** Until the opcode tasks move real code in, the whole current body is ON-only and would produce "defined-but-not-used" statics (`fat_open`, `fat_create`, …) under the off build. Wrap everything after the includes in `#if CONF_WITH_PLUGGABLE_FS … #endif`. (The includes themselves — `config.h`, `portab.h`, `pfs.h`, `fs.h`, `fs_internal.h`, `fatfs.h`, `gemerror.h`, `biosbind.h`, `string.h`, `kprint.h`, `endian.h` — stay outside the `#if`.) Add `#include "fatfs.h"` after `fs_internal.h`. This produces an empty translation unit when off, which links cleanly.

- [ ] **Step 4: Create `fs/fatfs.h`:**
```c
/*
 * fatfs.h - GEMDOS-shaped entry points into the built-in FAT driver
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * Called from bdos/'s thin GEMDOS shims when CONF_WITH_PLUGGABLE_FS is
 * off (the majority case): there is then exactly one filesystem, so the
 * shims call the compiled-in FAT implementation directly instead of
 * going through fs/pfs.c.  When the option is on the shims call the
 * pfs_do_*() helpers (fs/pfs.h) instead and none of these are used.
 */

#ifndef FATFS_H
#define FATFS_H

#include "portab.h"

long fat_open_path(char *name, int mod);
long fat_creat_path(char *name, char attr);
long fat_unlink_path(char *name);
long fat_getfree_path(long *buf, int drv);
long fat_mkdir_path(char *s);
long fat_rmdir_path(char *p);
long fat_chmod_path(char *p, int wrt, char mod);
long fat_chdir_path(char *p);
long fat_getdir_path(char *buf, int drv);
long fat_sfirst_path(char *name, int att);
long fat_snext_path(void);
long fat_rename_path(char *p1, char *p2);

#endif /* FATFS_H */
```

- [ ] **Step 5: Un-static the 12 `pfs_do_*()` in `fs/pfs.c`** (delete the leading `static` on the definitions at pfs.c:515, 529, 553, 643, 742, 787, 824, 861, 885, 921, 959, 1063) and add to `fs/pfs.h` (after the existing `pfs_proc_exit` / `pfs_cwd_addref` block, before `#endif`):
```c
/* The 12 GEMDOS-shaped helpers the bdos/ x* shims call when
 * CONF_WITH_PLUGGABLE_FS is on.  Signatures mirror the GEMDOS opcodes:
 * dfree/getdir take the 1-based-or-0 drive word GEMDOS passes; the path
 * ones take raw path strings.  See the mapping table in
 * docs/superpowers/specs/2026-08-11-invert-pluggable-fs-dispatch-design.md */
LONG pfs_do_dfree(WORD drv, ULONG *buf);
LONG pfs_do_mkdir(const char *path);
LONG pfs_do_rmdir(const char *path);
LONG pfs_do_chdir(const char *path);
LONG pfs_do_getdir(char *buf, WORD drv);
LONG pfs_do_open(const char *path, WORD mode);
LONG pfs_do_create(const char *path, UWORD attr);
LONG pfs_do_unlink(const char *path);
LONG pfs_do_chmod(const char *path, WORD wrt, WORD mod);
LONG pfs_do_rename(const char *p1, const char *p2);
LONG pfs_do_sfirst(char *path, WORD att);
LONG pfs_do_snext(void);
```
Leave `pfs_is_fs_call`/`pfs_dispatch` and the `FN_*` defines in place for now (deleted in Task 13). Adding non-static functions with prototypes satisfies `-Wmissing-prototypes`; they are still called by `pfs_dispatch` so no "unused" warning.

- [ ] **Step 6: Export 5 bdos helpers.** In `bdos/fsopnclo.c:74` change `static long opnfil(FCB *f, DND *dn, int mod);` → `long opnfil(FCB *f, DND *dn, int mod);` and at the definition `fsopnclo.c:328` drop `static`. In `bdos/fsdir.c` drop `static` from the `makdnd`/`getdnd`/`freednd`/`packit` forward prototypes (lines 142, 150, 152, 141-ish) and their definitions. Add to `bdos/fs.h` (in the "in fsdir.c" / "in fsopnclo.c" sections):
```c
DND *makdnd(DND *p, FCB *b);
DND *getdnd(char *n, DND *d);
void freednd(DND *dn);
char *packit(char *s, char *d);
long opnfil(FCB *f, DND *dn, int mod);
```

- [ ] **Step 7: Add the two defconfig lines.** For each of `configs/virt-arm_defconfig` and `configs/virt-m68k_defconfig`:
```sh
make virt-arm_defconfig
make menuconfig   # enable Pluggable filesystem driver support + the self-test driver
make savedefconfig
cp defconfig configs/virt-arm_defconfig
```
Repeat for `virt-m68k`. The resulting defconfigs gain `CONF_WITH_PLUGGABLE_FS=y` and `CONF_WITH_PLUGGABLE_FS_TEST=y`.

- [ ] **Step 8: Build both ways.**
```sh
make atari512_defconfig && make
make virt-arm_defconfig && make
make gitready
```
Expected: both build clean; off build links an empty `fatfs_pfs.o`; on build links `pfs.o` + `pfs_test.o` + the (still-gated) old-adapter `fatfs_pfs.o` and boots as before — the `osif()` block still intercepts, so behaviour is unchanged. `make gitready` passes.

- [ ] **Step 9: Commit.**
```sh
git add Makefile fs/build.mk fs/fatfs.c fs/fatfs_pfs.c fs/pfs.h fs/pfs.c bdos/fs.h bdos/fsopnclo.c bdos/fsdir.c configs/virt-arm_defconfig configs/virt-m68k_defconfig
git commit -m "fs: build fatfs_pfs.c unconditionally; export pfs_do_*/helpers (#174)"
```

---

## Per-opcode move template

Tasks 2–11 each move exactly one opcode (Fsfirst/Fsnext paired). Each task's core and wrapper share this shape — the cores are cookie-shaped (`PFSCOOKIE *dir, const char *name, …`), internally extract `DND *dn = (DND *)dir->index;`, and never call `findit`/`x*` (they call the other fatfs_pfs.c cores or already-public bdos primitives). The off wrapper is always:

```c
static LONG fat_<op>_path(<GEMDOS args>)
{
    DND *dn;
    const char *s;
    PFSCOOKIE dir, out;   /* "out" only for open/create */
    LONG rc;

    if ((long)(dn = findit(<path-arg>, &s, <dflag>)) < 0)
        return (long)dn;
    if (!dn)
        return <EPTHNF or EFILNF, matching today's x* body>;
    dir.fs = NULL;        /* fat_pfs_ops is gated ON; NULL fs is never
                           * dereferenced by the core or pfs.c's
                           * native_handles check (pfs.c not built off) */
    dir.index = (LONG)dn;
    dir.aux = 0;
    dir.pos = 0;
    rc = fat_<op>(&dir, s, <rest>);
    if (rc < 0)
        return rc;
    return out.index;     /* only for open/create */
}
```

And the shim is always:

```c
long x<op>(<GEMDOS args>)
{
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_<op>(<args>);
#else
    return fat_<op>_path(<args>);
#endif
}
```

(The exact `<args>` and which mask (`& VALID_FOPEN_BITS`, `& ~FA_SUBDIR`) carries through from today's `x*` are spelled out per task.)

---

## Task 2: Dfree (opcode 0x36, `xgetfree` → `pfs_do_dfree` / `fat_getfree_path`)

**Files:**
- Create core in: `fs/fatfs_pfs.c` (in the always-compiled region — un-gate it).
- Modify: `bdos/fsfat.c` (delete `countfree16` + `xgetfree` body, keep `xgetfree` shim).

**Interfaces:**
- Consumes (Task 1): `pfs_do_dfree(WORD,ULONG*)`, `ckdrv`, `drvtbl`, `getrealcl`, `DMD`, `ERR`.
- Produces: `fat_dfree(fs,drive,out)` vtable entry (used by the on path), `fat_getfree_path(buf,drv)` off wrapper.

- [ ] **Step 1: Move `countfree16` to `fs/fatfs_pfs.c` as `fat_countfree`.** Relocate the body at `bdos/fsfat.c:299-328` verbatim, rename to `fat_countfree`, make `static`. It uses `getrec` (fs.h:438), `DMD` fields — all already public.

- [ ] **Step 2: Add the `fat_dfree` core**, replacing the existing gated `fat_dfree` adapter (which called `xgetfree`). The new core takes a 0-based drive (the vtable contract) and does what `xgetfree`'s body does after the drive-normalisation line:
```c
static LONG fat_dfree(struct pfs_ops *fs, WORD drive, ULONG out[4])
{
    CLNO i, free;
    long n;
    DMD *dm;

    (void)fs;
    if ((n = ckdrv(drive, TRUE)) < 0)
        return ERR;
    dm = drvtbl[n];
    if (dm->m_16)
        free = fat_countfree(dm);
    else
    {
        free = 0;
        for (i = 0; i < dm->m_numcl; i++)
            if (!getrealcl(i + 2, dm))
                free++;
    }
    out[0] = (ULONG)free;
    out[1] = (ULONG)dm->m_numcl;
    out[2] = (ULONG)dm->m_recsiz;
    out[3] = (ULONG)dm->m_clsiz;
    return E_OK;
}
```
Place this OUTSIDE the `#if CONF_WITH_PLUGGABLE_FS` region (it is used by both the on vtable and the off wrapper). Delete the old gated `fat_dfree` adapter.

- [ ] **Step 3: Add the off wrapper** under `#if !CONF_WITH_PLUGGABLE_FS`:
```c
static LONG fat_getfree_path(long *buf, int drv)
{
    WORD drive = drv ? (WORD)(drv - 1) : run->p_curdrv;
    return fat_dfree(NULL, drive, (ULONG *)buf);
}
```
(`run` is the global PD*; `p_curdrv` is the current-drive word. `fat_getfree_path` does the exact drive-normalisation `xgetfree` did today.)

- [ ] **Step 4: Convert `xgetfree` in `bdos/fsfat.c` to a shim** (delete the `countfree16` static + the `xgetfree` body):
```c
long xgetfree(long *buf, int drv)
{
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_dfree(drv, (ULONG *)buf);
#else
    return fat_getfree_path(buf, drv);
#endif
}
```
Add `#include "fatfs.h"` (always) and keep `#include "pfs.h"` under the existing `#if CONF_WITH_PLUGGABLE_FS` if present (add it if not). `pfs_do_dfree` itself normalises `drv` to 0-based — matches today's behaviour where the on adapter called `xgetfree((long*)out, drive+1)`.

- [ ] **Step 5: Build both, gitready, commit.**
```sh
make atari512_defconfig && make
make virt-arm_defconfig && make
make gitready
git add fs/fatfs_pfs.c bdos/fsfat.c
git commit -m "fs: move Dfree (0x36) into fatfs_pfs core; xgetfree becomes shim (#174)"
```

---

## Task 3: Fopen (opcode 0x3D, `xopen` → `pfs_do_open` / `fat_open_path`)

**Files:**
- Create core in: `fs/fatfs_pfs.c`.
- Modify: `bdos/fsopnclo.c` (delete `ixopen` static; convert `xopen` to shim).

**Interfaces:**
- Consumes (Task 1): `opnfil`, `scan`, `FA_NORM`, `FA_RO`, `EFILNF`, `EACCDN`, `RO_MODE`.
- Produces: `fat_open(dir,name,mode,out)` vtable + `fat_open_path(name,mod)` off wrapper.

- [ ] **Step 1: Add the `fat_open` core** (the `ixopen` body at `fsopnclo.c:232-258` with `findit` removed and the handle returned via `out->index`):
```c
static LONG fat_open(PFSCOOKIE *dir, const char *name, WORD mode, PFSCOOKIE *out)
{
    FCB *f;
    DND *dn = (DND *)dir->index;
    long pos;
    LONG rc;

    pos = 0;
    if (!(f = scan(dn, name, FA_NORM, &pos)))
        return EFILNF;
    if ((f->f_attrib & FA_RO) && (mode != 0))
        return EACCDN;
    rc = opnfil(f, dn, mode);
    if (rc < 0)
        return rc;
    out->fs = dir->fs;
    out->index = rc;
    out->aux = 0;
    out->pos = 0;
    return E_OK;
}
```
Replace the gated ON-only old `fat_open` adapter (which called `xopen`). Place the new core in the always-compiled region. `fat_pfs_ops`'s `.open` now points at it (Task 1 left the vtable gated ON, so the repoint is inside that `#if` block — change `fat_open` reference; the function symbol is the same name, so just delete the old adapter and the vtable initializer already names `fat_open`).

- [ ] **Step 2: Add the off wrapper** under `#if !CONF_WITH_PLUGGABLE_FS`:
```c
static LONG fat_open_path(char *name, int mod)
{
    DND *dn;
    const char *s;
    PFSCOOKIE dir, out;
    LONG rc;

    if ((long)(dn = findit(name, &s, 0)) < 0)
        return (long)dn;
    if (!dn)
        return EFILNF;
    dir.fs = NULL;
    dir.index = (LONG)dn;
    dir.aux = 0;
    dir.pos = 0;
    rc = fat_open(&dir, s, mod, &out);
    if (rc < 0)
        return rc;
    return out.index;
}
```

- [ ] **Step 3: Convert `xopen` in `bdos/fsopnclo.c` to a shim** and delete the `ixopen` static (body now lives as `fat_open`):
```c
long xopen(char *name, int mod)
{
    int m = mod & VALID_FOPEN_BITS;
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_open(name, m);
#else
    return fat_open_path(name, m);
#endif
}
```
Delete the `static long ixopen(...)` prototype (line 73) and the definition (232-258). `VALID_FOPEN_BITS` is masked once here so both modes see the same masked mode — today the on adapter masked via `xopen`, the off path masked in `xopen`; both end up masked, so this preserves both. (`kpgmld.c:55`'s `xopen(s, 0)` still works — 0 masks to 0.)

- [ ] **Step 4: Build both, gitready, commit.**
```sh
make atari512_defconfig && make && make virt-arm_defconfig && make && make gitready
git add fs/fatfs_pfs.c bdos/fsopnclo.c
git commit -m "fs: move Fopen (0x3D) into fatfs_pfs core; xopen becomes shim (#174)"
```

---

## Task 4: Fcreate (opcode 0x3C, `xcreat` → `pfs_do_create` / `fat_creat_path`)

**Files:**
- Create core in: `fs/fatfs_pfs.c`.
- Modify: `bdos/fsopnclo.c` (convert `xcreat` AND `ixcreat` to shims; the `ixcreat` body is what moves).

**Interfaces:**
- Consumes: `opnfil`, `scan`, `makofd`, `ixdel`, `nextcl`, `dirinit`, `builds`, `contains_illegal_characters`, `getofd`, `ixlseek`, `ixread`, `ixwrite`, `ixclose`, `current_time`, `current_date`, `le2cpu16`, `CL_DIR`, `O_DIRTY`, `RO_MODE`, `RW_MODE`, `FA_VOL`, `FA_SUBDIR`, `FA_NORM`, `FA_RO`.
- Produces: `fat_create(dir,name,attr,out)` vtable; `fat_creat_path(name,attr)` off wrapper; `ixcreat` still exists as a bdos shim (used by `xmkdir`/`xrename` until Tasks 5/11 move them).

- [ ] **Step 1: Add includes to `fs/fatfs_pfs.c`.** Add `#include "endian.h"` (for `le2cpu16`) and `#include "time.h"` (for `current_time`/`current_date`, both `extern UWORD` in `bdos/time.h` — verify the include path; if `bdos/time.h` isn't on `fs_copts`'s `-I` path, copy the two `extern UWORD current_time, current_date;` declarations into the file with a comment instead of fighting the include path). Add `#include "mem.h"` if `O_DIRTY`/`OFD`/`DFD` macros need it (they're in `fs.h`, already included).

- [ ] **Step 2: Add the `fat_create` core** = the `ixcreat` body at `fsopnclo.c:99-208` with the `findit` block (lines 113-116) removed and the handle returned via `out->index`. Keep every other line verbatim — the final-name checks `if (!*s || (*s == '.'))`, `contains_illegal_characters`, the volume-label checks, the `scan`/`ixdel`/grow/`builds`/`ixwrite`/`ixclose`/`ixread`/`opnfil`/`getofd(...)->o_dfd->o_flag |= O_DIRTY` sequence. Parameter: `PFSCOOKIE *dir, const char *name, UWORD attr, PFSCOOKIE *out`; `DND *dn = (DND *)dir->index; const char *s = name;`. Replace `f->f_attrib = attr;` with `f->f_attrib = (UBYTE)attr;` (attr was `char`, now `UWORD`). End:
```c
    out->fs = dir->fs;
    out->index = f2;
    out->aux = 0;
    out->pos = 0;
    return E_OK;
```
Replace the gated old `fat_create` adapter; the vtable `.create` already names `fat_create`.

- [ ] **Step 3: Add the off wrapper** under `#if !CONF_WITH_PLUGGABLE_FS`:
```c
static LONG fat_creat_path(char *name, char attr)
{
    DND *dn;
    const char *s;
    PFSCOOKIE dir, out;
    LONG rc;

    if ((long)(dn = findit(name, &s, 0)) < 0)
        return (long)dn;
    if (!dn)
        return EPTHNF;
    dir.fs = NULL;
    dir.index = (LONG)dn;
    dir.aux = 0;
    dir.pos = 0;
    rc = fat_create(&dir, s, (UWORD)(UBYTE)attr, &out);
    if (rc < 0)
        return rc;
    return out.index;
}
```

- [ ] **Step 4: Convert `xcreat` to a shim** in `bdos/fsopnclo.c`:
```c
long xcreat(char *name, char attr)
{
    int a = attr & ~FA_SUBDIR;
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_create(name, (UWORD)a);
#else
    return fat_creat_path(name, (char)a);
#endif
}
```
(The `~FA_SUBDIR` mask is removed from `ixcreat`'s contract — the core receives the masked attr — matching today where `xcreat` masked before `ixcreat`. Today's on adapter passed raw attr to `pfs_do_create` then `xcreat` re-masked; effective masked either way, so this is bit-for-bit.)

- [ ] **Step 5: Convert `ixcreat` to a shim too** (it's still called by `bdos`'s `xmkdir`/`xrename` until Tasks 5/11):
```c
long ixcreat(char *name, char attr)
{
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_create(name, (UWORD)(UBYTE)attr);
#else
    return fat_creat_path(name, attr);
#endif
}
```
In on mode today, `xmkdir`'s `ixcreat(s, FA_SUBDIR)` went `ixcreat`→... wait, today `ixcreat` is the real body and the on adapter for create calls `xcreat`→`ixcreat`. `xmkdir` calls `ixcreat` directly, which today always runs the raw FAT body even in on mode (the osif block doesn't intercept `xmkdir`'s internal `ixcreat` call — that's a C call, not a GEMDOS trap). After this task, `ixcreat` in on mode routes through `pfs_do_create` → vtable `fat_create` core. That changes the on-mode internal-call behaviour of `xmkdir`/`xrename` to go through the vtable — which is exactly the design intent (the spec's "Internal cross-calls become direct calls between fatfs_pfs.c's own functions" is realised in Tasks 5/11, and until then routing through the vtable is the correct, equivalent path). Off mode: `ixcreat`→`fat_creat_path`→`fat_create` core, byte-for-byte the old `ixcreat` body. **Do NOT delete the `ixcreat` prototype from `fs.h` yet** (Tasks 5/11 still need it; deleted in Task 13).

- [ ] **Step 6: Build both, gitready, commit.**
```sh
make atari512_defconfig && make && make virt-arm_defconfig && make && make gitready
git add fs/fatfs_pfs.c bdos/fsopnclo.c
git commit -m "fs: move Fcreate (0x3C) into fatfs_pfs core; ixcreat becomes shim (#174)"
```

---

## Task 5: Dcreate (opcode 0x39, `xmkdir` → `pfs_do_mkdir` / `fat_mkdir_path`)

**Files:**
- Create core in: `fs/fatfs_pfs.c` plus the `namlen`/`dots` helpers (moved from `bdos/fsdir.c`).
- Modify: `bdos/fsdir.c` (delete `namlen`/`dots` statics; convert `xmkdir` to shim).

**Interfaces:**
- Consumes: `fat_create` (Task 4), `getofd`, `ixlseek`, `ixread`, `makdnd`, `makofd`, `nextcl`, `dirinit`, `ixclose`, `xmfreblk`, `freednd`, `memcpy`, `FA_SUBDIR`, `CL_DIR`, `CL_FULL`, `LEN_ZPATH`.
- Produces: `fat_mkdir(dir,name)` vtable; `fat_mkdir_path(s)` off wrapper.

- [ ] **Step 1: Move `namlen` and `dots` from `bdos/fsdir.c:176-191` and `:165` to `fs/fatfs_pfs.c`** as `static` (they are used only by the `fat_mkdir` core). In `fsdir.c` delete the `static int namlen(...)` prototype (line 141), the definition (176-191), and the `static const char dots[22] = ".          ";` (line 165).

- [ ] **Step 2: Add the `fat_mkdir` core** = the `xmkdir` body at `bdos/fsdir.c:199-283` with `ixcreat(s, FA_SUBDIR)` replaced by `fat_create(dir, name, FA_SUBDIR, &newfc)` and `h` taken from `newfc.index`. Parameter: `PFSCOOKIE *dir, const char *name`. Skeleton:
```c
static LONG fat_mkdir(PFSCOOKIE *dir, const char *name)
{
    PFSCOOKIE newfc;
    OFD *f;
    FCB *f2;
    OFD *fd, *f0;
    DFD *dfd;
    FCB *b;
    DND *dn;
    int h, cl, plen;
    long rc;

    rc = fat_create(dir, name, FA_SUBDIR, &newfc);
    if (rc < 0)
        return rc;
    h = (int)newfc.index;
    f = getofd(h);
    fd = f->o_dirfil;
    ixlseek(fd, f->o_dirbyt);
    b = (FCB *)ixread(fd, 32L, NULL);

    plen = namlen(b->f_name);
    for (dn = f->o_dnode; dn; dn = dn->d_parent)
        plen += namlen(dn->d_name);
    if (plen >= (LEN_ZPATH - 3))
    {
        ixdel(f->o_dnode, b, f->o_dirbyt);
        return EACCDN;
    }

    dn = makdnd(f->o_dnode, b);
    f0 = makofd(dn);
    if (nextcl(f0, 1))
    {
        ixdel(f->o_dnode, b, f->o_dirbyt);
        f->o_dnode->d_left = NULL;
        freednd(dn);
        return EACCDN;
    }
    f2 = dirinit(dn);
    /* …the rest of xmkdir's body verbatim from fsdir.c:247-282
     *  (memcpy(f2, dots, 22); f2->f_attrib = FA_SUBDIR; … parent-entry
     *  dance … memcpy(f, f0, sizeof(OFD)); ixclose(f, CL_DIR|CL_FULL);
     *  xmfreblk(f); sft[h-NUMSTD].f_own = 0; sft[h-NUMSTD].f_ofd = 0;
     *  return E_OK;) */
}
```
The body from `fsdir.c:247` onward (the two `memcpy(f2, dots, 22)` blocks, the root-vs-non-root `d_parent` branching, the `memcpy(f, f0, sizeof(OFD))`, `ixclose(f, CL_DIR | CL_FULL)`, `xmfreblk(f)`, the `sft[h-NUMSTD]` clearing) relocates verbatim. Replace the gated old `fat_mkdir` adapter; vtable `.mkdir` already names `fat_mkdir`.

- [ ] **Step 3: Add the off wrapper** under `#if !CONF_WITH_PLUGGABLE_FS`:
```c
static LONG fat_mkdir_path(char *s)
{
    DND *dn;
    const char *sp;
    PFSCOOKIE dir;

    if ((long)(dn = findit(s, &sp, 0)) < 0)
        return (long)dn;
    if (!dn)
        return EPTHNF;
    dir.fs = NULL;
    dir.index = (LONG)dn;
    dir.aux = 0;
    dir.pos = 0;
    return fat_mkdir(&dir, sp);
}
```

- [ ] **Step 4: Convert `xmkdir` in `bdos/fsdir.c` to a shim** (delete the old body):
```c
long xmkdir(char *s)
{
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_mkdir(s);
#else
    return fat_mkdir_path(s);
#endif
}
```

- [ ] **Step 5: Build both, gitready, commit.**
```sh
make atari512_defconfig && make && make virt-arm_defconfig && make && make gitready
git add fs/fatfs_pfs.c bdos/fsdir.c
git commit -m "fs: move Dcreate (0x39) into fatfs_pfs core; xmkdir becomes shim (#174)"
```

---

## Task 6: Fdelete (opcode 0x41, `xunlink` → `pfs_do_unlink` / `fat_unlink_path`)

**Files:**
- Create core in: `fs/fatfs_pfs.c`.
- Modify: `bdos/fsopnclo.c` (convert `xunlink` to shim).

**Interfaces:**
- Consumes: `scan`, `ixdel`, `FA_NORM`, `FA_RO`, `EFILNF`, `EACCDN`.
- Produces: `fat_remove(dir,name)` vtable; `fat_unlink_path(name)` off wrapper.

- [ ] **Step 1: Add the `fat_remove` core** = the `xunlink` body at `fsopnclo.c:603-629` with `findit` removed:
```c
static LONG fat_remove(PFSCOOKIE *dir, const char *name)
{
    DND *dn = (DND *)dir->index;
    FCB *f;
    long pos;

    pos = 0;
    if (!(f = scan(dn, name, FA_NORM, &pos)))
        return EFILNF;
    if (f->f_attrib & FA_RO)
        return EACCDN;
    pos -= 32;
    return ixdel(dn, f, pos);
}
```
Replace the gated old `fat_remove` adapter; vtable `.remove` already names `fat_remove`.

- [ ] **Step 2: Add the off wrapper** under `#if !CONF_WITH_PLUGGABLE_FS`:
```c
static LONG fat_unlink_path(char *name)
{
    DND *dn;
    const char *s;
    PFSCOOKIE dir;

    if ((long)(dn = findit(name, &s, 0)) < 0)
        return (long)dn;
    if (!dn)
        return EFILNF;
    dir.fs = NULL;
    dir.index = (LONG)dn;
    dir.aux = 0;
    dir.pos = 0;
    return fat_remove(&dir, s);
}
```

- [ ] **Step 3: Convert `xunlink` in `bdos/fsopnclo.c` to a shim:**
```c
long xunlink(char *name)
{
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_unlink(name);
#else
    return fat_unlink_path(name);
#endif
}
```

- [ ] **Step 4: Build both, gitready, commit.**
```sh
make atari512_defconfig && make && make virt-arm_defconfig && make && make gitready
git add fs/fatfs_pfs.c bdos/fsopnclo.c
git commit -m "fs: move Fdelete (0x41) into fatfs_pfs core; xunlink becomes shim (#174)"
```

---

## Task 7: Fattrib (opcode 0x43, `xchmod` → `pfs_do_chmod` / `fat_chmod_path`)

**Files:**
- Create core in: `fs/fatfs_pfs.c`.
- Modify: `bdos/fsdir.c` (convert `xchmod` to shim).

**Interfaces:**
- Consumes: `scan`, `ixlseek`, `ixread`, `ixwrite`, `ixclose`, `FA_NORM`, `CL_DIR`, `EFILNF`, `EACCDN`.
- Produces: `fat_chattr(dir,name,set,dos_attr)` vtable; `fat_chmod_path(p,wrt,mod)` off wrapper.

- [ ] **Step 1: Add the `fat_chattr` core** = the `xchmod` body at `bdos/fsdir.c:391-426` with `findit` removed, operating through `*dos_attr`:
```c
static LONG fat_chattr(PFSCOOKIE *dir, const char *name, BOOL set, UWORD *dos_attr)
{
    DND *dn = (DND *)dir->index;
    OFD *fd;
    char mod = (char)*dos_attr;
    long pos;

    if (!scan(dn, name, FA_NORM, &pos))
        return EFILNF;
    if (set && (mod & ~FA_NORM))
        return EACCDN;
    pos -= 21;
    fd = dn->d_ofd;
    ixlseek(fd, pos);
    if (!set)
        ixread(fd, 1L, &mod);
    else
    {
        ixwrite(fd, 1L, &mod);
        ixclose(fd, CL_DIR);
    }
    *dos_attr = (UWORD)(UBYTE)mod;
    return E_OK;
}
```
Replace the gated old `fat_chattr` adapter (which called `xchmod`); vtable `.chattr` already names `fat_chattr`.

- [ ] **Step 2: Add the off wrapper** under `#if !CONF_WITH_PLUGGABLE_FS`. It replicates `xchmod`'s `findit` + return-the-byte semantics exactly (`xchmod` returns `mod` as a `char`, sign-extended to `long` — reproduce that per-arch behaviour):
```c
static LONG fat_chmod_path(char *p, int wrt, char mod)
{
    DND *dn;
    const char *s;
    PFSCOOKIE dir;
    UWORD attr = (UWORD)(UBYTE)mod;
    LONG rc;

    if ((long)(dn = findit(p, &s, 0)) < 0)
        return (long)dn;
    if (!dn)
        return EPTHNF;
    dir.fs = NULL;
    dir.index = (LONG)dn;
    dir.aux = 0;
    dir.pos = 0;
    rc = fat_chattr(&dir, s, wrt ? TRUE : FALSE, &attr);
    if (rc < 0)
        return rc;
    return (long)(char)attr;
}
```
(`(long)(char)attr` reproduces `xchmod`'s `return mod;` exactly — on m68k (`char` signed) it sign-extends, on ARM (default unsigned `char`) it zero-extends, matching today's per-arch off behaviour. On-mode `pfs_do_chmod` returns `attr` as `LONG` unchanged from today, so the shim's on branch reproduces today's on behaviour.)

- [ ] **Step 3: Convert `xchmod` in `bdos/fsdir.c` to a shim:**
```c
long xchmod(char *p, int wrt, char mod)
{
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_chmod(p, wrt, mod);
#else
    return fat_chmod_path(p, wrt, mod);
#endif
}
```
(`pfs_do_chmod(p, WORD wrt, WORD mod)` receives `mod` promoted to `WORD`; today's on path got `pw[4]` WORD too — identical.)

- [ ] **Step 4: Build both, gitready, commit.**
```sh
make atari512_defconfig && make && make virt-arm_defconfig && make && make gitready
git add fs/fatfs_pfs.c bdos/fsdir.c
git commit -m "fs: move Fattrib (0x43) into fatfs_pfs core; xchmod becomes shim (#174)"
```

---

## Task 8: Ddelete (opcode 0x3A, `xrmdir` → `pfs_do_rmdir` / `fat_rmdir_path`)

**Files:**
- Create core in: `fs/fatfs_pfs.c`.
- Modify: `bdos/fsdir.c` (convert `xrmdir` to shim).

**Interfaces:**
- Consumes: `scan`, `getdnd`, `makofd`, `ixlseek`, `ixread`, `ixdel`, `xmfreblk`, `FA_SUBDIR`, `FA_LFN`, `ERASE_MARKER`, `EACCDN`, `EPTHNF`, `EINTRN`.
- Produces: `fat_rmdir(dir,name)` vtable; `fat_rmdir_path(p)` off wrapper.

**Note on the target-dir resolution:** `xrmdir`'s `findit(p, &s, 1)` (dflag=1) resolves the **target directory itself** as a DND. The vtable hands the core the **containing directory** + `name`. The core resolves the child DND via `scan(parent, name, FA_SUBDIR, &pos)` (which logs the child DND in via `makdnd` as a side effect, exactly as `findit(dflag=1)`'s `dirscan` would) and `getdnd(&f->f_name[0], parent)`. `pfs_split` guarantees `name` has no slash, so this single-level lookup is the right primitive.

- [ ] **Step 1: Add the `fat_rmdir` core** = the `xrmdir` body at `bdos/fsdir.c:296-378` with `findit` replaced by `scan`+`getdnd`:
```c
static LONG fat_rmdir(PFSCOOKIE *dir, const char *name)
{
    DND *parent = (DND *)dir->index;
    DND *d, *d1, **q;
    FCB *f;
    OFD *fd, *f2;
    long pos;

    if (!*name || (*name == '.'))
        return EPTHNF;          /* Ddelete(".") / ".." / "" - xrmdir's
                                 * findit(left these as cwd/root) doesn't
                                 * reach the body; absent a real child
                                 * scan they fail the lookup below anyway,
                                 * but short-circuit to EPTHNF to match
                                 * the "no such path" outcome. */

    pos = 0;
    f = scan(parent, name, FA_SUBDIR, &pos);
    if (!f)
        return EPTHNF;
    d = getdnd(&f->f_name[0], parent);
    if (!d)
        return EPTHNF;          /* scan() just logged it in: can't happen */

    if (!d->d_parent)           /* can't delete root */
        return EACCDN;

    if (!(fd = d->d_ofd))
        fd = makofd(d);

    ixlseek(fd, 0x40L);         /* skip . and .. */
    do
    {
        if (!(f = (FCB *)ixread(fd, 32L, NULL)))
            break;
    } while ((f->f_name[0] == (char)ERASE_MARKER) || (f->f_attrib == FA_LFN));

    if ((f != (FCB *)NULL) && (f->f_name[0] != 0x00))
        return EACCDN;

    for (d1 = *(q = &d->d_parent->d_left); d1 != d; d1 = *(q = &d1->d_right))
        ;

    if (d->d_files)
        return EINTRN;
    if (d->d_left)
        return EINTRN;

    *q = d->d_right;

    if (d->d_ofd)
        xmfreblk(d->d_ofd);

    d1 = d->d_parent;
    xmfreblk(d);

    ixlseek((f2 = fd->o_dirfil), (pos = fd->o_dirbyt));
    f = (FCB *)ixread(f2, 32L, NULL);

    return ixdel(d1, f, pos);
}
```
Replace the gated old `fat_rmdir` adapter; vtable `.rmdir` already names `fat_rmdir`.

- [ ] **Step 2: Add the off wrapper** under `#if !CONF_WITH_PLUGGABLE_FS`. It uses `findit(p, &s, 1)` (dflag=1, exactly like `xrmdir` today) and resolves the child via the same core by passing the **parent** + `s`:
```c
static LONG fat_rmdir_path(char *p)
{
    DND *dn;
    const char *s;
    PFSCOOKIE dir;

    if ((long)(dn = findit(p, &s, 1)) < 0)
        return (long)dn;
    if (!dn)
        return EPTHNF;
    /* findit(dflag=1) resolves the whole path *incl* the final dir, so
     * 'dn' is the target dir; reconstruct (parent, name) for the core.
     * If 's' is empty (path was "\" or "A:\"), 'dn' is the root - the
     * core's "!d_parent" check below returns EACCDN, matching xrmdir's
     * root rejection, so feed it back as a (root, "") lookup. */
    if (!*s)
    {
        if (!dn->d_parent)
            return EACCDN;
        /* "A:\SUB" with dflag=1 returns dn=SUB, s="" - shouldn't happen
         * since getpath would have consumed SUB; treat as not-found. */
        return EPTHNF;
    }
    {
        DND *parent = dn->d_parent;
        PFSCOOKIE pdir;
        if (!parent)
            return EACCDN;       /* deleting root */
        pdir.fs = NULL;
        pdir.index = (LONG)parent;
        pdir.aux = 0;
        pdir.pos = 0;
        return fat_rmdir(&pdir, s);
    }
}
```
(The off wrapper prefers `findit(dflag=1)` for byte-for-byte parity with today's `xrmdir` path resolution — including its multi-component-final-dir handling — and then hands the core the (parent, final-name) it extracted. The on path uses `pfs_split`'s single final component, which the core resolves via `scan`.)

- [ ] **Step 3: Convert `xrmdir` in `bdos/fsdir.c` to a shim:**
```c
long xrmdir(char *p)
{
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_rmdir(p);
#else
    return fat_rmdir_path(p);
#endif
}
```

- [ ] **Step 4: Build both, gitready, commit.**
```sh
make atari512_defconfig && make && make virt-arm_defconfig && make && make gitready
git add fs/fatfs_pfs.c bdos/fsdir.c
git commit -m "fs: move Ddelete (0x3A) into fatfs_pfs core; xrmdir becomes shim (#174)"
```

---

## Task 9: Dsetpath (opcode 0x3B, `xchdir` → `pfs_do_chdir` / `fat_chdir_path`)

**Files:**
- Create wrapper in: `fs/fatfs_pfs.c` (no vtable core — on mode `pfs_do_chdir` maintains `pfs_dirtbl[]` itself; off mode is the verbatim `xchdir` body).

**Interfaces:**
- Consumes: `findit`, `incr_curdir_usage`, `decr_curdir_usage`, `contains_wildcard_characters` (moves here), `EPTHNF`, `toupper`.
- Produces: `fat_chdir_path(p)` off wrapper only.

- [ ] **Step 1: Move `contains_wildcard_characters`** from `bdos/fsdir.c:496-505` to `fs/fatfs_pfs.c` as `static` (its only two users are `xchdir` (moving) and `xsfirst` (moving in Task 11)). In `fsdir.c` delete the `static BOOL contains_wildcard_characters(...)` prototype (line 149-ish) and definition (496-505).

- [ ] **Step 2: Add the off wrapper** = the `xchdir` body at `bdos/fsdir.c:1101-1146` verbatim, renamed `fat_chdir_path`, under `#if !CONF_WITH_PLUGGABLE_FS`:
```c
static LONG fat_chdir_path(char *p)
{
    DND *dnd;
    long rc;
    int olddir, newdir, dlog;
    const char *s;

    if (contains_wildcard_characters(p))
        return EPTHNF;

    if (p[1] == ':')
        dlog = toupper(p[0]) - 'A';
    else
        dlog = run->p_curdrv;

    olddir = run->p_curdir[dlog];

    rc = (long)(dnd = findit(p, &s, 1));
    if (rc < 0L)
        return rc;
    if (!dnd)
        return EPTHNF;

    newdir = incr_curdir_usage(dnd);
    if (newdir < 0)
        return EPTHNF;
    run->p_curdir[dlog] = newdir;

    if (olddir)
        decr_curdir_usage(olddir);

    return E_OK;
}
```

- [ ] **Step 3: Convert `xchdir` in `bdos/fsdir.c` to a shim** (no off-path core, no vtable entry — on mode `pfs_do_chdir` is the whole implementation):
```c
long xchdir(char *p)
{
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_chdir(p);
#else
    return fat_chdir_path(p);
#endif
}
```

- [ ] **Step 4: Build both, gitready, commit.**
```sh
make atari512_defconfig && make && make virt-arm_defconfig && make && make gitready
git add fs/fatfs_pfs.c bdos/fsdir.c
git commit -m "fs: move Dsetpath (0x3B) into fatfs_pfs off wrapper; xchdir becomes shim (#174)"
```

---

## Task 10: Dgetpath (opcode 0x47, `xgetdir` → `pfs_do_getdir` / `fat_getdir_path`)

**Files:**
- Create wrapper in: `fs/fatfs_pfs.c` (no vtable core — on mode `pfs_do_getdir` reads `pfs_dirtbl[]` itself).

**Interfaces:**
- Consumes: `Drvmap`, `ckdrv`, `dopath`, `dirtbl`, `EDRIVE`, `LEN_ZPATH`.
- Produces: `fat_getdir_path(buf,drv)` off wrapper only.

- [ ] **Step 1: Add the off wrapper** = the `xgetdir` body at `bdos/fsdir.c:1221-1242` verbatim, renamed `fat_getdir_path`, under `#if !CONF_WITH_PLUGGABLE_FS`:
```c
static LONG fat_getdir_path(char *buf, int drv)
{
    DND *p;
    int n;
    int len;

    drv = (drv == 0) ? run->p_curdrv : drv - 1;

    if (!(Drvmap() & (1L << drv)) || (ckdrv(drv, FALSE) < 0))
    {
        *buf = 0;
        return EDRIVE;
    }

    n = run->p_curdir[drv];
    p = dirtbl[n].dnd;
    len = LEN_ZPATH - 3;
    buf = dopath(p, buf, &len);
    *--buf = 0;

    return E_OK;
}
```
(`dirtbl` is the legacy `DIRTBL_ENTRY dirtbl[]` global — already visible via `fs.h`; `Drvmap` and `ckdrv` are public. `dopath` is in `fs_internal.h`.)

- [ ] **Step 2: Convert `xgetdir` in `bdos/fsdir.c` to a shim:**
```c
long xgetdir(char *buf, int drv)
{
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_getdir(buf, drv);
#else
    return fat_getdir_path(buf, drv);
#endif
}
```

- [ ] **Step 3: Build both, gitready, commit.**
```sh
make atari512_defconfig && make && make virt-arm_defconfig && make && make gitready
git add fs/fatfs_pfs.c bdos/fsdir.c
git commit -m "fs: move Dgetpath (0x47) into fatfs_pfs off wrapper; xgetdir becomes shim (#174)"
```

---

## Task 11: Fsfirst/Fsnext (opcodes 0x4E/0x4F, `xsfirst`/`xsnext` → `pfs_do_sfirst`/`pfs_do_snext` / `fat_sfirst_path`/`fat_snext_path`)

**Files:**
- Create wrappers in: `fs/fatfs_pfs.c` (no vtable core — on mode uses `fat_readdir` + `pfs_searches[]` via `pfs_do_sfirst`/`pfs_do_snext`, unchanged).

**Interfaces:**
- Consumes: `ixsfirst`, `ixsnext`, `makbuf`, `contains_wildcard_characters` (moved in Task 9), `DTAINFO`, `run->p_xdta`, `EFILNF`, `ENMFIL`.
- Produces: `fat_sfirst_path(name,att)` and `fat_snext_path()` off wrappers.

- [ ] **Step 1: Add the `fat_sfirst_path` off wrapper** = the `xsfirst` body at `bdos/fsdir.c:514-530` verbatim, under `#if !CONF_WITH_PLUGGABLE_FS`:
```c
static LONG fat_sfirst_path(char *name, int att)
{
    long result;
    DTAINFO *dt;

    dt = (DTAINFO *)(run->p_xdta);
    dt->dt_offset_drive = -1L;
    result = ixsfirst(name, att, dt);
    if ((result < 0) || !contains_wildcard_characters(name))
        return result;
    return E_OK;
}
```

- [ ] **Step 2: Add the `fat_snext_path` off wrapper** = the `xsnext` body at `bdos/fsdir.c:645-667` verbatim, under `#if !CONF_WITH_PLUGGABLE_FS`:
```c
static LONG fat_snext_path(void)
{
    FCB *f;
    DTAINFO *dt;

    dt = (DTAINFO *)run->p_xdta;
    if (dt->dt_offset_drive < 0L)
        return ENMFIL;
    f = ixsnext(dt);
    if (f == NULL)
    {
        dt->dt_offset_drive = -1L;
        return ENMFIL;
    }
    makbuf(f, (DTAINFO *)run->p_xdta);
    return E_OK;
}
```

- [ ] **Step 3: Convert `xsfirst` and `xsnext` in `bdos/fsdir.c` to shims** (delete the old bodies; `ixsfirst`/`ixsnext` stay):
```c
long xsfirst(char *name, int att)
{
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_sfirst(name, att);
#else
    return fat_sfirst_path(name, att);
#endif
}

long xsnext(void)
{
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_snext();
#else
    return fat_snext_path();
#endif
}
```

- [ ] **Step 4: Build both, gitready, commit.**
```sh
make atari512_defconfig && make && make virt-arm_defconfig && make && make gitready
git add fs/fatfs_pfs.c bdos/fsdir.c
git commit -m "fs: move Fsfirst/Fsnext (0x4E/0x4F) off wrappers; xsfirst/xsnext become shims (#174)"
```

---

## Task 12: Frename (opcode 0x56, `xrename` → `pfs_do_rename` / `fat_rename_path`)

**Files:**
- Create core in: `fs/fatfs_pfs.c` plus the `update_fcb`/`is_subdir`/`unpackit` helpers (moved from `bdos/fsdir.c`).
- Modify: `bdos/fsdir.c` (delete `update_fcb`/`is_subdir`/`unpackit` statics; convert `xrename` to shim).

**Interfaces:**
- Consumes: `ixsfirst` (existence check), `fat_create` (Task 4), `getofd`, `xclose`, `ixclose`, `ixlseek`, `ixread`, `ixwrite`, `builds`, `getdnd`/`freednd`/`packit` (Task 1), `update_fcb`/`is_subdir`/`unpackit` (moved here), `xmfreblk`, `contains_illegal_characters`, `DND_LOCKED`, `ROOT_PSEUDO_CLUSTER`, `EACCDN`, `ENSAME`, `EPTHNF`, `EFILNF`, `EINTRN`.
- Produces: `fat_rename(olddir,oldname,newdir,newname)` vtable; `fat_rename_path(p1,p2)` off wrapper.

- [ ] **Step 1: Move `update_fcb`, `is_subdir`, `unpackit`** from `bdos/fsdir.c:862-870`, `:844-854`, `:1326-1349` to `fs/fatfs_pfs.c` as `static`. In `fsdir.c` delete their forward prototypes (lines 143, 153) and definitions. (`update_fcb` uses `ixlseek`/`ixwrite`; `is_subdir` uses `packit`/`strncasecmp`; `unpackit` uses `memset`. All public.)

- [ ] **Step 2: Add the `fat_rename` core** = the `xrename` body at `bdos/fsdir.c:886-1089` with these transforms:
  - `ixsfirst(p2, FA_SUBDIR, (DTAINFO *)0L)` stays (it's the existence-check role, which stays in `bdos`).
  - `findit(p1, &s1, 0)` is removed — the core receives `olddir` cookie + `oldname`. `DND *dn1 = (DND *)olddir->index; const char *s1 = oldname;`
  - `findit(p2, &s2, 0)` is removed — `DND *dn2 = (DND *)newdir->index; const char *s2 = newname;`
  - `ixcreat(p2, att)` (line 1003) → `fat_create(newdir, newname, (UWORD)(UBYTE)att, &newfc); hnew = (int)newfc.index;` Keep the `dn1->d_flag |= DND_LOCKED; dn2->d_flag |= DND_LOCKED; … &= ~DND_LOCKED;` dance around it (preserves the scavenge protection exactly as `xrename` does today).
  - `xclose(hnew)` (line 1060) stays — `xclose` is the real Fclose (out of scope), and `hnew`'s sft slot has `f_pfs.fs == 0` so `xclose`'s pluggable branch is not taken.
  - Parameter: `PFSCOOKIE *olddir, const char *oldname, PFSCOOKIE *newdir, const char *newname`. Everything else (the `dmd1`/`strtcl1`/`strtcl2` cross-device logic, the `is_subdir`/cross-directory branch, the `update_fcb` calls, the `if (att & FA_SUBDIR)` DND cleanup with `unpackit`/`getdnd`/`freednd`, the final `ixclose(fd, CL_DIR)`) relocates verbatim.
  Replace the gated old `fat_rename` adapter; vtable `.rename` already names `fat_rename`.

- [ ] **Step 3: Add the off wrapper** under `#if !CONF_WITH_PLUGGABLE_FS`. It does the two `findit`s (exactly as `xrename` did), builds both cookies, and calls the core:
```c
static LONG fat_rename_path(char *p1, char *p2)
{
    DND *dn1, *dn2;
    const char *s1, *s2;
    PFSCOOKIE old, new;

    if ((long)(dn1 = findit(p1, &s1, 0)) < 0)
        return (long)dn1;
    if (!dn1)
        return EPTHNF;
    if ((long)(dn2 = findit(p2, &s2, 0)) < 0)
        return (long)dn2;
    if (!dn2)
        return EPTHNF;
    old.fs = NULL;   old.index = (LONG)dn1;  old.aux = 0;  old.pos = 0;
    new.fs = NULL;   new.index = (LONG)dn2;  new.aux = 0;  new.pos = 0;
    return fat_rename(&old, s1, &new, s2);
}
```
Note: `xrename(int n, char *p1, char *p2)`'s `n` is unused (`/*ARGSUSED*/`); the wrapper takes `(p1, p2)` and the shim drops `n`.

- [ ] **Step 4: Convert `xrename` in `bdos/fsdir.c` to a shim:**
```c
long xrename(int n, char *p1, char *p2)
{
    (void)n;
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_rename(p1, p2);
#else
    return fat_rename_path(p1, p2);
#endif
}
```

- [ ] **Step 5: Build both, gitready, commit.**
```sh
make atari512_defconfig && make && make virt-arm_defconfig && make && make gitready
git add fs/fatfs_pfs.c bdos/fsdir.c
git commit -m "fs: move Frename (0x56) into fatfs_pfs core; xrename becomes shim (#174)"
```

---

## Task 13: Delete the osif() early-return, `pfs_dispatch`, `pfs_is_fs_call`, and the now-unused `ixcreat` shim

**Files:**
- Modify: `bdos/bdosmain.c` (delete the `#if CONF_WITH_PLUGGABLE_FS` `osif()` block ~lines 674-692), `fs/pfs.c` (delete `pfs_is_fs_call`, `pfs_dispatch`, `FN_*` defines), `fs/pfs.h` (delete `pfs_is_fs_call`/`pfs_dispatch` declarations + their doc comment; update header), `bdos/fsopnclo.c` + `bdos/fs.h` (delete `ixcreat` shim + prototype), `fs/fatfs_pfs.c` (remove the now-empty `#if CONF_WITH_PLUGGABLE_FS` wrapper if the last ON-only static inside it is gone — verify; the `fat_root`/`fat_lookup`/`fat_readdir`/pool/`fat_release`/`fat_abspath`/vtable stay gated ON).

**Interfaces:**
- Consumes: all 12 moved cores + shims from Tasks 2-12.
- Produces: `osif()` always dispatches via `funcs[]`; `ixcreat` gone.

- [ ] **Step 1: Delete the `osif()` early-return block** in `bdos/bdosmain.c` (the `#if CONF_WITH_PLUGGABLE_FS … if (!rc && pfs_is_fs_call(fn)) return pfs_dispatch(fn, pw); … #endif` at ~674-692). The on path now reaches the shims via `funcs[]` exactly like the off path — the shims' `#if CONF_WITH_PLUGGABLE_FS` branches are now live. `bdosmain.c`'s `#include "pfs.h"` becomes unused (only `pfs_is_fs_call`/`pfs_dispatch` were needed) — delete the `#if CONF_WITH_PLUGGABLE_FS / #include "pfs.h" / #endif` too.

- [ ] **Step 2: Delete `pfs_is_fs_call`, `pfs_dispatch`, and the `FN_*` defines** from `fs/pfs.c` (lines 41-61 and 1174-1240, plus the `FN_*` block 28-39). In `fs/pfs.h` delete the `BOOL pfs_is_fs_call(WORD fn);` declaration, the `#ifdef __arm__ LONG pfs_dispatch(...); #else ... #endif` block, and the big doc comment above them that describes `osif()`'s dispatch hook (pfs.h:182-207). Update the file's header comment (pfs.h:1-13) to remove the "dispatched through this interface from bdos/bdosmain.c's osif()" line and point at the design spec for the new shape.

- [ ] **Step 3: Delete `ixcreat`.** After Tasks 5 and 12 nothing calls `ixcreat` (its last two users were `xmkdir`/`xrename`, both now shims that route through `pfs_do_create`/`fat_creat_path`). In `bdos/fsopnclo.c` delete the `ixcreat` shim (added in Task 4) and its `#include "fatfs.h"` is still needed by `xopen`/`xcreat`/`xunlink` shims — keep it. In `bdos/fs.h` delete `long ixcreat(char *name, char attr);` (line 413).

- [ ] **Step 4: Update `fs/Kconfig`** `CONF_WITH_PLUGGABLE_FS` help text: it now gates only the pluggable dispatch machinery (drive table, cookie resolution, cwd/search state, the on-mode `pfs_do_*`), not the FAT implementation (which always builds). Suggested help:
```
	  Routes GEMDOS filesystem calls (Fopen, Fread, Dsetpath, Fsfirst,
	  ...) through a pluggable interface so a driver can expose a whole
	  filesystem as a GEMDOS drive.  The built-in FAT filesystem keeps
	  working either way: when off, the GEMDOS shims call fatfs_pfs.c's
	  fat_*_path() entry points directly; when on, they call the pfs_do_*()
	  helpers here, which dispatch through a per-drive vtable (with the
	  FAT code wrapped as fat_pfs_ops).  Say n unless a driver needs this.
```

- [ ] **Step 5: Update `fs/fatfs_pfs.c` and `fs/pfs.h` file header comments** per the spec's Documentation section: fatfs_pfs.c's header no longer says "calls back into bdos/" (it now holds the FAT implementation); pfs.h's header no longer mentions the osif dispatch hook.

- [ ] **Step 6: Build both, gitready, commit.**
```sh
make atari512_defconfig && make && make virt-arm_defconfig && make && make gitready
git add bdos/bdosmain.c fs/pfs.c fs/pfs.h bdos/fsopnclo.c bdos/fs.h fs/Kconfig fs/fatfs_pfs.c
git commit -m "fs: delete osif() pfs early-return, pfs_dispatch, pfs_is_fs_call, ixcreat (#174)"
```
After this commit the inversion is structurally complete: `osif()` always uses `funcs[]`; the on path lives in the shims + `pfs.c`; the off path lives in the shims + `fatfs_pfs.c`.

---

## Task 14: Compile-check every target

**Files:** none (verification only).

- [ ] **Step 1: Build every smoke-test config off** (the majority case) to confirm the always-on path compiles everywhere:
```sh
for c in atari512 ste falcon tt raspi1 raspi2 virt-arm virt-m68k; do
  make ${c}_defconfig && make || { echo "FAIL $c"; exit 1; }
done
```
- [ ] **Step 2: Compile-check the remaining off configs** for coverage of arch/machine variants:
```sh
for c in atari192 atari256 aranym amiga firebee m548x cartridge floppy atari512-dispatch; do
  make ${c}_defconfig && make || { echo "FAIL $c"; exit 1; }
done
```
(If a config name doesn't exist, skip it — run `make help` to list `configs/`.) Any failure here is a moved-core reference to a bdos internal that wasn't exported (Task 1) — add the prototype to `bdos/fs.h` and re-export (re-run `make gitready`).

- [ ] **Step 3: Build the on configs** (`virt-arm`, `virt-m68k`) — already done above, but confirm `pfs_test.o` links and `pfs.o` does.

- [ ] **Step 4: `make gitready`** on a representative config.

---

## Task 15: Documentation

**Files:**
- Modify: `doc/status.txt` (if it mentions the old dispatch order), `fs/build.mk` header (Task 1 already rewrote it — verify), `fs/pfs.h`/`fs/fatfs_pfs.c` headers (Task 13 already updated — verify).

- [ ] **Step 1: Search `doc/status.txt`** for "pluggable" / "pfs" / "dispatch" and update any line that describes the pre-inversion `osif()`-intercepts-first wiring to reflect the new "shims dispatch; osif always uses funcs[]" shape.

- [ ] **Step 2: Verify the `fs/build.mk` header** (rewritten in Task 1) and the `fs/pfs.h`/`fs/fatfs_pfs.c` headers (Task 13) accurately describe the new shape and point at the design spec.

- [ ] **Step 3: Commit.**
```sh
git add doc/status.txt fs/build.mk fs/pfs.h fs/fatfs_pfs.c
git commit -m "docs: update pluggable-fs wiring description for the inversion (#174)"
```

---

## Task 16: Functional boot verification (the real equivalence gate)

**Files:** none. Use the `ptos-smoketest` skill for the verified emulator invocations.

- [ ] **Step 1: Off-path smoke (bit-for-bit).** For each off smoke config — `atari512`, `ste`, `falcon` (Hatari, 31 s IDE boot wait), `tt`, `raspi1` (QEMU `raspi1ap`), `raspi2` (QEMU `raspi2b`), `virt-arm`, `virt-m68k` — boot to the GEM desktop and exercise FAT through GEMDOS: open/read/write/create/rename/delete files, mkdir/rmdir, Fsfirst/Fsnext via the desktop file selector, Dfree via the info dialog. Behaviour must be indistinguishable from `master`.

- [ ] **Step 2: On-path smoke.** Boot `virt-arm` and `virt-m68k` (now with `CONF_WITH_PLUGGABLE_FS=y` + `CONF_WITH_PLUGGABLE_FS_TEST=y`):
  1. The relocated FAT driver still serves drives A:/etc. through the vtable — repeat the FAT exercises from Step 1.
  2. The registered foreign `pfs_test` drive (P:) still appears and `P:\PFSTEST.TXT` still reads its one-line content — the "driver behind the layer" story survives the move.

- [ ] **Step 3: Mark the PR ready.** `gh pr ready` on PR #176. Post a comment summarising the verification matrix (which configs booted, off + on).

---

## Self-Review notes

Run before handing off:

1. **Spec coverage:** every opcode in the spec's mapping table (Dfree/Fopen/Fcreate/Dcreate/Fdelete/Fattrib/Ddelete/Dsetpath/Dgetpath/Fsfirst/Fsnext/Frename) is implemented by Tasks 2-12 ✓. The build system + config changes (spec "Build System" + "Configuration Changes") are Task 1 ✓. The osif/pfs_dispatch/pfs_is_fs_call deletion + ixcreat deletion is Task 13 ✓. Documentation + verification (spec "Documentation" + "Behaviour And Verification") are Tasks 15-16 ✓. Fsfirst/Fsnext's two-implementation split (spec §Fsfirst/Fsnext) is Task 11 ✓. Non-funcs[] callers (kpgmld `xopen`, proc.c `ixsfirst` existence check) — `kpgmld`'s `xopen` goes through the shim in both modes (Task 3) ✓; `proc.c`'s `ixsfirst` is untouched (Task 11 keeps `ixsfirst` in bdos) ✓.

2. **Placeholder scan:** each task's steps contain the actual code for shims, wrappers, and the cores that need adaptation; bodies that are verbatim relocations give the exact source line range and the precise set of edits (findit removal, internal-call rename, cookie extraction). No "TBD"/"implement later"/"similar to Task N".

3. **Type consistency:** vtable core signatures match `struct pfs_ops` in `fs/pfs.h` (`root`, `lookup`, `open`, `create`, `readdir`, `mkdir`, `rmdir`, `remove`, `rename`, `chattr`, `dfree`, `mediach`, `release`) ✓. Off-wrapper signatures match `fs/fatfs.h` ✓. `pfs_do_*` signatures match the prototypes added to `fs/pfs.h` in Task 1 ✓. `fat_countfree`/`fat_dfree`/`fat_open`/`fat_create`/`fat_remove`/`fat_chattr`/`fat_mkdir`/`fat_rmdir`/`fat_rename` are the names used consistently across their defining task and the vtable initializer ✓.

4. **Ordering hazards:** Task 4 (Fcreate core + `ixcreat` shim) precedes Task 5 (Dcreate, whose core calls `fat_create`) and Task 12 (Frename, whose core calls `fat_create`) ✓. Task 1 exports `opnfil`/`makdnd`/`getdnd`/`freednd`/`packit` before any task that needs them ✓. Task 9 moves `contains_wildcard_characters` before Task 11 (which also uses it) ✓. Task 12 moves `update_fcb`/`is_subdir`/`unpackit` in the same task that uses them ✓. Task 13 (delete osif block) is last so the shims' on branches are dead-but-compiled (and therefore safe) during Tasks 2-12 ✓. The recursion hazard is handled by the atomic-per-opcode rule stated in "Recursion hazard" ✓.