# Cherry-pick upstream bdos/ bugfixes (Design)

## Context

Issue #109 ports a set of small, self-contained filesystem and process bugfixes
that upstream EmuTOS landed after pTOS's fork point (`aaf30d28fb`) but that this
tree never received. The issue lists ten upstream commit hashes; one of them
(`1e6758ef`) is a re-applied duplicate of another (`05797930` — its own commit
message says "previously committed as 0579793", identical diff), so there are
**nine logical fixes** to port. Implementation analysis additionally found that
the last fix (`f73f452b`) depends on upstream's OFD/DFD restructure
(`1c120131`, "Reorganise the filesystem's OFD structure", committed one day
earlier explicitly "in preparation for" the fix), which pTOS also lacks; the
user approved porting that pair together, making **ten commits** in total.

Audit outcome, against current master (`77b7c31b`, PR-#128 toolchain default
now `m68k-atari-mintelf`):

- All nine fixes are genuinely **missing from master**. Three orphaned pTOS
  commits with matching titles (`7266b4f5` "Fix bug in parsing pathname",
  `8eb432e2` "Fail with error code in case of corrupted relocation table",
  `673f688c` "Fix BDOS bug: renaming a R/O file is not allowed") exist as
  objects but sit on no branch — they are not ancestors of HEAD and no branch
  contains them. They read as a prior, never-merged attempt at this same issue
  and already carry the pTOS adaptation for the three cleanest fixes; this
  port reuses them as the reference rather than re-deriving the adaptation.
- Six of nine conflict against pTOS's `bdos/` divergence when cherry-picked
  raw. This is therefore a **port, not a blind cherry-pick**: each fix is
  applied to pTOS's current code and reconciled by hand.

## Goals

- Land all nine upstream bdos/ correctness fixes on pTOS master.
- Preserve every pTOS-specific behavior the `bdos/` files have gained since
  the fork (ARM/ELF-loader maintenance in `kpgmld.c`/`proc.c`, FastRAM 4-byte
  alignment rounding in `iumem.c`, the `void *lastcp` signature of
  `pgfix01()`, endian.h byte-swap macro moves).
- Keep both supported arches building at every commit: m68k (`atari512`,
  now mintelf-by-default) and ARM (`rpi2`). `bdos/` is m68k-core but is
  compiled on ARM too.
- Exercise the affected GEMDOS paths on real-ish hardware: a final Hatari
  STE smoke test of `ptos512k.img` boots to the GEM desktop.

## Non-goals

- Porting any non-`bdos/` upstream fix. Out of scope for #109.
- Refactoring `bdos/` beyond what a given upstream fix requires.
- Syncing the fork point or pulling in unrelated upstream history.
- Introducing host-compiled unit tests for GEMDOS. pTOS is freestanding with
  no libc and no host runtime; verification is cross-build + emulator boot,
  not a host test harness (the same conclusion the VDI spec reached).

## Per-fix plan

Each fix is one commit, ordered low-to-high risk so the tree stays green and
the riskiest change is cheapest to drop. "Reuse" means the orphaned pTOS
commit already carries the adaptation; "reconcile" means hand-port against
pTOS's current code.

1. **`323ee921` — dcrack() empty-pathname-with-colon** (`bdos/fsdir.c`).
   One-line guard so an empty pathname followed by `:` is not mis-parsed as a
   drive spec. Clean apply expected; reuse orphan `7266b4f5` as reference.

2. **`96165134` — renaming a read-only file must be disallowed**
   (`bdos/fsdir.c`). The upstream fix *adds* an `FA_RO` → `EACCDN` check in
   `xrename()` (the issue's paraphrase has the direction inverted; the patch
   is unambiguous). Append the 6-line guard. Clean apply expected; reuse
   orphan `673f688c` as reference.

3. **`05797930` — corrupted PRG relocation table, odd offset** (`bdos/kpgmld.c`).
   Adds `if (((LONG)cp) & 1) return EPLFMT;` to `pgfix01()` so an odd
   relocation offset returns an error instead of crashing with an address
   error on 68000. Adapt to pTOS's `void *lastcp` signature; reuse orphan
   `8eb432e2` as reference. (`1e6758ef` skipped — duplicate.)

4. **`777991d4` — Fcreate overwrite-delete check** (`bdos/fsopnclo.c`).
   Checks the return of `ixdel()` when overwriting an existing file so a
   file that is open in another process cannot be silently duplicated.
   Small conflict vs pTOS; reconcile.

5. **`d4757152` — first file in empty filesystem corrupts its first cluster**
   (`bdos/fsio.c`). A `last == 0L` "first time through" test was used to
   capture `p->o_currec`, but `o_currec` is legitimately 0 for the first
   record, so the test misses the genuine first pass the second time around.
   Replaced with an explicit `BOOL first_time` flag. Small context conflict
   vs pTOS's `xrw_recs`; reconcile.

6. **`8e04469b` — file-handle leak in Pexec() on load errors** (`bdos/proc.c`
   only, in pTOS). Upstream's fix also refactors `kpgmhdrld()`'s signature
   (open moves into `xexec()`). **pTOS does not need that refactor**: pTOS's
   `kpgmhdrld()` already self-closes the handle on its own error paths
   (the `fail: xclose(*h)` block, added for the ELF/PRG dispatch), and pTOS's
   `kpgmld()` already closes `h` unconditionally before returning. The leak
   is therefore confined to the three `xexec()` error paths that run *between*
   `kpgmhdrld`'s success and the `kpgmld` call: the `alloc_env` failure
   `return ENSMEM`, the `alloc_tpa` failure `return ENSMEM`, and the `setjmp`
   longjmp handler. Port = insert `xclose(fh);` on those three paths. No
   `kpgmld.c` or `proc.h` change. Reused orphan `8eb432e2`-style minimalism
   applies to commit 3, not here.

