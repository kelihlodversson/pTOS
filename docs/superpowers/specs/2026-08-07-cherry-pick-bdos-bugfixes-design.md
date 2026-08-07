# Cherry-pick upstream bdos/ bugfixes (Design)

## Context

Issue #109 ports a set of small, self-contained filesystem and process bugfixes
that upstream EmuTOS landed after pTOS's fork point (`aaf30d28fb`) but that this
tree never received. The issue lists ten upstream commit hashes; one of them
(`1e6758ef`) is a re-applied duplicate of another (`05797930` — its own commit
message says "previously committed as 0579793", identical diff), so there are
**nine logical fixes** to port.

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

2. **`96165134` — rename of a read-only file wrongly disallowed** … wait,
   re-check direction: the upstream fix *disallows* renaming a read-only file
   (adds an `FA_RO` → `EACCDN` check in `xrename`). Append the 6-line guard.
   Clean apply expected; reuse orphan `673f688c` as reference.

3. **`05797930` — corrupted PRG relocation table, odd offset** (`bdos/kpgmld.c`).
   Adds `if (((LONG)cp) & 1) return EPLFMT;` to `pgfix01()` so an odd
   relocation offset returns an error instead of crashing with an address
   error on 68000. Adapt to pTOS's `void *lastcp` signature; reuse orphan
   `8eb432e2` as reference. (`1e6758ef` skipped — duplicate.)

4. **`777991d4` — Fcreate overwrite-delete check** (`bdos/fsopnclo.c`).
   Checks the return of `ixdel()` when overwriting an existing file so a
   file that is open in another process cannot be silently duplicated.
   Small conflict vs pTOS; reconcile.

5. **`d4757152` — first file in an empty filesystem corrupts cluster 0**
   (`bdos/fsio.c`). Adds a `first_time` guard so the free-cluster scan does
   not clobber cluster 0 when the directory is empty. Small conflict;
   reconcile.

6. **`8e04469b` — file-handle leak in Pexec() on load errors** (`bdos/kpgmld.c`,
   `bdos/proc.c`, `bdos/proc.h`). Ensures the file handle is closed on every
   error path of `xexec()`. **Substantive**: pTOS actively maintains the ARM
   ELF loader in these same files; reconcile by porting the leak fix onto the
   ELF path as well as the PRG path, not by taking the upstream structure
   wholesale.

7. **`e65ae149` — Mshrink() corrupts the free-memory-descriptor chain**
   (`bdos/iumem.c`). The freed portion's MD must be placed on the allocated
   list before `freeit()` (which coalesces), not inserted into the free list.
   **Substantive**: pTOS's `shrinkit()` rounds `newlen` up to a multiple of 4
   for FastRAM alignment — that rounding **must survive**. Reconcile: keep
   the rounding, apply the list-management fix around it.

8. **`d2d08811` — off-by-one range tests in Fclose() for standard handles**
   (`bdos/fsopnclo.c`). Restructures `xclose()` so a standard handle mapped
   via `Fforce()` is closed correctly. Upstream rewrites the function's
   structure; pTOS's `xclose()` already differs. Decide per-branch: take
   upstream wholesale only if pTOS has no divergent logic of its own there;
   otherwise reconcile.

9. **`f73f452b` — lost clusters with concurrent writes** (`bdos/fs.h`,
   `bdos/fsopnclo.c`). **Largest change.** Adds a `o_usecnt` reference count
   and a "base OFD" concept to `DFD` so that multiple OFDs of the same file
   share one copy of the directory data and the base OFD is freed only when
   the last user closes. Struct layout change in `fs.h` (~10 added lines) plus
   refcount plumbing in `fsopnclo.c` (~14 added lines). Done last so it is
   the cheapest to drop if it destabilizes boot; verify all initializer and
   sizeof fallout across the tree.

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
- `f73f452b` is last precisely so it can be dropped independently.
- Each commit message records the pTOS-specific adaptation made, so a
  future re-sync with upstream is unambiguous about what was ported vs
  reconciled.

## Open question for the plan phase

- Whether `d2d08811` should take upstream's `xclose()` rewrite wholesale or
  be reconciled onto pTOS's current structure. Left to the implementation
  plan to decide by diffing the two `xclose()` bodies.