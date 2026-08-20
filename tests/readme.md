# pTOS regression tests

A small, platform-independent regression test harness. Test suites are
plain C, run inside a real (emulated) pTOS boot, and are cross-compiled
for every architecture pTOS targets — the same test source runs unmodified
on m68k and ARM.

## Enabling and building

The harness is off by default. Turn it on with `make menuconfig`, under
**Debugging → Regression tests → Include built-in regression tests**
(`CONF_WITH_REGRESSION_TESTS`), or by editing `.config` directly:

```sh
sed -i 's/# CONF_WITH_REGRESSION_TESTS is not set/CONF_WITH_REGRESSION_TESTS=y/' .config
```

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
  carrying `runtests.tos`, `tests/emudesk.inf`, and the contents of
  `tests/destdata/` (see [Packaged test data](#packaged-test-data) below).

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

On Hatari (m68k), attach it as an IDE disk and use `--conout 2` to capture
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
`FAIL: <message>` detail line, and the harness exits via `Pterm(0)` with a
return code equal to the number of failed tests. A Data Abort or
`guest_errors` output *before* the summary line means the harness itself
crashed — a real bug, not a test assertion failure.

## Adding a test suite

Each suite is a single C file at `tests/<name>/<name>.c` defining exactly
one entry point, `void test_<name>(void)`. The Makefile auto-discovers
every `tests/*/*.c` file and wires its `test_<name>()` into the generated
`tests/run_tests.c` — nothing needs registering by hand.

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
`portab.h` types (`ULONG`, `BOOL`, etc.) or `-mshort`/`-fleading-underscore`
conventions — use plain C types and libcmini's headers (`<mint/osbind.h>`
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

Files a test needs on disk at runtime go under `tests/destdata/`, mirroring
the layout they should have on the built image's `C:` drive — e.g.
`tests/destdata/TESTS/` ships as `C:\TESTS\` (used by `stack_alignment` to
exercise `Fsfirst()` on a subdirectory). Add files there if a new suite
needs fixtures; an empty directory needs a placeholder file (`.keep`) since
git can't track empty directories.

## Multiple suites

Nothing here is unique to `stack_alignment` — add as many
`tests/<name>/<name>.c` files as needed and they all run in one pass,
reported individually and summarized together.
