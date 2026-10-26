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

The separate `make vdi-contract-test` target checks the typed x32 VDI
layouts, opcode inventory and memory raster semantics on the host. The
same contract tests run in ring 3 during the PC boot self-test.
`pc-x86_64-vdi_defconfig` also exercises the callable VDI client, drawing
with pixel readback, memory rasters, callback refusal, handle ownership and
fault cleanup. Copy `obj/x32/x32probe.elf` to `X32VDI.TOS` in the EFI boot
drive alongside the existing x32 loader fixtures; CI does this for both PC
configurations. The enabled configuration must print `x86-64 VDI client:
PASS`, and both must print `x86-64 memtest: PASS`. See
[the VDI contract](../doc/vdi-x32.txt) for the supported calls and bounds.
The VDI configuration also requires `x86-64 VDI padded pitch: PASS`: it
opens a workstation with a narrower viewport over the real GOP pitch and
checks that console clearing and ring-3 drawing preserve the sentinel
pixels in every row's padding.
`x86-64 VDI coordinate bounds: PASS` checks simulated screen heights of
32768, 32769 and 65535 rows: signed WORD bounds before narrowing, 64-bit
pixel arguments, unchanged output buffers on rejection, and visible
low-coordinate lines with optional clipping disabled and enabled.

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
that holds `EFI/BOOT/BOOTX64.EFI` and is attached as a FAT drive. Build
`pc-x86_64-desktop_defconfig` and keep `EMUDESK.PRG` and `EMUDESK.INF` from
`test-dir` on that drive: the ring-3 desktop autoruns `RUNTESTS.TOS` through
the same `#Z` line as on ARM and m68k. The console-only `pc-x86_64_defconfig`
also permits manual launch from EmuCON when iterating locally.

On Hatari (m68k), attach it as an ACSI disk and use `--conout 2` to capture
the console text (a VT-52 terminal channel) directly to Hatari's own
stdout:

```sh
hatari --tos ptos512k.img --machine ste --memsize 4 --sound off \
  --acsi test-hd.img --conout 2 --run-vbls 1200
```

CI runs the suite on nine machines (the `regression` job in
`.github/workflows/build.yml`: virt-arm, virt-m68k, virt-m68k with
`-mfastcall`, virt-m68k-mmu, virt-m68k-aspace, virt-arm-aspace, raspi2b-aspace, pc-x86_64 and raspi2b) with `tools/run-regression-tests.sh
<virt-arm|virt-m68k|virt-m68k-mmu|virt-m68k-aspace|raspi2b-aspace|pc-x86_64|raspi2b>`, which also works locally once the matching
configuration (`virt-arm-cli`, `virt-m68k-cli-elf` (the m68k ELF loader is an option, and three suites need it), `virt-m68k-mmu` (the same with the higher-half kernel, on a 68040), `virt-m68k-aspace` (and `virt-arm-aspace`, `rpi2-aspace`: every process in an address space of its own; the suites that look at isolation are gated on `TEST_WITH_USER_ASPACE`), `pc-x86_64-desktop` or `rpi2-test` (the Pi 2 with the console on the UART as well: its desktop starts the suite from `emudesk.inf`, so there are no shutdown checks)) is built and
`make test-dir` has run. The virt machines are the `-cli` configurations so
that they boot to EmuCON, where the script types `runtests.tos` into the
serial console; the script ends QEMU when the harness has printed its
verdict and fails unless that is `All tests passed.` After a pass it starts
`poweroff.tos` (`Shutdown(SHUT_POWER)`, doc/shutdown.txt) and expects QEMU to
switch off by itself. On x32 the script starts the suite and each shutdown
probe in a fresh desktop boot using INF autorun; it sends no launch keystrokes.
Reboot and cold-boot checks require a second EFI entry, and the shutdown
probes must still flush their open data file. Set `OVMF_CODE` and `OVMF_VARS`
to firmware files when they are outside `/usr/share/OVMF`.

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
compiled in as a `test_<name>()` entry point.  `tests/isolation/` does the
same with `isoprobe.c` (`ISOPROBE.TOS`), which makes the forbidden accesses
one launch at a time; the suite is built only where `CONF_WITH_USER_ASPACE`
is set on ARM and m68k.

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

## Suites that need AES/VDI

`tools/pBench/gem.c` (the minimal AES/VDI bindings of pBench, see
`doc/pbench.txt`) is linked into `runtests.tos` and `tools/pBench` is on the
include path of every suite, so a suite can `#include "gem.h"` and call
`appl_init()`, `v_opnvwk()` and the VDI text and drawing calls without
anything else. Bindings that a suite needs and `gem.h` lacks belong there.

