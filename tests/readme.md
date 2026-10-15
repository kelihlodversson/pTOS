# pTOS regression tests

A small, platform-independent regression test harness. Test suites are
plain C, run inside a real (emulated) pTOS boot, and are cross-compiled
for every architecture pTOS targets — the same test source runs unmodified
on m68k, ARM and x86-64 (as x32 programs).

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

`make test-dir` collects the same files (`runtests.tos`, `emudesk.inf`, the
probe programs and `TESTS/`) into a plain `test-dir/` directory instead of
an image. That needs neither `mkfs.fat` nor mtools, and lets QEMU's virtual
FAT driver present the directory as the disk (see [Running](#running)).

On x86-64 the programs are built as x32 programs (libcmini's
`ptos_x86_64_defconfig`, with the compiler and flags of the other x32
programs). There is no disk image target for x86-64: use `make test-dir`.

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

From a host directory, without building an image (`make test-dir` first).
raspi2b wants a power-of-two SD card size, so give vvfat a smaller raw
window; virt-arm takes it over virtio-blk as it is:

```sh
qemu-system-arm -M raspi2b -bios kernel7.img \
  -drive if=sd,driver=raw,size=256M,file.driver=vvfat,file.dir=test-dir,file.rw=on \
  -d guest_errors -serial stdio

qemu-system-arm -M virt,highmem=off -cpu cortex-a7 -m 128 -kernel virt-arm.elf \
  -global virtio-mmio.force-legacy=false \
  -drive if=none,id=hd0,format=raw,file.driver=vvfat,file.dir=test-dir,file.rw=on \
  -device virtio-blk-device,drive=hd0 -d guest_errors -serial stdio -display none
```

On x86-64 (QEMU with OVMF), copy `test-dir`'s contents into the directory
that holds `EFI/BOOT/BOOTX64.EFI` and is attached as a FAT drive, boot, and
type `runtests.tos` at the EmuCON prompt — there is no autorun.

On Hatari (m68k), attach it as an ACSI disk and use `--conout 2` to capture
the console text (a VT-52 terminal channel) directly to Hatari's own
stdout:

```sh
hatari --tos ptos512k.img --machine ste --memsize 4 --sound off \
  --acsi test-hd.img --conout 2 --run-vbls 1200
```

CI runs the suite on three machines (the `regression` job in
`.github/workflows/build.yml`) with `tools/run-regression-tests.sh
<virt-arm|virt-m68k|pc-x86_64>`, which also works locally once the matching
configuration (`virt-arm-cli`, `virt-m68k-cli-elf` (the m68k ELF loader is an option, and three suites need it) or `pc-x86_64`) is built and
`make test-dir` has run. The virt machines are the `-cli` configurations so
that they boot to EmuCON, where the script types `runtests.tos` into the
serial console; the script ends QEMU when the harness has printed its
verdict and fails unless that is `All tests passed.` After a pass it starts
`poweroff.tos` (`Shutdown(SHUT_POWER)`, doc/shutdown.txt) and expects QEMU to
switch off by itself.

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
currently configured (`ARCH_ARM`/`ARCH_M68K`/`ARCH_X86_64`), so avoid anything
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
