# Mature the Native FAT Layer, informed by XFS Design

> **Status:** Approved, pending implementation. See `doc/status.txt` for the
> current state of FAT32 support (or the lack of it).

## Context

Issue #197 started as "evaluate adopting FatFs (ChaN) as an alternative
FAT12/16/32 backend". Brainstorming surfaced three findings that redirect the
work:

1. **FatFs has no fixed release for known CVEs.** runZero disclosed six CVEs
   in FatFs R0.16 (July 2026); two apply to an 8.3/no-exFAT build used here:
   **CVE-2026-6682** (High 7.6, FAT32 `mount_volume()` integer overflow ->
   memory corruption via crafted media) and **CVE-2026-6686** (Medium,
   uninitialized-cluster leak on extend-beyond-EOF). The author is
   unresponsive and no fixed release exists, so adopting FatFs means carrying
   a private fork with local fixes, or shipping known bugs.
2. **MiNT's XFS interface is the wrong thing to adopt whole.** Its driver
   interface (`struct filesys` in FreeMiNT `sys/mint/fsops.h`, a ~33-pointer
   vtable + `fcookie` + separate `DEVDRV`/`FILEPTR` file I/O) is built around
   a multi-process, uid/gid/mode, errno-core kernel. pTOS is single-user,
   GEMDOS-only. Making pTOS binary-compatible with `.xfs` modules means
   porting the FreeMiNT module loader, `kerinfo`, cookie system, and dos
   layer — effectively a MiNT kernel inside pTOS. Source-compatibility
   (re-shape `pfs_ops` into the XFS vtable shape) buys nothing without then
   porting drivers.
3. **One XFS design choice is worth taking: 32-bit object identity.** XFS's
   `fcookie.index` is a 32-bit `fs_ino_t`; it is exactly what lets MiNT's
   fatfs address FAT32's 28-bit cluster numbers. pTOS's `PFSCOOKIE.index/aux`
   are already `LONG` (32-bit) — the pluggable layer is already wide enough —
   but the legacy FAT layer it wraps is not: **`CLNO` is `UWORD`**
   (`bdos/fs.h:102`), and FAT32 is rejected at three separate gates today
   (see below).

So the deliverable of #197 pivots from "adopt FatFs" to: **mature the native
FAT layer (the FAT32 enabler: 32-bit cluster numbers, FAT32 BPB parsing,
28-bit cluster access, root-as-cluster-chain)**, with the FatFs spike kept as
a parallel comparison point, and an evaluation document capturing the XFS
lessons, the FatFs CVE analysis, and the native-vs-FatFs comparison.

This is new territory relative to upstream EmuTOS: upstream does not support
FAT32 either, so there is no upstream backport to lift — this is original
portable work that must keep m68k and ARM building.

## Target Architecture

### Phase 1 — widen the geometry types (groundwork)

- `CLNO` becomes `ULONG` (`bdos/fs.h:102`). This is the "widen the index from
  16 to 32 bit" change. It is mechanical but broad:
  - `DFD.o_strtcl`, `OFD.o_curcl` (`fs.h:134,161`), `FCB.f_clust` (`fs.h:200`),
    `DND.d_strtcl` (`fs.h:229`), `DMD.m_numcl` (`fs.h:267`), and the directory
    table entry `dt_clnum` (`fs.h:353`).
  - The FAT primitives in `bdos/fsfat.c`: `cl2rec`, `clfix`, `getrealcl`,
    `getclnum`, `findfree16`/`findfree`.
  - The `fs/fatfs_pfs.c` cluster walkers (`fat_countfree`, the readdir /
    chattr / rename paths).
- **Verified fit**: with `CLNO`=UWORD, `OFD`=56 and `DND`=56 bytes; with
  `CLNO`=ULONG, `OFD`=60 and `DND`=58 — both still under the 64-byte
  FOLDRnnn.PRG compatibility limit (`fs.h:148,221`). No repack or side-table
  needed. (Measured with `m68k-atari-mintelf-gcc -mshort` against the exact
  struct layouts.)
- `RECNO` is already `ULONG` (`fs.h:103`), so sector addressing is fine.

### Phase 2 — FAT32 in the BDOS FAT implementation

