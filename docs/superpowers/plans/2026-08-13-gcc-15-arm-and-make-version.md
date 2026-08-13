# GCC 15 ARM Support and GNU Make Version Guard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make ARM images build with Arm GNU Toolchain 15 and fail immediately with a clear error when GNU Make is older than 4.3.

**Architecture:** Add a parse-time GNU Make version check near the top of the root Makefile, before grouped-target syntax is read. Make `util/string.c` emit the existing external `strcpy()` implementation for ARM only, so GCC 15 ARMv6's synthesized `_strcpy` reference has a provider while call sites retain their static inline implementation. Cover the Make guard with a standalone shell test using a temporary Makefile copy, then build `rpi1` and `rpi2` with the local GCC 15 toolchain.

**Tech Stack:** GNU Make 4.3+, POSIX shell, Arm GNU Toolchain 15.3.1, freestanding GNU C90.

## Global Constraints

- Require GNU Make 4.3 or later; 4.2.x and earlier must fail during Makefile parsing.
- The error must identify the detected Make version and direct macOS users to use Homebrew `gmake`.
- Emit the existing `strcpy()` implementation in `util/string.c` for ARM only;
  do not disable GCC builtins or optimization passes.
- Do not alter m68k or ColdFire compiler flags.
- Validate local GCC 15 builds for `rpi1` and `rpi2` using `gmake`.
- Run `gmake gitready` before completion.

---

### Task 1: Test the GNU Make Version Guard

**Files:**
- Create: `tools/test-make-version.sh`
- Modify: `Makefile:24` (temporary test input only; no production change in this task)

**Interfaces:**
- Consumes: root `Makefile` and the `MAKE_VERSION` variable supplied by GNU Make.
- Produces: executable regression test which exits 0 only when unsupported versions produce the required diagnostic and GNU Make 4.3 is accepted.

- [ ] **Step 1: Create the failing regression test**

Create `tools/test-make-version.sh` with this test harness. It copies only the root Makefile because both tested invocations must stop during parsing, before includes or build targets are evaluated.

```sh
#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tmpdir=${TMPDIR:-/tmp}/ptos-make-version-test.$$

cleanup()
{
    rm -rf "$tmpdir"
}

trap cleanup EXIT HUP INT TERM

mkdir -p "$tmpdir"
cp "$repo_root/Makefile" "$tmpdir/Makefile"

cd "$tmpdir"

if make -f Makefile MAKE_VERSION=4.2 help >make-4.2.log 2>&1; then
    echo 'Makefile accepted GNU Make 4.2'
    exit 1
fi

if ! grep -F 'GNU Make 4.3 or later is required' make-4.2.log >/dev/null; then
    cat make-4.2.log
    echo 'GNU Make 4.2 did not report the required version diagnostic'
    exit 1
fi

if make -f Makefile MAKE_VERSION=4.3 help >make-4.3.log 2>&1; then
    echo 'GNU Make 4.3 unexpectedly completed the incomplete temporary build'
    exit 1
fi

if grep -F 'GNU Make 4.3 or later is required' make-4.3.log >/dev/null; then
    cat make-4.3.log
    echo 'GNU Make 4.3 was rejected by the version guard'
    exit 1
fi

echo 'GNU Make version test passed'
```

- [ ] **Step 2: Run the test and verify it fails for the missing guard**

Run: `sh tools/test-make-version.sh`

Expected: FAIL with `GNU Make 4.2 did not report the required version diagnostic` because the root Makefile has no explicit version validation yet.

- [ ] **Step 3: Add the parse-time version check**

Immediately before `MAKEFLAGS = --no-print-directory` in `Makefile`, add this comparison. It explicitly accepts 4.3 and newer 4.x releases, as well as every future major release; it therefore accepts 4.10 correctly.

```make
# Grouped targets require GNU Make 4.3.  Apple still ships GNU Make 3.81 as
# /usr/bin/make, so reject it before make parses any grouped-target rules.
MAKE_MAJOR = $(word 1,$(subst ., ,$(MAKE_VERSION)))
MAKE_MINOR = $(word 2,$(subst ., ,$(MAKE_VERSION)))
ifneq (,$(filter 0 1 2 3,$(MAKE_MAJOR)))
$(error GNU Make 4.3 or later is required (found $(MAKE_VERSION)); use Homebrew gmake on macOS)
endif
ifeq (4,$(MAKE_MAJOR))
ifneq (,$(filter 0 1 2,$(MAKE_MINOR)))
$(error GNU Make 4.3 or later is required (found $(MAKE_VERSION)); use Homebrew gmake on macOS)
endif
endif
```

Preserve these exact acceptance cases: reject 3.81, 4.0, 4.2; accept 4.3, 4.4, and 4.10. Do not use Bash-only syntax or a non-portable version-sorting utility.

- [ ] **Step 4: Run the regression test and verify it passes**

