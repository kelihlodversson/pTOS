# Invert Pluggable-FS Dispatch Design

## Context

Issue #174, following PR #159 (the pluggable filesystem layer). Today the
GEMDOS filesystem calls are routed to the pluggable layer **ahead of** the
real entry points, and the concrete FAT implementation lives in `bdos/` and is
*wrapped* by `fs/`. Both dependency directions are backwards:

- `osif()` (`bdos/bdosmain.c`) checks `pfs_is_fs_call(fn)` for 12 GEMDOS
  opcodes and calls `pfs_dispatch(fn, pw)` **before** `funcs[]` ever dispatches
  to the built-in `x*` function. `pfs_dispatch()` is a thin `pw[]`-argument
  unpacker over the well-typed `pfs_do_*()` helpers.
- The 12 built-in `x*` functions in `bdos/` (`xgetfree`, `xmkdir`, `xrmdir`,
  `xchdir`, `xcreat`/`ixcreat`, `xopen`/`ixopen`, `xunlink`, `xchmod`,
  `xgetdir`, `xsfirst`/`ixsfirst`, `xsnext`, `xrename`) contain the actual
  FAT12/16 implementation. They only run directly when
  `CONF_WITH_PLUGGABLE_FS` is off.
- `fs/fatfs_pfs.c`'s `fat_pfs_ops` vtable is a thin **adapter**, not an
  implementation: every entry reconstructs an absolute path string from a
  directory cookie (`fat_abspath()`) and calls straight back into the same
  `x*` functions. So with the option on, a path is resolved by `pfs.c`, then
  re-walked inside `xopen()`/`xmkdir()`/... via `fat_abspath`'s path round
  trip.
- `fs/build.mk` builds the whole `fs/` directory — `pfs.c` and
  `fatfs_pfs.c` — only when `CONF_WITH_PLUGGABLE_FS` is set (wired via
  `optional-dirs-$(CONF_WITH_PLUGGABLE_FS)` in the top-level `Makefile`). The
  option is off by default, so this is the majority case across configs.

`Fread`/`Fwrite`/`Fclose` are deliberately outside this change: they are
already made pluggable-aware directly in `xread()`/`xwrite()`/`xclose()` via
`pfs_handle_read/write/close()`, per `fs/pfs.h`'s own doc comment.

## Target Architecture

The dependency points one way: thin `bdos/` shims call into `fs/`, which holds
the concrete implementation.

- `osif()` goes back to always dispatching through `funcs[]`, unconditionally.
  Delete `pfs_is_fs_call()`, `pfs_dispatch()`, and the
  `CONF_WITH_PLUGGABLE_FS`-gated early-return block that calls them
  (`bdos/bdosmain.c` lines ~674-692).
- The 12 `x*` functions become thin GEMDOS-shaped entry points keeping their
  current names and signatures (so `funcs[]` and internal callers are
  untouched):
  - **on** (`CONF_WITH_PLUGGABLE_FS`): call the matching `pfs_do_*()` helper
    in `fs/pfs.c`, which does drive resolution, containing-directory cookie
    resolution, and vtable dispatch as today.
  - **off**: call a path-level wrapper in `fs/fatfs_pfs.c` directly — no
    `pfs_do_*()`, no vtable, no `pfs.c` at all, since there is only ever one
    filesystem. Same "call the single compiled-in implementation directly" 
    pattern as `CONF_WITH_VDI_BACKEND_DISPATCH` (vdi/vdi_backend.h).
- `fs/pfs.c` keeps only the pluggable machinery — `pfs_do_*`, the drive
  table, cookie resolution (`pfs_resolve_dir`, `pfs_cwd_get`), `pfs_searches[]`,
  cwd tracking (`pfs_dirtbl[]`), `pfs_handle_read/write/close`,
  `pfs_register_drive`, `pfs_proc_exit`, `pfs_cwd_addref` — and is compiled
  only when the option is on.
- `fs/fatfs_pfs.c` holds the real FAT implementation (the bodies of today's
  `x*` functions) and is compiled **unconditionally**. The `fat_abspath()`
  "reconstruct a path string and call back into bdos/" adapter shape goes
  away.

## The Moved FAT Implementation

The FAT core is naturally `(DND, name)`-shaped, not path-shaped. Today's
`ixopen`/`ixcreat` bodies call `findit()` first and then everything after
operates on the containing directory DND plus the final component
(`scan(dn, s, ...)`, `opnfil`, `makofd`, `ixdel`, `nextcl`, `dirinit`,
`builds`, the volume-label checks). So each `fat_pfs_ops` entry becomes the
real core operating directly on the cookie's DND, and the off-case wrapper is
`findit()` + a transient cookie + that same core. Each mode resolves the
containing directory exactly once.

