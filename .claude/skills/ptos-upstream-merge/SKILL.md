---
name: ptos-upstream-merge
description: Use when merging upstream EmuTOS into pTOS, resolving the conflicts from that merge, or auditing a completed (or previously completed) merge for correctness. Covers preserving merge ancestry (never squash/rebase), the pre-merge worktree baseline, the specific way conflict resolution silently drops code guarded by not-yet-Kconfig'd macros, known m68k-only idioms that break on ARM, downstream-extension collisions (pTOS extends a domain upstream also defines), the three mandatory post-merge audits (unconfigured macros, deleted upstream blocks, dropped pTOS-side code) with their exact commands, why the full CI matrix — not just local rpi1/rpi2 builds — is part of the verification bar, and a worked-example table of every bug found in the 2026-08 merge (pTOS 78308b8b + upstream edf307a6, PR #264).
---

# Merging upstream EmuTOS into pTOS

Upstream EmuTOS has no Kconfig and no ARM port — it edits `include/config.h`
directly and assumes m68k throughout. pTOS is Kconfig-driven and portable to
ARM. Every upstream merge crosses that gap, and the failure modes are
specific enough to be worth checking for by name rather than by general
vigilance. This is a process skill — pair it with `ptos-smoketest` for the
actual boot-verification commands.

## 1. Before merging: take a baseline

```sh
git worktree add /tmp/ptos-master-baseline master
cd /tmp/ptos-master-baseline && git submodule update --init   # lib/libcmini, needed for regression tests
make rpi2_defconfig && make
```

Keep this around for the whole merge. It is the only reliable way to tell
"this boots differently than before" from "this was already broken" — don't
take anyone's word (including your own past reasoning) that a boot failure
is pre-existing without checking the baseline first. This is how the
SD-card corruption, the screen-init ordering bug, and the VDI-trap-dispatch
bug all got confirmed as regressions rather than dismissed.

## 2. Preserve merge ancestry — never squash, never rebase

**The upstream sync must land as a real merge commit**, with pTOS's
pre-merge tip and upstream's tip as its two parents. Squash-merging or
rebasing the branch collapses that history — the next upstream sync loses
the recorded common ancestor, `git merge-base` degrades to guessing (or
walking back to the *previous* sync point, dragging thousands of
already-merged commits back into scope), and every audit technique in this
skill that diffs against "upstream's tip" or "pTOS's pre-merge tip" loses
its reference points. A one-off squash looks harmless in the PR that does
it; it makes every future sync more expensive, not just this one.

Treat this as a hard rule, not a preference, and verify it explicitly
before considering the merge done:

```sh
# The merge commit must have exactly two parents...
git log -1 --format=%P <merge-commit> | wc -w   # expect 2

# ...and upstream's tip must be a real ancestor of the result (true for a
# genuine merge; false if it was squashed or the branch was rebased onto
# upstream instead of merging upstream in).
git merge-base --is-ancestor <upstream-tip> <merge-commit> && echo OK
```

If a PR review tool or "squash and merge" button is the repo/team default,
override it for this PR specifically — a fast-forward-only or squash merge
silently discards exactly the topology this whole skill depends on.

## 3. The core trap: conflict resolution deletes code it thinks is dead

**A code block guarded by `#if CONF_WITH_X` or `#ifdef X`, where `X` isn't
defined anywhere yet (no Kconfig entry exists at merge time), does not
compile to nothing — a human resolving the conflict by hand can easily read
it as dead/unreachable code and delete it**, rather than leaving it intact
for a later Kconfig pass to wire up. This happened repeatedly in the
2026-08 merge (see §7) and is invisible to compilation, because deleted
code and never-configured code look identical at build time — both are
just absent from the binary. Nothing errors. Nothing warns. The only way to
catch it is the diff-based audits in §5, run *after* the merge, against
both upstream's tip and pTOS's own pre-merge tip.

**Rule while resolving conflicts:** never delete a hunk just because its
guard macro looks unfamiliar or undefined. If a conflict is genuinely
unresolvable by inspection (upstream's version and pTOS's version both
changed the same lines for different reasons), keep both, wrap them
distinctly, and flag it — don't pick one side and silently drop the other.
Whether the macro deserves a Kconfig entry is a separate, later decision;
deleting the code forecloses that decision instead of deferring it.