### text_blit

`text_blit` checks VDI text output on Atari ST/STE style bitplane screens
(1, 2 or 4 planes, 320 or 640 pixels wide; also the 640x480 with 16 colours
of a Falcon). It is part of the blitter text
epic (#498), and is skipped, and reported as `SKIP` with the screen it found,
on every other machine: without a blitter (`Blitmode()` says so), on ARM and on
screens that are not bitplane screens it has nothing to compare.

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

### raster_copy

`raster_copy` checks `vro_cpyfm()` copies between two areas of the screen on
Atari ST/STE style bitplane screens (1, 2 or 4 planes, 320 or 640 pixels
wide), and is skipped, as `text_blit` is, without a blitter, on ARM and on
other screens. It is part of the blitter text epic (#498), issue #503.

With the blitter disabled the copy is done by the assembler `fast_bit_blt()`,
with it enabled by `bit_blt()` and the blitter. The test makes the same copies
through both on a band of the screen (restored afterwards), and compares the
screen memory: all 16 writing modes, widths from 1 to 70 pixels, heights of 1
and 7 lines, three alignments of the source and four of the destination, and
destinations that are far from the source, overlap it on the same lines, or
one line above or below it (so that the blit goes in either direction), with
and without a clip rectangle. Mode 5 leaves the destination as it is: the
blitter is checked against the band as it was, and the CPU path against the
blitter's.

### On an emulated Falcon

Both suites run on a Falcon in Hatari, where the CPU can have a data cache
(`--machine falcon --cpulevel 3` or `4`, `--memsize 14`, `--dsp none`, the
test disk as `--ide-master`), in the screen mode pTOS starts in (640x480 with
16 colours), and pass with the 68030 and with the 68040. As a control, with
the blitter code's cache calls (`BLITTER_CACHE_OPS` in `vdi/vdi_raster.c`)
turned off, `text_blit` fails with the 68040 (1552 of the 7488 justified text
cases, for one) and still passes with the 68030: Hatari shows the missing
flush and invalidate of the 68040's data cache, but not that of the 68030's
(so the 68030 code is only checked for not crashing here).

The workflow `.github/workflows/hatari-tests.yml` does this on every pull
request to `master` or an epic branch (`tools/run-hatari-tests.py` runs one
machine): the ROM image of `atari512` and of `atari512-fastcall` (the same
built with `-mfastcall`, so that the code is run under that calling convention
too) on an emulated STE in ST-LOW, ST-MEDIUM and ST-HIGH, and on a Falcon with
a 68030 and with a 68040, both with the MMU emulated (`--mmu on`: the page
tables pTOS builds for the caches are walked, not only written; the 68030 and
68040 address-space work of #571 is tested the same way). It fails if a test
fails, and also if `text_blit` or
`raster_copy` skip themselves, as they have to run on every one of these.
The `falcon-mmu` image (#726: the Falcon ROM that runs in the upper half, with
the 68030 MMU on from its first instruction) is run on the 68030 only.

`make x32-vdi-import-test` builds the native VDI import applications and
malformed metadata fixtures in `obj/x32/VDI*.TOS`. Copy those files to the PC
boot drive alongside `X32VDI.TOS`. Both PC configurations require
`x86-64 VDI imports: PASS`: the optional VDI configuration executes imported
client calls through normal and relocated ELF loads, nested Pexec and
PE_LOAD/GO; the baseline refuses the unavailable import. Both reject
malformed metadata. See `doc/vdi-x32.txt` for the SDK link recipe.

`make aes-contract-test` checks standalone AES layouts, shared rectangle
helpers and all 79 legacy AES operation classifications. `make
x32-aes-import-test` builds mixed AES/VDI imported applications and malformed
fixtures in `obj/x32/AES*.TOS`. Copy these to the PC boot drive and copy the
probe to `X32AES.TOS` as well as `X32VDI.TOS`. The optional
`pc-x86_64-aes_defconfig` boot checks multi-client registration, isolated
messaging/events, authoritative sender ids, recursive update ownership and
explicit/normal/fault-exit cleanup. Baseline and VDI-only boots refuse the
optional AES service/imports. CI requires both `x86-64 AES client: PASS` and
`x86-64 AES imports: PASS`; see `doc/aes-x32.txt` for the full contract.
