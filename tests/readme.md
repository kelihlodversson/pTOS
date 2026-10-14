# pTOS regression tests

A small, platform-independent regression test harness. Test suites are
plain C, run inside a real (emulated) pTOS boot, and are cross-compiled
for every architecture pTOS targets — the same test source runs unmodified
on m68k and ARM.

## Building

The harness is enabled by default (`CONF_WITH_REGRESSION_TESTS`). To omit it
from a configuration, disable **Debugging → Regression tests → Include
built-in regression tests** in `make menuconfig` (or `make guiconfig`).

Build the kernel as usual, then build the test image:

```sh
make rpi2_defconfig && make   # or any other config
make test-hd
```

`make test-hd` produces two things:

- `runtests.tos` — a standalone program linked against
  [libcmini](https://github.com/KeliHlodversson/libcmini) (the `lib/libcmini`
  submodule), containing every enabled test suite.
- `test-hd.img` — a raw disk image (MBR + FAT16, built by `tools/mkhdisk.sh`)
  carrying `runtests.tos`, `tests/emudesk.inf`, and `tests/destdata/TESTS/`
  (see [Packaged test data](#packaged-test-data) below).

`lib/libcmini` is a git submodule. If it's missing, `make test-hd` fails
with an actionable error — run `git submodule update --init` and retry.

## Running

`tests/emudesk.inf`'s `#Z` autorun line launches `C:\RUNTESTS.TOS`
automatically as soon as the desktop reaches it, so booting `test-hd.img`
runs every suite with no interaction needed. It can also be launched
manually — from `EmuCON` (`RUNTESTS.TOS`) or by double-clicking it on the
desktop — which is useful when iterating with a non-test disk image already
attached.

On QEMU (ARM):

```sh
qemu-system-arm -M raspi2b -bios kernel7.img \
  -drive file=test-hd.img,format=raw,if=sd \
  -d guest_errors -serial stdio
```

On Hatari (m68k), attach it as an ACSI disk and use `--conout 2` to capture
the console text (a VT-52 terminal channel) directly to Hatari's own
stdout:

```sh
hatari --tos ptos512k.img --machine ste --memsize 4 --sound off \
  --acsi test-hd.img --conout 2 --run-vbls 1200
```

See the `ptos-smoketest` skill (`.claude/skills/ptos-smoketest/SKILL.md`)
for more emulator invocations and gotchas.

## Reading the output

```
pTOS regression tests

  stack_alignment (#214)... PASS

--- Summary: 1 passed, 0 failed, 1 total ---

All tests passed.
```

A failing test prints `FAIL` instead of `PASS`, followed by an indented
`FAIL: <message>` detail line, and the harness exits via
`Pterm(<failure count>)` — 0 when every test passed, so a caller checking
the return value (e.g. via `Pexec()`) can tell success from failure without
parsing the printed text. A Data Abort or `guest_errors` output *before*
the summary line means the harness itself crashed — a real bug, not a test
assertion failure.

## Adding a test suite

Each suite is a single C file at `tests/<name>/<name>.c` defining exactly
one entry point, `void test_<name>(void)`. The Makefile auto-discovers
every `tests/*/` directory and wires the `test_<name>()` from its
`<name>.c` into the generated `tests/run_tests.c` — nothing needs
registering by hand.

A suite's directory can hold other files besides `<name>.c` without them
being treated as suites themselves. `tests/pie_load/`, for instance,
also has `pie_probe.c`: a standalone payload `pie_load.c` launches via
`Pexec()` to test the PIE ELF loader, built and linked separately from
`runtests.tos` (see the Makefile's `pieprobe.tos` rule) rather than
compiled in as a `test_<name>()` entry point.

```c
#include "test.h"

void test_foo(void)
{
    ptest_begin("foo");

    ptest_assert(some_condition);
    ptest_assert_msg(another_condition, "expected 42");

    ptest_pass();
}
```

Or fail immediately from anywhere after `ptest_begin()`:

```c
    if (setup_failed)
        ptest_fail("could not set up fixture");
```

The full API is documented in `tests/test.h`. A suite should follow the
`ptest_begin()` / assert / `ptest_pass()` (or `ptest_fail()`) shape shown
above; `ptest_pass()` must always be reached exactly once per test (calling
it is what finalizes and counts the result).

Test code is userland, built against libcmini rather than the kernel's own
`portab.h` types (`ULONG`, `BOOL`, etc.) or `-mshort` conventions — use plain
C types and libcmini's headers (`<mint/osbind.h>`
for GEMDOS calls). It's cross-compiled for whichever architecture is
currently configured (`ARCH_ARM`/`ARCH_M68K`), so avoid anything
architecture-specific unless the test is deliberately targeted at one (see
`stack_alignment`, which is ARM-only in effect: it exercises a bug that
never existed on m68k).

**Console output must use `\r\n`, not bare `\n`.** `conws()` writes straight
to the BIOS console, which needs an explicit `\r` to return to column 0 —
without it, output silently walks off to the right on a real VT52-style
terminal (it only *looks* fine in a raw serial-log capture, where any
viewer already treats `\n` as a full line break).

## Packaged test data

Files a test needs on disk at runtime go under `tests/destdata/TESTS/`,
which `make test-hd` packages onto the image as `C:\TESTS\` (used by
`stack_alignment` to exercise `Fsfirst()` on a subdirectory) — that one
directory is what `TEST_DESTDIR` in the top-level Makefile currently
points at, not all of `tests/destdata/`, so add fixture files there
rather than in a sibling directory. An empty directory needs a
placeholder file (`.keep`) since git can't track empty directories.

## Multiple suites

Nothing here is unique to `stack_alignment` — add as many
`tests/<name>/<name>.c` files as needed and they all run in one pass,
reported individually and summarized together.

## Suites that need AES/VDI

`tools/pBench/gem.c` (the minimal AES/VDI bindings of pBench, see
`doc/pbench.txt`) is linked into `runtests.tos` and `tools/pBench` is on the
include path of every suite, so a suite can `#include "gem.h"` and call
`appl_init()`, `v_opnvwk()` and the VDI text and drawing calls without
anything else. Bindings that a suite needs and `gem.h` lacks belong there.

### text_blit

`text_blit` checks VDI text output on Atari ST/STE style bitplane screens
(1, 2 or 4 planes, 320 or 640 pixels wide). It is part of the blitter text
epic (#498), and is skipped, and reported as `SKIP`, on every other machine:
without a blitter (`Blitmode()` says so), on ARM and on screens that are not
bitplane screens it has nothing to compare.

It draws strings on a band of the screen (restored afterwards), starting
from a fixed pseudo-random pattern, in three groups:

- plain text (no effects, no rotation) in every system font, writing mode,
  a few colours, 18 horizontal alignments and 8 clip rectangles, compared
  with a model in the test that draws the glyphs straight from the font data;
- text with every combination of the effects (bold, light, skewed, underline,
  outline) and rotation (0, 90, 180, 270 degrees), and
- text with effects under clip rectangles, where the last two groups compare
  the output with the blitter disabled and enabled with `Blitmode()`.

While text output does not use the blitter at all, both renderings come from
the same code and the comparison between them cannot fail; the model is what
is checked then. A blitter implementation of text output has to produce
exactly the pixels of the software one, which is what the comparison
guarantees from then on.

Failures print the first few differing cases (string, position, point size,
effects, writing mode, colour, rotation, clip rectangle and the first byte
that differs). It needs about two minutes under Hatari, which is what to run
it on (`--machine ste`; `--tos-res low|med|high` for the three screen modes):

```sh
make atari512_defconfig && make
make test-hd
hatari --tos ptos512k.img --machine ste --memsize 4 --sound off \
  --fast-forward on --acsi test-hd.img --conout 2
```

Hatari does not exit when the tests are done (the harness returns to the
desktop), so run it under `timeout -s KILL` and read the output.