7. **`e65ae149` — Mshrink() corrupts the free-memory-descriptor chain**
   (`bdos/iumem.c`). A single contiguous free area must be described by one
   MD; `Mshrink()` called twice on the same block could leave two adjacent
   MDs on the free chain. Fix: instead of inserting the freed portion's MD
   directly into the free list (which bypasses coalescing), place it on the
   **allocated** list (`f->m_link = mp->mp_mal; mp->mp_mal = f;`), update
   `m->m_length`, then call `freeit(f, mp)` which coalesces. **Substantive**:
   pTOS's `shrinkit()` rounds `newlen` up to a multiple of 4 for FastRAM
   alignment — that rounding **must survive**. Reconcile: keep the rounding,
   drop the `p`/`q` locals, replace the free-list insertion block with the
   allocated-list push + `freeit()`.

8. **`d2d08811` — off-by-one range test in Fclose() for standard handles**
   (`bdos/fsopnclo.c`). When closing a standard handle, the test for "mapped
   to a character device" was `if (h <= 0)` — but handle 0 is a valid standard
   handle, so `<= 0` wrongly treats "Fforce'd to stdin" as done. Upstream also
   adds a guard against a standard handle being Fforce'd to *another* standard
   handle (returns `EIHNDL`). Analysis resolved the earlier open question:
   pTOS's `xclose()` standard-handle branch has the **same structure** as
   upstream's pre-fix code, so the fix maps **1:1** — change `if (h <= 0)` to
   `if (h < 0)` and add `if (h < NUMSTD) return EIHNDL;`. The "conflict"
   reported by probe was context drift elsewhere in the file, not in the
   edited hunk.

9a. **`1c120131` — reorganise the OFD: introduce DFD** (`bdos/fs.h`,
    `bdos/fsdir.c`, `bdos/fsdrive.c`, `bdos/fsfat.c`, `bdos/fsio.c`,
    `bdos/fsopnclo.c`). **Prerequisite for `f73f452b`** (committed one day
    earlier, described as "in preparation for a fix to a filesystem bug that
    can cause lost clusters"). pTOS's OFD currently holds `o_td`/`o_strtcl`/
    `o_fileln`/`o_usecnt` directly and `makopn()` *memcpys* the metadata from
    an already-open OFD ("a bit clumsily"); there is no `DFD`, no `o_disk`,
    no `o_dfd` anywhere in the tree. The 64-byte OFD `FOLDRnnn.PRG`
    constraint survives upstream's restructure (the embedded `DFD o_disk`
    still fits). Conflicts in `fsdir.c`, `fsio.c`, `fsopnclo.c` (pTOS-specific
    `le2cpu16/le2cpu32` endian conversions in `makopn` and elsewhere); the
    restructure must be reconciled to keep pTOS's endian handling. Cleanly
    applies to `fs.h`, `fsdrive.c`, `fsfat.c`.

9b. **`f73f452b` — lost clusters with concurrent writes to the same file**
    (`bdos/fs.h` comment + `bdos/fsopnclo.c`). Each handle independently
    acquired free clusters but the on-disk chain reflected only the last
    handle closed — the bug also exists in Atari TOS 1–3, fixed in TOS 4.
    The fix (built on `1c120131`): `makopn()` now shares a single DFD via
    `o_dfd` (incrementing `o_usecnt` when the file is already open), and
    `sftdel()` decrements `o_usecnt`, freeing the non-base OFD immediately
    and freeing the base OFD only when the count hits zero. Done last so it
    is the cheapest to drop if it destabilizes boot; verify all initializer
    and `sizeof` fallout across the tree.

## Verification

Per commit, after the edit:

- `make gitready` (whitespace/style).
- Build m68k: `make atari512_defconfig && make` (mintelf is now the default,
  so no `.config` toolchain hack is needed).
- Build ARM: `make rpi2_defconfig && make`.
- Both must reach the "... is ready" line with no new warnings.

End of batch, after `f73f452b`:

- **Hatari STE smoke test** of `ptos512k.img` per the ptos-smoketest skill:
  `--machine ste --memsize 4 --sound off --run-vbls 1200` (Avi-record frame
  inspection, ~60 s emulated). Pass signal is the GEM desktop rendering. This
  exercises GEMDOS file open/close, rename, create, program load, and memory
  shrink paths — the very code this PR changes.
- Optional QEMU raspi2 boot to confirm ARM `bdos/` still boots post-#128.

## Risk and backout

- Per-commit commits mean `git revert` isolates any single fix that
  regresses the post-merge Hatari boot.
- `1c120131` + `f73f452b` are last precisely so they can be dropped as a
  pair if the restructure destabilizes boot.
- Each commit message records the pTOS-specific adaptation made, so a
  future re-sync with upstream is unambiguous about what was ported vs
  reconciled.

## Resolved open questions

- `d2d08811` maps 1:1 onto pTOS's current `xclose()` standard-handle
  branch (`if (h <= 0)` → `if (h < 0)` plus the `h < NUMSTD` guard); no
  rewrite decision needed. Resolved during plan-source gathering.
- `f73f452b` cannot port alone; `1c120131` (OFD/DFD restructure) is a
  prerequisite. User approved porting both as commits 9a/9b.