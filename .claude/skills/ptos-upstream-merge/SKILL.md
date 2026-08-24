---
name: ptos-upstream-merge
description: Use when merging upstream EmuTOS into pTOS, resolving the conflicts from that merge, or auditing a completed (or previously completed) merge for correctness. Covers the pre-merge worktree baseline, the specific way conflict resolution silently drops code guarded by not-yet-Kconfig'd macros, the two mandatory post-merge audits (unconfigured macros, deleted blocks) with their exact commands, known m68k-only idioms that break on ARM, and a worked-example table of every bug found in the 2026-08 merge (pTOS 78308b8b + upstream edf307a6).
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

## 2. The core trap: conflict resolution deletes code it thinks is dead

**A code block guarded by `#if CONF_WITH_X` or `#ifdef X`, where `X` isn't
defined anywhere yet (no Kconfig entry exists at merge time), does not
compile to nothing — a human resolving the conflict by hand can easily read
it as dead/unreachable code and delete it**, rather than leaving it intact
for a later Kconfig pass to wire up. This happened four times in the 2026-08
merge (see §5) and is invisible to compilation, because deleted code and
never-configured code look identical at build time — both are just absent
from the binary. Nothing errors. Nothing warns. The only way to catch it is
the diff-based audit in §4b, run *after* the merge, against upstream's tip.

**Rule while resolving conflicts:** never delete a hunk just because its
guard macro looks unfamiliar or undefined. If a conflict is genuinely
unresolvable by inspection (upstream's version and pTOS's version both
changed the same lines for different reasons), keep both, wrap them
distinctly, and flag it — don't pick one side and silently drop the other.
Whether the macro deserves a Kconfig entry is a separate, later decision;
deleting the code forecloses that decision instead of deferring it.

## 3. Known upstream idioms that break on ARM

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

## 4. Two mandatory post-merge audits

Do these *after* conflicts are resolved and the tree builds, before
considering the merge done. They catch different things and neither
substitutes for the other.

### 4a. Unconfigured-macro audit

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

### 4b. Deleted-block audit

Finds code that's actually *gone*, not just unconfigured — this is what
catches the §2 failure mode, and it will find things §4a cannot, because a
macro with zero remaining references anywhere doesn't show up in a
code-driven search at all.

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

Also specifically re-diff occurrence *counts* for every macro §4a already
flagged as a gap, upstream vs. HEAD (`git grep -c CONF_WITH_X <rev>` on
each). If upstream has more occurrences across more files than HEAD, even
though the macro now has *some* live `#if` (which is why §4a found it at
all), there may be a second or third guarded block in another file that
never made it across.

## 5. Worked examples: 2026-08 merge (pTOS `78308b8b` + upstream `edf307a6`)

Concrete reference for what this actually catches, and how each class of
bug presented:

| Bug | File(s) | Category | Symptom before fix |
|---|---|---|---|
| `SUBALIGN(2)` overriding ARM section alignment | `emutos.ld` | §3 (linker) | Misaligned symbols, undefined-instruction traps |
| `raspi_screen_init()` running after `linea_init()` needed it | `bios/screen.c` | §3 (ordering) | Blank/corrupted screen init |
| `VEC_GEM` left unset on ARM (merge unconditionally set `vditrap`, m68k-only) | `bios/bios.c`, `bios/vectors.h` | Conflict-resolution regression | "Exception number 28" panic on every VDI call |
| MBR/GPT parsing using `swpl()` instead of `le2cpu32()` | `bios/disk.c` | §3 (endian) | Partition start/size corrupted by exactly 256× |
| `malloc_align_stram` 2-byte-aligning ST-RAM on ARM | `bdos/umem.c` | §3 (alignment) | Alignment data abort deep in desktop icon setup — looked like an unrelated crash far from the cause |
| `ixread(fd,len,NULL)` losing its "return pointer" contract | `bdos/fsio.c` | §3 (primitive-rewrite contract) | Every `Fopen()` on every drive returned file-not-found; `EMUDESK.INF` silently failed to load |
| `country.mk`: `FONTOBJ_L9` removal applied but `i18n_es_cset` not updated to match | `country.mk` | Partial merge resolution (two paired changes, one side dropped) | Default build failed to link (`undefined reference to _fnt_l9_6x6`) — this broke *every* default defconfig, not an edge case |
| 17 `CONF_WITH_*` macros with zero Kconfig stanza | various | §4a | Silent, no build error — features permanently compiled out |
| `CONF_WITH_EJECT` menu dispatch deleted | `desk/deskmain.c`, `tools/draftexc.c` | §4b / §2 | Kconfig option existed (after the §4a fix) but did nothing — no code left to enable |
| `CONF_WITH_GRAF_MOUSE_EXTENSION` (`PD.p_mouse`, `gr_mouse()` save/restore) deleted | `aes/struct.h`, `aes/gemgrlib.c` | §4b / §2 | Same as above |
| `CONF_WITH_EXTENDED_OBJECTS` + `CONF_WITH_ALT_DESKTOP_GRAPHICS` deleted, zero Kconfig entry at all | `aes/gemoblib.c`, `desk/deskmain.c` | §4b / §2 | §4a's code-driven search couldn't even find these — zero remaining references |

Takeaways that generalize:

- **A "the build broke" bug and a "this silently does nothing" bug need
  different detection strategies.** The country.mk bug was loud (nothing
  linked); the four §4b deletions were completely silent (clean build,
  clean boot, feature just isn't there). Don't let a clean build convince
  you the merge is complete.
- **When two related lines change together upstream, verify both sides
  landed.** `country.mk`'s bug was exactly this: one half of a paired
  change (`FONTOBJ_L9` removal) merged cleanly, the other half
  (`i18n_es_cset` value) didn't get touched and was left stale.
- **A rewritten low-level primitive can break every caller identically** —
  the `ixread()` bug looked at first like "every file lookup is broken,"
  which is a much scarier-sounding bug than its actual one-line root cause.
  Trust the "root cause it" instinct over the instinct to treat a
  widespread symptom as a widespread problem.

## 6. Verification workflow

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
3. Boot-test under the emulator per `ptos-smoketest`, both with and without
   the newly-touched options enabled. Run the regression suite
   (`make test-hd`) — it exercises real GEMDOS/BDOS file I/O in a way a
   bare boot to desktop does not, and is what caught the `ixread()` bug.
4. Use `gmake`, not `make`, throughout (`make` on macOS is BSD make 3.81;
   the Kconfig tooling requires GNU make 4.3+).