Run: `sh tools/test-make-version.sh`

Expected: PASS with `GNU Make version test passed`.

- [ ] **Step 5: Check the real local Make diagnostics**

Run: `make help`

Expected: exit nonzero and one clear error containing `GNU Make 4.3 or later is required (found 3.81)` and `gmake`.

Run: `gmake help`

Expected: exit zero and list pTOS configurations.

- [ ] **Step 6: Commit the guarded Makefile and regression test**

```sh
git add Makefile tools/test-make-version.sh
git commit -m "Require GNU Make 4.3"
```

### Task 2: Provide ARM `strcpy()` for GCC 15

**Files:**
- Modify: `util/string.c:18-31`

**Interfaces:**
- Consumes: `ARCH_ARM`, `USE_STATIC_INLINES`, `include/string.h`, and the existing
  `strcpy()` implementation in `util/string.c`.
- Produces: an ARM global `_strcpy` definition for GCC 15-generated references;
  m68k and ColdFire preprocessing and symbols remain unchanged.

- [ ] **Step 1: Create the failing compiler-behavior check**

From a clean `rpi1` configuration, build until the linker fails and capture the missing symbol:

```sh
gmake distclean
gmake rpi1_defconfig
if gmake >gcc15-before.log 2>&1; then
    echo 'GCC 15 rpi1 build unexpectedly passed before the compatibility flag'
    exit 1
fi
grep -F "undefined reference to \`_strcpy'" gcc15-before.log
```

Expected: the `grep` succeeds, proving the regression is the GCC 15 `_strcpy` link failure rather than a generator or configuration failure. If `bug translate all` crashes first, rerun only after it completes successfully; do not treat that unrelated host-tool crash as evidence for this task.

- [ ] **Step 2: Emit the existing implementation for ARM only**

Immediately after `#include "config.h"` and before `#include "string.h"` in
`util/string.c`, add:

```c
#ifdef ARCH_ARM
#undef USE_STATIC_INLINES
#define USE_STATIC_INLINES 0
#endif
```

This makes the existing `#if !(USE_STATIC_INLINES)` block compile the external
`strcpy()` implementation only in the ARM `util/string.c` translation unit.
Do not change the function body, `include/string.h`, compiler flags, or m68k
and ColdFire behavior.

- [ ] **Step 3: Verify the focused regression is fixed**

Run:

```sh
gmake distclean
gmake rpi1_defconfig
gmake
```

Expected: exit zero and `# kernel.img is ready`. Confirm
`arm-none-eabi-nm emutos.elf` contains a `T _strcpy` definition and no undefined
`_strcpy` symbol.

- [ ] **Step 4: Validate the requested ARM configurations**

Run:

```sh
gmake distclean
gmake rpi1_defconfig
gmake
test -f kernel.img
gmake distclean
gmake rpi2_defconfig
gmake
test -f kernel7.img
```

Expected: both builds exit zero; `kernel.img` exists for `rpi1` and `kernel7.img` exists for `rpi2`.

- [ ] **Step 5: Run repository formatting checks**

Run: `gmake gitready`

Expected: exit zero with `gitready checks passed.`

- [ ] **Step 6: Commit GCC 15 compatibility support**

```sh
git add util/string.c
git commit -m "Support GCC 15 ARM builds"
```

### Task 3: Document the Supported Local Build Invocation

**Files:**
- Modify: `doc/install.txt:51-55`

**Interfaces:**
- Consumes: the GNU Make 4.3 version guard and macOS Homebrew convention.
- Produces: installation documentation that directs macOS users to `gmake` when the system `make` is too old.

- [ ] **Step 1: Write the documentation expectation check**

Run:

```sh
grep -F 'GNU make 4.3 or later' doc/install.txt
grep -F 'gmake' doc/install.txt
```

Expected: the first command succeeds and the second fails because the current documentation does not name the Homebrew command.

- [ ] **Step 2: Update the GNU Make requirement text**

Replace the existing GNU Make bullet with:

```text
  * GNU make 4.3 or later (grouped targets are used; make 4.2.x will not work).
    macOS ships GNU Make 3.81 as /usr/bin/make; install GNU make with Homebrew
    and run gmake instead.
```

- [ ] **Step 3: Verify the documentation check**

Run:

```sh
grep -F 'GNU make 4.3 or later' doc/install.txt
grep -F 'gmake' doc/install.txt
```

Expected: both commands succeed.

- [ ] **Step 4: Re-run the complete focused verification**

Run:

```sh
sh tools/test-make-version.sh
gmake distclean
gmake rpi1_defconfig
gmake
test -f kernel.img
gmake distclean
gmake rpi2_defconfig
gmake
test -f kernel7.img
gmake gitready
```

Expected: every command exits zero.

- [ ] **Step 5: Commit the documentation**

```sh
git add doc/install.txt
git commit -m "Document GNU Make requirement on macOS"
```