**The mirror-image mistake is just as common: keeping both sides instead of
reconciling them.** When a conflict resolver takes the "safe" route of
concatenating both versions of a hunk rather than picking or merging them,
the result is two definitions of the same symbol — sometimes identical
(harmless-looking, but still a redefinition error), sometimes each correct
in a different way (one side has a real bug fix the other lacks). This
showed up as duplicate `#define`s, duplicate assembly labels, and duplicate
C functions across *five separate files* in PR #264, entirely undetected by
local builds targeting only the machines that happened not to compile that
file, and only surfaced once CI exercised every configuration (§6). A
targeted scan for this pattern, once conflicts are resolved:

```sh
# Duplicate macro definitions with the same value close together (the
# unambiguous case — different values under #if/#else are normal and not
# a bug; read the surrounding context before treating a hit as real):
git grep -n '^#define ' -- '*.h' '*.c' | awk -F: '{split($3,a," "); if (a[2] && seen[$1 SUBSEP a[2]]++) print}'

# Duplicate C function definitions in the same file (same caveat — legitimate
# #if/#else alternative implementations, e.g. CONF_WITH_3D_OBJECTS on/off
# variants of the same function name, will also match; read before fixing):
git grep -oE '^(static )?[A-Za-z_][A-Za-z0-9_ *]*\**[A-Za-z_][A-Za-z0-9_]*\s*\([^;{]*\)\s*$' -- '*.c' | sort | uniq -c | sort -rn | awk '$1>1'
```

## 4. Known upstream idioms that break on ARM

Upstream is m68k-only, so none of these have ever been exercised on a
32-bit-int, little-endian, alignment-sensitive target. Grep for them
specifically in anything upstream touched:

| Idiom | Why it breaks on ARM | Fix |
|---|---|---|
| `swpl()`/`swpw()` unconditional byte-swap macros | m68k is always big-endian, so an unconditional swap is correct there and *only* there; on little-endian ARM it double-converts an already-correct value | Use `le2cpu32()`/`cpu2le32()`/`bswap16()` from `include/endian.h` for any little-endian on-disk/protocol value (MBR/GPT fields, etc.) |
| Bare `int`/`unsigned int` in code meant to be 16-bit | m68k builds with `-mshort` (`int` == 16 bits); ARM's `int` is 32 bits. A field silently doubles in width, or arithmetic that relied on 16-bit wraparound stops wrapping | Use `WORD`/`UWORD`/`LONG`/`ULONG` from `portab.h` explicitly — never bare `int` in shared code |
| Assuming GEMDOS `Malloc()`/`Mxalloc()` blocks are 4-byte aligned | TOS only ever guaranteed *even* (2-byte) alignment on non-Falcon machines, which is all m68k needs; ARM's compiler assumes natural 4-byte alignment for pointer-sized fields and can emit `STRD`/`LDRD`, which fault on misalignment regardless of the CPU's unaligned-access tolerance | See `bdos/umem.c`'s `malloc_align_stram`: force 4-byte alignment unconditionally under `#ifndef __m68k__` |
| A low-level I/O primitive's `ubufr == NULL` "return a pointer, don't copy" special case | Looks like dead branch to someone refactoring for clarity; several BDOS callers (`scan()`, directory walks) depend on that exact contract to peek at a buffer without copying | When upstream rewrites a primitive like this, diff the *contract*, not just the code shape — check every caller of the old version still gets what it expects from the new one |
| Linker `SUBALIGN(N)` on shared sections | Per the GNU ld manual, `SUBALIGN` *overrides* each input section's own requested alignment, not just bounds it; `SUBALIGN(2)` is deliberate on m68k (tight ROM packing, and m68k tolerates it) but silently downgrades ARM objects that need real 4-byte/1KB alignment | Gate `SUBALIGN` on `ARCH_ARM`, see `emutos.ld` |
| Init-ordering assumptions baked into m68k boot flow | m68k machines don't have a "screen must be mailbox-initialized before line-A reads its dimensions" step; a function that reads screen geometry can silently run before the geometry exists on a machine upstream never had | Trace the actual call order on the new machine, don't assume upstream's ordering transfers |

