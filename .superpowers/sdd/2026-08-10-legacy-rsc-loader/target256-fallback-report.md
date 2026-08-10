# TARGET_256 Legacy RSC Loader Fallback Report

## Change

`CONF_WITH_LEGACY_RSC_LOAD` now defaults to `y` for both `TARGET_192` and
`TARGET_256`.  No loader implementation code changed.  The Kconfig help and
legacy-loader design document now state that constrained 192 KB and 256 KB ROM
targets use the legacy loader by default, while larger and default targets use
the portable loader.

## Baseline

Command:

```sh
make release-256k
```

Before the change, the release build failed while building French:

```
./mkrom: emutos.img is too big: 283 extra bytes
make[1]: *** [Makefile:450: ptos256fr.img] Error 1
make: *** [release.mk:90: release-256k] Error 1
```

The same local baseline reported 237 bytes free for `ptos256de.img`.  These
measurements differ from the CI figures in the request (409 bytes over for
French and 111 bytes free for German), so this report records the reproducible
local values from the current checkout.

## Post-Change Release Build

Command:

```sh
make release-256k
```

All 256 KB language images built successfully.  Relevant image results:

```
# ptos256de.img done (6309 bytes free)
# ptos256de.img is ready
# ptos256fr.img done (5789 bytes free)
# ptos256fr.img is ready
```

After all image builds, the release command exited nonzero at packaging because
the existing release rule uses `mkdir $(RELEASE_256K)` and the untracked
`release-archives/` parent directory was absent:

```
mkdir release-archives/ptos-256k-20260810
mkdir: No such file or directory
make: *** [release.mk:91: release-256k] Error 1
```

This is unrelated to the image size or loader policy, so `release.mk` was not
changed.

## Portable-Path Verification

Commands:

```sh
make atari512_defconfig
make obj/autoconf.h
python3 -c 'from pathlib import Path; header = Path("obj/autoconf.h").read_text(); assert "#define CONF_WITH_LEGACY_RSC_LOAD 0" in header, header'
make

make rpi1_defconfig
make obj/autoconf.h
python3 -c 'from pathlib import Path; header = Path("obj/autoconf.h").read_text(); assert "#define CONF_WITH_LEGACY_RSC_LOAD 0" in header, header'
make
```

Both generated configurations selected portable loading and built successfully:

```
# ptos512k.img is ready
# kernel.img is ready
```

`make atari512_defconfig` and `make rpi1_defconfig` update `.config` only;
explicitly regenerating `obj/autoconf.h` was necessary before each assertion.

## Review Follow-Up

The original 256 KB Kconfig help incorrectly said that the whole 256 KB ROM
budget could not accommodate the portable loader.  The baseline measurements
showed that the U.S. and German portable images fit, while French overflowed.
The help and design specification now state that the legacy default ensures
every 256 KB language variant, including French, fits the ROM budget.  This is
a documentation-only correction; the Kconfig default and loader implementation
are unchanged.