This is the real work; everything in Phase 1 is in service of it.

- **FAT32 boot-sector parsing.** Today only the FAT12/16 `struct bs` /
  `fat16_bs` are read (`bios/blkdev.c:617-618`). A FAT32 volume needs the
  32-bit fields: `BPB_FATSz32`, `BPB_TotSec32`, `BPB_RootClus`,
  `BPB_FSVer`, `BPB_ExtFlags`, and the FAT32-specific interpretation of the
  BPB (e.g. the 0xF8 media byte handling for "no FATs" in `XH_DL_MINSPC`).
- **28-bit cluster access in `fsfat.c`.** FAT32 entries are 4 bytes each
  (`offset = cl * 4`), versus the 12-bit `cl + cl>>1` and 16-bit `cl << 1`
  packing handled today (`fsfat.c:48,125`). Add a `m_32` flag to `DMD`
  alongside `m_16` (`fs.h:278`), and extend `clfix`/`getrealcl`/`getclnum`
  with the 4-byte path (byte-assembled like the 12-bit path, so no endian
  issues).
- **Root as a cluster chain.** This is the largest behavioural change. FAT32
  has no fixed root directory area: `rdlen` is 0, the root lives at
  `BPB_RootClus`. `log_media()` (`bdos/fsdrive.c:197-201`) currently returns
  `EDRIVE` when `fsiz == 0` (the FAT32 marker) and hard-codes the root as a
  pseudo-cluster at `d_strtcl = 2` (`fsdrive.c:229`). The root `DND` becomes a
  normal cluster chain like any subdirectory — the existing
  cluster-chain-walking code (`nextcl`, directory reads) then serves it
  unchanged.
- **FAT32 directory entries.** The `FCB` struct maps cleanly: the high word
  of the start cluster sits at dir-entry offset 0x14, which is exactly
  `f_fill[8..9]` of the existing `FCB` (`fs.h:194-202`); the low word at 0x1A
  is `f_clust`. So no new `FCB` fields — just a "read/write the 32-bit start
  cluster" helper that assembles `f_clust | (f_fill[8..9] << 16)` on FAT32
  and reads only `f_clust` on FAT12/16.
- **Dfree / free-cluster accounting at 32-bit** (`m_numcl`, `fat_countfree`).
- **Write path**: allocation (`findfree`), FAT entry updates, subdirectory
  creation, and 32-bit cluster-word assembly on dir entry writes all follow
  from the above once the primitives are 32-bit.

### Phase 3 — remove the BIOS/XHDI gates

- `bios/blkdev.c:662`: `tmp > MAX_FAT16_CLUSTERS` rejection -> allow up to
  the FAT32 limit (~2^28-2 clusters), parse FAT32, set a 32-bit-FAT flag.
- `bdos/fsdrive.c:197`: `fsiz == 0` rejection -> FAT32 root handling
  (Phase 2).
- `bios/xhdi.c:322`: `XH_DL_CLUSTS32` returns `EINVFN` ("No FAT32 support")
  -> real value; `XH_DL_BFLAGS` (`xhdi.c:325-328`) gains the 32-bit-FAT bit.
- `bios/disk.c:374` "TOS-compatible FAT32 partition" type and the
  `disk.c:562` / `blkdev.c:441` FAT32-unsupported notes get updated.

### Phase 4 — FatFs spike as comparison (kept, not shipping)

Per the brainstorming decision, the FatFs evaluation is kept **as a parallel
comparison point**, not the shipping path:

- Vendor ChaN FatFs R0.16 under `fs/fatfs/` (`ff.c`, `ff.h`, `ffconf.h`,
  `diskio.h`, `diskio.c`).
- A minimal adapter `fs/fatfs_ff_pfs.c` implementing `pfs_ops` over FatFs.
- `CONF_WITH_FATFS_EVAL` Kconfig symbol (default n,
  `depends on CONF_WITH_PLUGGABLE_FS`), enabled in `virt-m68k` only, so the
  adapter is compile-checked by CI without changing the shipping path.
- `ffconf.h` scoped to 8.3 short names only: `FF_USE_LFN=0`, `FF_USE_EXFAT=0`,
  `FF_USE_MKFS=0`, `FF_FS_REENTRANT=0`, code page 437, `FF_MIN_SS=FF_MAX_SS=512`.