## 5. Downstream-extension collisions: pTOS extends a domain upstream also defines

A different failure mode from §3/§7: upstream introduces a perfectly valid,
self-consistent definition — but it's *incomplete* for pTOS specifically,
because pTOS extends the same domain with something upstream doesn't know
about. Upstream's definition isn't wrong on its own terms; it's just scoped
to upstream's world.

Concrete pattern to watch for: any `MAX_*` bound, capability/feature
bitmask, enum, dispatch table, array size, or opcode range that enumerates
a fixed set of "known" things — bus types, device classes, machine IDs,
backend IDs. If pTOS added a new member to that set (a new bus, a new
backend, a new device class) *before* this merge, and upstream
independently grew the same enumeration for its own reasons, the merge can
silently resolve to whichever side's version "won" the conflict — leaving
either pTOS's addition dropped, or upstream's count/table sized for a
smaller set than pTOS actually needs. Two known instances from this
merge's lineage: `MAX_BUS`, where upstream knows only ACSI/SCSI/IDE/SDMMC
but pTOS adds VirtIO, and an `EXTENDED_PALETTE`-shaped enum with the same
shape of gap. `bios/disk.h` now owns the fixed ABI bus-number enum and its
final `MAX_BUS` member; preserve both when merging upstream changes to
`bios/machine.h` or disk-driver bounds.

This rarely produces a build error — both sides' definitions compile fine
in isolation, and the merged value is *a* valid number, just not
necessarily the *right* one for pTOS's actual member count. Detect it by
diffing definitions, not by watching for compiler complaints:

```sh
# For every symbol matching this shape, diff pTOS's pre-merge value against
# upstream's, and check the diff against pTOS's own domain (does pTOS have
# more members in this set than upstream's number accounts for?):
git grep -nE '^\s*#define\s+MAX_[A-Z_]+\s+[0-9]' -- '*.h' '*.c'
diff <(git show <pTOS-pre-merge-tip>:<file> | grep -E '^\s*#define\s+MAX_') \
     <(git show <upstream-tip>:<file> | grep -E '^\s*#define\s+MAX_')
```