Mapping of the 12 opcodes (`funcs[]` entry is unchanged and keeps its name):

| Opcode | bdos shim | on -> `pfs_do_*` | off -> fatfs_pfs.c wrapper |
| --- | --- | --- | --- |
| Dfree 0x36 | `xgetfree` | `pfs_do_dfree` | `fat_getfree_path` |
| Dcreate 0x39 | `xmkdir` | `pfs_do_mkdir` | `fat_mkdir_path` |
| Ddelete 0x3A | `xrmdir` | `pfs_do_rmdir` | `fat_rmdir_path` |
| Dsetpath 0x3B | `xchdir` | `pfs_do_chdir` | `fat_chdir_path` |
| Fcreate 0x3C | `xcreat` | `pfs_do_create` | `fat_creat_path` |
| Fopen 0x3D | `xopen` | `pfs_do_open` | `fat_open_path` |
| Fdelete 0x41 | `xunlink` | `pfs_do_unlink` | `fat_unlink_path` |
| Fattrib 0x43 | `xchmod` | `pfs_do_chmod` | `fat_chmod_path` |
| Dgetpath 0x47 | `xgetdir` | `pfs_do_getdir` | `fat_getdir_path` |
| Fsfirst 0x4E | `xsfirst` | `pfs_do_sfirst` | `fat_sfirst_path` |
| Fsnext 0x4F | `xsnext` | `pfs_do_snext` | `fat_snext_path` |
| Frename 0x56 | `xrename` | `pfs_do_rename` | `fat_rename_path` |

The vtable entries that have no off-path counterpart (`fat_root`, `fat_lookup`,
`fat_readdir`, `fat_release`, `fat_close`, `fat_read`, `fat_write`,
`fat_dfree`, `fat_mediach`) stay as vtable-only implementations. `fat_close`/
`fat_read`/`fat_write` are handle-based and belong to the out-of-scope
Fread/Fwrite/Fclose handling; they are not touched.

### Fsfirst/Fsnext

These do not map onto a `fat_pfs_ops` entry — the vtable has `readdir()`, and
the search state lives in different places in the two modes:

- **on**: `xsfirst`/`xsnext` call `pfs_do_sfirst`/`pfs_do_snext`, which use
  the existing pool-based `fat_readdir()` (fatfs_pfs.c's `fat_readdir_pool[]`)
  and `pfs.c`'s `pfs_searches[]` search state. Unchanged from today.
- **off**: `xsfirst`/`xsnext` call `fat_sfirst_path`/`fat_snext_path`, the
  verbatim-moved legacy bodies using the caller's process DTA and
  `ixsfirst`/`ixsnext`/`match` semantics, byte-identical to today. (In this
  mode `fat_readdir()` and its pool are not compiled.)

This keeps two search implementations, each exactly today's, rather than
unifying them; the off path (the majority) risks no divergence.

## Non-`funcs[]` Callers

These constrain the move and must keep working:

- `bdos/kpgmld.c:55` `xopen()` (Pexec program loader) — routes through the
  thin shim in both modes. With the option on, loading from a foreign-driver
  drive goes through `pfs_do_open`, which is correct.
- `bdos/proc.c:291` `ixsfirst(path, 0, NULL)` — the NULL-DTA **existence
  check** role (also used by `xrename`). This role stays a real bdos
  function; only `ixsfirst`'s *search* role moves into fatfs_pfs.c's readdir
  machinery. The existence-check helper keeps its current behavior in both
  modes.
- Internal cross-calls inside the moved code (`xmkdir -> ixcreat`,
  `xcreat -> ixcreat`, `xrename -> ixcreat`/`ixsfirst`, `xsfirst -> ixsfirst`,
  `xsnext -> ixsnext`) become direct calls between fatfs_pfs.c's own
  functions.

## Build System

- Top-level `Makefile` (~line 112): move `fs` from
  `optional-dirs-$(CONF_WITH_PLUGGABLE_FS)` into `core-dirs-y`, giving
  `core-dirs-y = bios bdos fs util`.
- `fs/build.mk`: `obj-y += fatfs_pfs.o` (unconditional);
  `obj-$(CONF_WITH_PLUGGABLE_FS) += pfs.o`;
  `obj-$(CONF_WITH_PLUGGABLE_FS_TEST) += pfs_test.o`. Rewrite the header
  comment that claims the whole directory is option-gated.
- `fs/Kconfig`: `CONF_WITH_PLUGGABLE_FS` now gates only the pluggable
  dispatch machinery, not the FAT wrapper; update its help text. No new
  dependencies are needed — `CONF_WITH_PLUGGABLE_FS_TEST`, `CONF_PFS_MAX_SEARCHES`
  and `CONF_PFS_MAX_CWD` already depend on it.
- More bdos internals need exposing to `fs/fatfs_pfs.c` via `bdos/fs.h` /
  `bdos/fs_internal.h`. Today only `dopath()`, `ixsnext()` and `makbuf()` are
  exposed there, but the FAT primitives the moved cores need — `findit()`,
  `scan()`, `makofd()`, `builds()`, `dirinit()`, `nextcl()`, `ixdel()`,
  `ixread()`, `ixwrite()`, `ixlseek()`, `ixclose()`, `getofd()` — are already
  declared in `bdos/fs.h`. The one gap is `opnfil()` (`bdos/fsopnclo.c`),
  currently `static`: drop `static` and declare it in `fs.h`. Add prototypes
  only — no logic changes. (`-Wmissing-prototypes` makes the declarations
  mandatory.)
- `fs/pfs.h` stays in `fs/`; `pfs.h` remains pluggable-only.
- Every `pfs_*` reference in bdos (`fsio.c`, `fsopnclo.c`, `proc.c`, the
  `osif()` block) is already under `#if CONF_WITH_PLUGGABLE_FS`, so the off
  build links clean once `pfs.o` is gone.

## Configuration Changes

Permanent test vehicles for the on path, and groundwork for the virtio-9p
PR (#157):

- `configs/virt-arm_defconfig`: add `CONF_WITH_PLUGGABLE_FS=y`,
  `CONF_WITH_PLUGGABLE_FS_TEST=y`.
- `configs/virt-m68k_defconfig`: add the same two lines.
- The `-cli` variants and every other config stay untouched (option off — the
  majority case).
- Regenerated with `make savedefconfig` per the repo's "Adding a
  configuration" workflow.

## Behaviour And Verification

This touches core FAT12/16 correctness for every machine config (the option
is off nearly everywhere by default), so behavioural equivalence must be
verified both ways. Use the ptos-smoketest skill's verified emulator
invocations: QEMU for `raspi1ap`/`raspi2b`/virt-arm/virt-m68k, Hatari for the
Atari m68k targets (note the Falcon IDE 31 s boot wait).

**off path** (majority): build + boot the stock configs and exercise FAT
through GEMDOS — boot to the GEM desktop, then copy / read / write / rename /
mkdir / rmdir, Fsfirst/Fsnext via the desktop file selector, Dfree via the
info dialog. At minimum the smoke-test configs (atari512, STE, Falcon, TT,
raspi1, raspi2, virt-arm, virt-m68k); also compile-check the remaining
targets for coverage of the always-on path (atari192/256, aranym, amiga,
firebee, m548x, cartridge, floppy, atari512-dispatch, the `-cli` variants).
Bit-for-bit equivalent to today.

**on path**: `virt-arm` and `virt-m68k` with the option + test driver. Two
checks: (1) the relocated FAT driver still serves drives A:/etc. through the
vtable (same operations as above); (2) the registered foreign `pfs_test`
drive still appears and reads its one file — the "driver behind the layer"
story survives the move.

`make gitready` must pass.

## Documentation

- This spec: `docs/superpowers/specs/2026-08-11-invert-pluggable-fs-dispatch-design.md`.
- `fs/pfs.h` and `fs/fatfs_pfs.c` header comments describe the new shape
  (the "calls back into bdos/" adapter description in fatfs_pfs.c's header
  and the "See docs/superpowers/specs for the design this implements" pointer
  in pfs.h both need updating).
- `fs/Kconfig` help text for `CONF_WITH_PLUGGABLE_FS`.
- `fs/build.mk` header comment.
- `doc/status.txt` line about the pluggable layer's wiring, if it mentions
  the old dispatch order.

## Constraints

- A move, not a rewrite: the FAT logic's bodies relocate with their behaviour
  intact; only their entry shape changes (path -> `(DND, name)` cookie core,
  `findit()` moved into the off wrappers).
- Off-path behaviour must be bit-for-bit unchanged; the on path must keep
  working and keep the foreign-driver test passing.
- Source basenames stay unique across the tree (`fatfs_pfs.c`, `pfs.c`,
  `pfs_test.c` are unique).
- C90 declarations-before-statements, 4-space indent, `/* */` comments, no
  hard tabs (`make gitready`).
- No changes to the public bdos `funcs[]` wiring or the `Fread`/`Fwrite`/
  `Fclose` dispatch.