- `diskio.c` glues to `disk_rw()` (`bios/disk.h:100`).

This spike exists to give the doc concrete cost/complexity numbers and to
keep FatFs as a viable fallback if the native work stalls — not to be
enabled in production. The CVE caveats (CVE-2026-6682/6686) are documented as
a reason the native path is preferred.

### Phase 5 — evaluation document

`docs/superpowers/specs/2026-08-15-fat32-backend-maturation-design.md`
(committed with the implementation) and a companion report section in
`doc/status.txt` covering:

- The FAT implementation landscape survey (FatFs, EDK2 FatPkg, U-Boot FAT,
  FreeBSD/NetBSD msdosfs, Linux fs/fat, MiNT fatfs.xfs, FreeDOS kernel,
  Petit FatFs, Circle).
- The XFS study: what was considered (binary vs source compatibility) and why
  only the 32-bit identity idea was adopted.
- The FatFs CVE analysis and why native code dodges it.
- Native-vs-FatFs comparison (scope, size, risk, maintainership).

## Configuration Changes

- New experimental symbol `CONF_WITH_FATFS_EVAL` (default n,
  `depends on CONF_WITH_PLUGGABLE_FS`), enabled in `configs/virt-m68k_defconfig`
  to compile-check the spike. Regenerated with `make savedefconfig`.
- FAT32 itself is not a new config: it becomes part of the always-on FAT
  implementation, gated only by `CLNO` widening + the Phase 3 gate removal.
  No per-machine option.

## Behaviour And Verification

FAT12/16 must stay bit-for-bit identical (the majority of configs). FAT32 is
new behaviour. Use the ptos-smoketest skill's verified emulator invocations:
QEMU for `raspi1ap`/`raspi2b`/virt-arm/virt-m68k, Hatari for the Atari m68k
targets (note the Falcon IDE 31 s boot wait).

- **Regression (FAT12/16)**: boot the stock smoke-test configs (atari512,
  STE, Falcon, TT, raspi1, raspi2, virt-arm, virt-m68k) to the GEM desktop and
  exercise copy/read/write/rename/mkdir/rmdir, Fsfirst/Fsnext, Dfree. Compare
  against the pre-change build.
- **FAT32 reads**: create a FAT32 image (e.g. `mkfs.fat -F 32`), attach it as
  a QEMU drive / Hatari IDE image, boot, and verify the desktop lists it,
  files read correctly, and Dfree reports the right free/total space.
- **FAT32 writes**: create/delete/rename files and subdirectories, then
  verify the image on the host (`fsck.fat -n`, mount and `diff`).
- **Boundary**: a FAT16-sized FAT32 (cluster count <= 65524) still
  distinguishes correctly from FAT16.
- The `CONF_WITH_FATFS_EVAL` config must compile clean (spike only; not
  boot-tested as the shipping path).
- `make gitready` must pass.

## Constraints

- A maturation of the existing implementation, not a rewrite: FAT12/16
  behaviour must be unchanged; FAT32 is additive on top of widened types.
- The 64-byte `OFD`/`DND` FOLDRnnn.PRG limit must be respected (verified to
  fit at 60/58).
- The public `BPB` (GetBPB() ABI, `biosdefs.h:24-32`, an 18-byte all-UWORD
  TOS layout) stays untouched: FAT32 geometry travels in a separate wider
  internal record (option B), not by widening `BPB` fields.
- No changes to the public bdos `funcs[]` wiring or the Fread/Fwrite/Fclose
  dispatch.
- `PFSCOOKIE` stays as-is (already 32-bit); the pfs vtable shape is not
  changed to mimic XFS.
- C90 declarations-before-statements, 4-space indent, `/* */` comments, no
  hard tabs (`make gitready`), unique source basenames.

## Follow-Up Work (explicitly out of scope here)

- LFN (long file names) / VFAT — GEMDOS can't represent them anyway.
- exFAT — out of scope entirely.
- Real XFS compatibility (binary `.xfs` loading or vtable source-shape
  compatibility) — rejected in brainstorming, documented in the report.