Also read every enum/dispatch-table definition upstream touched in a file
pTOS has *also* independently extended (found via the same `# Conflicts:`
list as §5's other audits) and count entries by hand against both sides'
domains — this needs judgment, not just a diff, since the fix is usually
"take the union," not "take either side."

## 6. Three mandatory post-merge audits

Do these *after* conflicts are resolved and the tree builds, before
considering the merge done. They catch different things and none
substitutes for another — run all three.

### 6a. Unconfigured-macro audit

Finds `CONF_WITH_*`-style macros that are referenced via `#if`/`#ifdef`
somewhere in the tree but have no Kconfig `config` stanza anywhere — they
silently evaluate to `0` under `-Wundef` and are permanently unreachable
via `menuconfig`, even though their code still exists.

```sh
git grep -ohE '\b(CONF_WITH_[A-Z0-9_]+|DETECT_[A-Z0-9_]+)\b' -- '*.c' '*.h' '*.S' | sort -u > /tmp/used_macros
# For each, check for a real stanza (not just a mention):
for m in $(cat /tmp/used_macros); do
  git grep -q "^config $m\b" -- '*Kconfig*' || echo "NO KCONFIG: $m"
done
```

Cross-check any hit against `include/config.h` — if it's hardcoded there
instead, that also violates the project convention ("config.h now holds
only values derived from the configuration... do not add per-target
defaults there," per `CLAUDE.md`) and needs a proper Kconfig entry instead.

For each gap, recover upstream's original per-machine default logic before
writing the Kconfig `default`/`depends on`:

```sh
git show <upstream-tip>:include/config.h | grep -B5 -A5 CONF_WITH_<NAME>
```

That shows the old `#ifndef CONF_WITH_X / #define CONF_WITH_X <default>`
fallback, usually conditioned on `#if defined(MACHINE_...)` or another
already-Kconfig'd option — translate that condition directly into
`depends on`/`default` clauses.

### 6b. Upstream-deleted-block audit

Finds upstream code that's actually *gone* from the merge result, not just
unconfigured — this is what catches the §3 failure mode, and it will find
things §6a cannot, because a macro with zero remaining references anywhere
doesn't show up in a code-driven search at all.

Scope this to the files that were **originally conflicted** — cleanly
auto-merged files don't have this failure mode, since git's auto-merge
takes clean diff hunks from one side or the other and never deletes code
based on whether a macro is defined. Get that list from the merge commit's
`# Conflicts:` section:

```sh
git log <merge-commit> -1 --format=%B | grep '^#\s' | sed 's/^#\s*//'
```

**Gotcha:** this only works if the final commit message actually preserved
the `Conflicts:` list from `.git/MERGE_MSG` — if you write your own
commit message via `-F`/`-m` instead of accepting the auto-populated one,
copy that section in first, or you lose the list and have to reconstruct it
by re-running the merge in a scratch worktree.

For every file in that list:

```sh
diff -u <(git show <upstream-tip>:<file>) <(git show HEAD:<file>)
```

Read every removed (`-`) hunk that is C/preprocessor code, not a comment or
copyright header. For each one: is it inside an `#if`/`#ifdef` guard on a
`CONF_WITH_*`/`MACHINE_*`/feature-looking macro? If yes and there's no
equivalent replacement elsewhere in the file (check — pTOS sometimes has a
legitimate superseding mechanism, e.g. its own truecolor VDI backend
replacing an upstream 16-bit-mode code path; that's not a loss), it's a
candidate. Read enough surrounding context to tell a real deletion from a
harmless reordering (diffs of reordered-but-intact functions look similar
at a glance).

Also specifically re-diff occurrence *counts* for every macro §6a already
flagged as a gap, upstream vs. HEAD (`git grep -c CONF_WITH_X <rev>` on
each). If upstream has more occurrences across more files than HEAD, even
though the macro now has *some* live `#if` (which is why §6a found it at
all), there may be a second or third guarded block in another file that
never made it across.

### 6c. pTOS-preservation audit (the mirror image of 6b)

Just as conflict resolution can silently drop upstream code, it can
silently drop **pTOS's own** fixes, ARM hooks, portability workarounds, and
feature code — a conflict resolved by taking upstream's side of a hunk
wholesale when pTOS's own version should have won, or needed reconciling
with it. This is not hypothetical: in PR #264, this audit caught a dropped
`#include "asm.h"` in `aes/geminput.c` that left two calls
(`disable_interrupts()`/`enable_interrupts()`) compiling with no prototype
in scope, and — found later, via CI rather than this audit, see §7's note
on coverage limits — an entire pTOS-only assembly routine
(`aes/arch/m68k/gemdosif.S`'s `justretf`/`aestrap_intercepted`) missing
outright, undefined-reference errors on every m68k link.

**Why this needs a different method than 6b:** pTOS's own changes aren't
reliably guarded by `#if CONF_WITH_*` the way upstream's newer features
are — they're often plain bug fixes, ARM-port additions, or refactors with
no preprocessor signal at all. Grepping for macro guards won't find these;
you need a real three-way-diff read.

```sh
# Isolate exactly what pTOS itself changed in a file since the histories
# diverged, with zero noise from upstream's independent evolution of the
# same file:
merge_base=$(git merge-base <pTOS-pre-merge-tip> <upstream-tip>)
diff -u <(git show $merge_base:<file>) <(git show <pTOS-pre-merge-tip>:<file>)
```

Every hunk in that diff is pTOS-authored and must survive the merge in some
form. For each hunk, check `git show HEAD:<file>` for the same content or a
genuine functional equivalent (not just "something in the same area" —
upstream's own later refactor of the same function can look similar while
quietly dropping the ARM-specific branch or the bug-fix condition pTOS's
hunk introduced). Prioritize:

- Bug fixes (comment mentions a specific bug, edge case, CPU-model
  threshold, or "fix" — `git log <merge_base>..<pTOS-pre-merge-tip> --oneline -- <file>`
  finds the pTOS commit that introduced it, which usually explains *why*
  and lets you judge whether HEAD still does the right thing)
- ARM/portability adaptations — anything touching endianness, alignment,
  `WORD`/`UWORD` vs. bare `int`, machine-conditional code
- Small, easy-to-miss one-liners: an `#include` a function actually needs,
  a single field added to a struct, a one-line signature/type correction
  (these are exactly what a "keep upstream's whole hunk" resolution
  silently drops, since they're too small to notice went missing)
- Whole pTOS-only files/routines in a conflicted area with no upstream
  equivalent at all (e.g. an m68k-only assembly stub upstream never had) —
  these are invisible to a hunk-level diff of a *shared* file entirely;
  cross-check the full symbol list (`.globl`s, function definitions)
  between the pre-merge and post-merge version of every conflicted
  `.S`/`.c` file that has one, not just the diff of changed lines.

Given the volume (potentially 150+ conflicted files, many with years of
independent pTOS-side history), triage: files where step 1's diff is small
are tractable to fully verify; for files where pTOS's changes are extremely
extensive, do a coarser structural check (do the file's key functions and
exported symbols still look intact?) and say so explicitly rather than
silently skipping or claiming full coverage you didn't do.

## 7. Full CI matrix is part of the verification bar — not a formality

**Local smoke testing (§8) only covers what you can actually build and run
in the working environment** — in this session's environment, that meant
ARM only (`rpi1`/`rpi2`/`rpi3`/`rpi4`/`virt-arm*`), since no m68k
cross-toolchain was installed. Treating "rpi1/rpi2 boot cleanly" as "the
merge is done" is a real trap: PR #264's ARM builds and boot tests were
clean for most of a multi-day session while the *entire* m68k build family
(`atari*`, `amiga*`, `firebee*`, `m548x-*`, `aranym`, `cartridge`,
`floppy`, `prg`, `virt-m68k*`, and the release-archive job) was still
broken — eleven separate bugs, each hiding behind the previous one in the
object-file link order, only surfaced one at a time as CI re-ran after
each fix (see §9's "CI-only" rows). None of them were reachable from an
ARM-only local build; several (duplicate symbols, dropped m68k-only
routines) couldn't be reachable that way even in principle, since the
affected files aren't part of an ARM build at all.

**The rule:** treat the full CI configuration matrix as the actual build
gate, and local smoke testing as a fast first pass over whatever subset you
can run directly — not the other way around. Push, watch every job (not
just the ones you expect to be relevant), and iterate:

```sh
gh pr checks <PR> -R <owner>/<repo>
# for a specific failing job's log once the whole run has completed
# (per-job logs aren't downloadable while the run is still in progress):
gh run view --job <job-id> -R <owner>/<repo> --log | grep -B3 'error:'
```

Expect this to be genuinely iterative — fixing the first m68k build error
reveals the next one further down the same file's object list, and a
single session may need several push/CI-round/fix cycles before the matrix
is green. That's normal for a merge this size, not a sign something is
wrong with the approach.

## 8. Local build/boot verification workflow

1. Build the default config for every affected machine (`rpi1_defconfig`,
   `rpi2_defconfig`, at minimum) with **no manual `.config` edits** —
   catches the class of bug that only shows up in what `menuconfig`'s
   actual defaults resolve to (like the country.mk font-link failure).
2. For every newly-added or newly-restored Kconfig option, explicitly force
   it to `y` and rebuild. Default-off code has had zero compiler attention
   and reliably breaks the moment it's actually turned on — this caught 5
   separate bugs in the 2026-08 merge's Kconfig-completeness pass alone.
   Toggle via the project's kconfiglib convention, not by hand-editing
   `.config`:
   ```sh
   CONFIG_= python3 -c "
   import kconfiglib
   kconf = kconfiglib.Kconfig('Kconfig')
   kconf.load_config('.config')
   kconf.syms['CONF_WITH_FOO'].set_value(2)   # 2 == 'y' for a bool
   kconf.write_config('.config')
   "
   ```
   The `CONFIG_=` (empty, exported) env var must match `tools/kconfig.mk`'s
   convention exactly — get it wrong (e.g. `CONFIG_=CONFIG_`) and
   kconfiglib silently falls back to Kconfig defaults for *everything*,
   including machine selection, corrupting `.config` in a way that's easy
   to miss until the next build picks the wrong target.

   Don't stop at "compiles with the option on" — where practical, also test
   *interactions*: a feature can compile fine alone on one target and break
   only when a related option is off, or only on a different machine than
   the one you happened to toggle it on. Exhaustively testing every
   combination isn't practical by hand; this is exactly what §7's full CI
   matrix exists to backstop, so treat local Kconfig toggling as a targeted
   spot-check, not a substitute for CI going green.
3. Boot-test under the emulator per `ptos-smoketest`, both with and without
   the newly-touched options enabled. Run the regression suite
   (`make test-hd`) — it exercises real GEMDOS/BDOS file I/O in a way a
   bare boot to desktop does not, and is what caught the `ixread()` bug.
4. **Verify non-ROM build products too, not just the kernel image.** A boot
   test only exercises what the running OS image does — it says nothing
   about host-side generator tools (`tools/bug.c`, `tools/erd.c`,
   `tools/draft*.c`), generated resources (`.rsc`/`.def` files), the NLS
   catalog build (`./bug xgettext && ./bug make`, checking for
   "N untranslated entries" warnings), or the release/archive tooling
   (`release.mk`). These have their own compile/link steps (the host gcc,
   not the cross-compiler) and their own failure modes that a clean kernel
   boot can't reveal — PR #264's `Release archives` CI job failed for the
   same shared-file root cause as the kernel builds, but would have needed
   a separate check to catch locally.
5. Diff compiler warnings against the pre-merge baseline, not just errors.
   A merge that introduces macro-redefinition warnings, new `-Wundef` hits,
   implicit-function-declaration warnings, or incompatible-pointer/function-type
   warnings is showing you evidence of exactly the failure modes this skill
   describes — often *before* they escalate to a build-breaking error on
   some other config the warning's config doesn't hit. Don't wait for CI to
   turn a warning into an error on a target you haven't tried yet; grep the
   build log for these classes proactively once conflicts are resolved.
6. Use `gmake`, not `make`, throughout (`make` on macOS is BSD make 3.81;
   the Kconfig tooling requires GNU make 4.3+).

## 9. Closing upstream-port tracking issues

If individual GitHub issues tracked specific features to port from
upstream (e.g. "Port EmuDesk printer icon support"), a bulk merge like this
one satisfies many of them at once — but **don't close an issue merely
because the corresponding upstream commit is now somewhere in `git log`.**
Verify, for each candidate issue, that the feature is actually present in
HEAD (not one of the §6b/§6c casualties), has a real Kconfig entry if it's
meant to be optional, and is actually wired into the relevant dispatch
table/menu/build list — not just compiled-in-but-unreachable. This is the
same verification standard as the rest of this skill, applied per-issue: a
grep confirming the feature's key symbol/Kconfig option exists and is
referenced from a real call site, not just "the commit is in history."

Two PR #264 issues were deliberately *not* closed despite matching commits
being present in the merged history: one where pTOS had restructured the
target function enough since the fork point that upstream's specific
optimization commit couldn't apply as-is (needs a manual, targeted port,
not something a bulk merge picks up), and one where the underlying data
(translation catalogs) came through but the issue's own acceptance
criterion ("warning-free") wasn't actually met yet. "The commit merged" and
"the issue is resolved" are different questions — always check the second
one directly.

## 10. Worked examples: 2026-08 merge (pTOS `78308b8b` + upstream `edf307a6`, PR #264)

Concrete reference for what this actually catches, and how each class of
bug presented. The "Found via" column matters: several of these were
invisible to everything *except* the full CI matrix (§7) — a reminder that
local ARM-only testing, however thorough, is not sufficient on its own.

| Bug | File(s) | Category | Found via | Symptom before fix |
|---|---|---|---|---|
| `SUBALIGN(2)` overriding ARM section alignment | `emutos.ld` | §4 (linker) | Local ARM boot | Misaligned symbols, undefined-instruction traps |
| `raspi_screen_init()` running after `linea_init()` needed it | `bios/screen.c` | §4 (ordering) | Local ARM boot | Blank/corrupted screen init |
| `VEC_GEM` left unset on ARM (merge unconditionally set `vditrap`, m68k-only) | `bios/bios.c`, `bios/vectors.h` | Conflict-resolution regression | Local ARM boot | "Exception number 28" panic on every VDI call |
| MBR/GPT parsing using `swpl()` instead of `le2cpu32()` | `bios/disk.c` | §4 (endian) | Local ARM boot | Partition start/size corrupted by exactly 256× |
| `malloc_align_stram` 2-byte-aligning ST-RAM on ARM | `bdos/umem.c` | §4 (alignment) | Local ARM boot | Alignment data abort deep in desktop icon setup — looked like an unrelated crash far from the cause |
| `ixread(fd,len,NULL)` losing its "return pointer" contract | `bdos/fsio.c` | §4 (primitive-rewrite contract) | Local ARM boot | Every `Fopen()` on every drive returned file-not-found; `EMUDESK.INF` silently failed to load |
| `country.mk`: `FONTOBJ_L9` removal applied but `i18n_es_cset` not updated to match | `country.mk` | Partial merge resolution (two paired changes, one side dropped) | Local ARM build (default config) | Default build failed to link (`undefined reference to _fnt_l9_6x6`) — this broke *every* default defconfig, not an edge case |
| 17 `CONF_WITH_*` macros with zero Kconfig stanza | various | §6a | Targeted audit | Silent, no build error — features permanently compiled out |
| `CONF_WITH_EJECT` menu dispatch deleted | `desk/deskmain.c`, `tools/draftexc.c` | §6b / §3 | Targeted audit | Kconfig option existed (after the §6a fix) but did nothing — no code left to enable |
| `CONF_WITH_GRAF_MOUSE_EXTENSION` (`PD.p_mouse`, `gr_mouse()` save/restore) deleted | `aes/struct.h`, `aes/gemgrlib.c` | §6b / §3 | Targeted audit | Same as above |
| `CONF_WITH_EXTENDED_OBJECTS` + `CONF_WITH_ALT_DESKTOP_GRAPHICS` deleted, zero Kconfig entry at all | `aes/gemoblib.c`, `desk/deskmain.c` | §6b / §3 | Targeted audit | §6a's code-driven search couldn't even find these — zero remaining references |
| Dropped `#include "asm.h"` (`disable_interrupts()`/`enable_interrupts()` no prototype in scope) | `aes/geminput.c` | §6c | Targeted audit | `-Wimplicit-function-declaration`, not build-breaking but silently wrong |
| `bios/arch/m68k/startup.S` included `"../obj/header.h"` (upstream's path) but the Makefile still generates `bios/header.h` (pTOS's path) | `bios/arch/m68k/startup.S` | Mismatched pairing (code from one side, build rule from the other) | **CI only** (full m68k matrix) | Every m68k config failed at the very first compiled object |
| Two complete, divergent implementations of `_cache_exists`/`_set_cache`/`_get_cache` concatenated, plus a duplicated `cacr_enable` declaration and a dead unreachable code fragment | `bios/arch/m68k/processor.S` | §3 (both-sides-kept duplication) | **CI only** | Assembler: `symbol 'ce_exit' is already defined` |
| `bios/virtio_input.c` used `kbdvecs` (via `ikbd.h`'s `call_mousevec` macro) without including `bios.h`; `fs/pfs.c` used `run` without including `bdos/bdosstub.h` | `bios/virtio_input.c`, `fs/pfs.c` | §6c (missing include, sibling file had the correct pattern) | **CI only** (virt-arm/virt-m68k) | `'kbdvecs' undeclared`, `'run' undeclared` |
| `raspi_vl805.c` used `kprint.h`'s `RESTRICT`-using prototypes without including `portab.h` (a sibling file built one line earlier does) | `bios/machine/raspi/raspi_vl805.c` | Missing include, pre-existing (not merge-caused) but only reachable on `rpi4` | **CI only** (rpi4) | `expected ';', ',' or ')' before 'fmt'` |
| `bios/serport.c` grew ~300 lines upstream adding shared IOREC helper functions, defined *after* pTOS's own per-port wrapper functions that call them | `bios/serport.c` | Merge reordering (pTOS code positioned before upstream's new helpers it depends on) | **CI only** (any m68k serial backend) | `conflicting types`/`static declaration follows non-static declaration`, implicit declarations |
| `user_wheel`/`old_statvec` regressed from pTOS's properly-typed function pointers to generic `PFVOID`; `wheel_int()` redeclared locally with the wrong signature; two `static` functions' forward declarations missing `static` | `vdi/vdi_mouse.c` | §6c (type regression) + missing `static` | **CI only** (any `CONF_WITH_EXTENDED_MOUSE` m68k config) | `conflicting types for 'wheel_int'`, `static declaration follows non-static declaration` |
| Duplicate `CTRL_ARROW_LEFT`/`CTRL_ARROW_RIGHT` `#define`s (identical values) | `include/scancode.h` | §3 (both-sides-kept duplication) | GitHub Copilot PR review | Macro-redefinition warning |
| Duplicate `desktop_set_cache()` — pTOS's correct `LONG`-returning version (matching `Supexec()`'s `PFLONG` callback contract, with an explaining comment) alongside upstream's `void`-returning duplicate | `desk/deskmain.c` | §3 (both-sides-kept duplication) | **CI only** (proactive scan after the 3rd instance of this pattern) | `conflicting types for 'desktop_set_cache'` |
| `line_a_vars` (upstream's pointer-typed global) used bare in new code instead of pTOS's `linea_vars` (a struct instance, needing `&`) | `bios/natfeats.c` | Naming/type mismatch in genuinely-new upstream code | **CI only** | `'line_a_vars' undeclared; did you mean 'linea_vars'?` |
| Entire pTOS-only m68k routines (`justretf`, `aestrap_intercepted`) present on pTOS's pre-merge tip, absent from the merge result; only the ARM implementations of the same symbols survived | `aes/arch/m68k/gemdosif.S` | §6c (whole-routine loss, not caught by the hunk-diff audit — see §6c's note on checking full symbol lists) | **CI only** (m68k link stage) | `undefined reference to '_justretf'` |

Takeaways that generalize:

- **A "the build broke" bug and a "this silently does nothing" bug need
  different detection strategies.** The country.mk bug was loud (nothing
  linked); the Kconfig/deleted-block rows were completely silent (clean
  build, clean boot, feature just isn't there). Don't let a clean build
  convince you the merge is complete.
- **When two related lines change together upstream, verify both sides
  landed.** `country.mk`'s bug was exactly this: one half of a paired
  change (`FONTOBJ_L9` removal) merged cleanly, the other half
  (`i18n_es_cset` value) didn't get touched and was left stale.
- **A rewritten low-level primitive can break every caller identically** —
  the `ixread()` bug looked at first like "every file lookup is broken,"
  which is a much scarier-sounding bug than its actual one-line root cause.
  Trust the "root cause it" instinct over the instinct to treat a
  widespread symptom as a widespread problem.
- **Once you've found this failure mode once in a merge, scan for it
  proactively rather than waiting for each instance to surface one CI run
  at a time.** The "both sides kept" duplication pattern hit five different
  files; the third and later instances were found by a targeted grep
  (§3's scan commands) run *after* recognizing the pattern from the first
  two, not by waiting for CI to find each one in turn.
- **A hunk-level diff audit (§6b/§6c) cannot see a symbol that never
  appears in a shared file's diff at all** — `justretf`/`aestrap_intercepted`
  lived entirely inside a file where the *rest* of the content merged fine;
  only cross-checking the full `.globl`/function symbol list against the
  pre-merge tip caught the gap, and even that happened reactively (via a
  CI link error) rather than proactively. If a §6c audit pass has budget
  left, a full symbol-list diff (not just a hunk diff) on every conflicted
  `.S` file is worth the extra step.
