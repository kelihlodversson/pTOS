# GCC 15 ARM Support and GNU Make Version Guard

## Goal

Build ARM images with Arm GNU Toolchain 15.3 while rejecting unsupported GNU
Make versions before the build evaluates grouped-target rules.

## Root Cause

ARM configurations enable `USE_STATIC_INLINES`, so `include/string.h` provides
`strcpy()` as a `static __inline__` function and `util/string.c` does not emit
an external implementation. Arm GNU Toolchain 15.3 replaces a call site with
an external `_strcpy` call, leaving an unresolved symbol at link time. The CI
toolchain is GCC 13.2.1 and does not exhibit this behavior.

The Makefile uses GNU Make grouped targets (`&:`), which require GNU Make 4.3.
macOS supplies GNU Make 3.81 as `/usr/bin/make`; Homebrew supplies a compatible
version as `gmake`.

## Design

### GNU Make Guard

At the beginning of `Makefile`, before grouped-target syntax can be evaluated,
compare `MAKE_VERSION` with 4.3. If it is older, stop with a clear error that
states the installed version, requires GNU Make 4.3 or later, and directs macOS
users to run Homebrew `gmake`.

The guard accepts 4.3 and every later release. It does not depend on external
host utilities so an unsupported invocation fails deterministically during
Makefile parsing.

### GCC 15 Compatibility

Emit the existing `util/string.c` `strcpy()` implementation for ARM while
retaining the static inline implementation at call sites. GCC 15.3 recognizes
the inline loop and rewrites it into an external `__builtin_strcpy` call. On
ARMv7 it inlines that builtin, but on ARMv6 it leaves an unresolved `_strcpy`
reference. `-fno-builtin-strcpy`, `always_inline`, and
`-fno-tree-loop-distribute-patterns` do not reliably prevent that late
transformation.

`util/string.c` will locally override `USE_STATIC_INLINES` to 0 only when
`ARCH_ARM` is defined, allowing it to supply `_strcpy`. Other ARM translation
units continue to use the header's static inline implementation. m68k and
ColdFire retain their existing behavior.

No m68k or ColdFire flags change.

### Tests

Add a small host-shell regression test that runs Make with overridden
`MAKE_VERSION` values. It verifies that 4.2 reports the compatibility error,
while 4.3 is accepted far enough to report the expected unconfigured-build
error instead of the version error.

Verify with the installed Arm GNU Toolchain 15.3.1:

```sh
gmake distclean
gmake rpi1_defconfig
gmake
gmake rpi2_defconfig
gmake
```

Each build must produce its expected kernel image. Run `gmake gitready` before
completion.

## Scope

This work supports GCC 15 for ARM builds and validates the local `rpi1` and
`rpi2` configurations. The existing GitHub Actions matrix continues to validate
all ARM configurations with Ubuntu's GCC 13.2.1 package.
