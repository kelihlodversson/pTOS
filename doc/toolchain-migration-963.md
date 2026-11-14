# Packaged toolchain migration validation (#963)

pTOS defaults to the m68k, ARM and x32 SDKs. SDK 1.0.0 contains GCC 16.2.0,
Binutils 2.45 and the fix for the m68k MOVE address-dependency bug reported in
[pTOS-toolchain#15](https://github.com/kelihlodversson/pTOS-toolchain/issues/15).
The integration includes the completed explicit AES context refactor on master.

## Installation and version pinning

Linux CI calls `tools/install-ptos-toolchain.sh ARCH --release`. The manifest
`tools/ptos-toolchain.sha256` pins version 1.0.0 and the SHA-256 of each official
GitHub Release `.deb`. Every selected file must have a checksum and match it
before APT installs any SDK package. Installed versions must equal the pin.
This does not require historical packages to remain on apt.ptos.dev.

Local developers may select the same release or install current packages from
the signed apt.ptos.dev repository. macOS uses the Homebrew tap and published
bottles on Apple Silicon macOS 26; its SDK smoke tests validate the compiler fix.
Compiler/package versions and final CI results are recorded in the PR.

## Compiler fix and performance comparison

The fixed m68k backend rejects a MOVE whose auto-increment or auto-decrement
register also contributes to its other operand's address. Valid auto-inc-dec
instructions remain enabled. The global `-fno-auto-inc-dec` flag has been
removed from the kernel, LTO links, libcmini, programs, runtime linker and
fixtures. The installed SDK regression explicitly enables the optimization.

Small Amiga ROMs still need LTO with the new compiler: the 256 KiB Vampire
image exceeds its limit by 2,150 bytes without it, while the LTO build fits
with 8,348 bytes free. Their LTO defaults therefore remain enabled independently
of the removed auto-inc-dec workaround.

The temporary SDK 0.2.0 workaround and its replacement pBench references are
removed. CI compares the fixed compiler against the original committed
references, with the existing 3% mean and 15% primitive thresholds. No benchmark
threshold has been relaxed and no slower baseline is retained from the paused
migration. See the PR's current pBench result for the actual measured comparison.

## Local validation

- Actual APT installation of all three checksum-verified 1.0.0 release SDKs in
  Ubuntu 24.04 through the shared `--release` installer: passed. The managed
  environment still blocks apt.ptos.dev, so its repository path was not used.
- Installed SDK smoke tests for m68k, ARM and x32: passed as an ordinary user.
- Installed m68k assembly and QEMU runtime checks with auto-inc-dec enabled:
  passed across 68000/68020/68040, -Os/-O2 and stack/fastcall, covering 96 copies.
- Full m68k address-space QEMU regression without the disabling flag: 25 suites
  passed, zero failed; five shutdown checks and scalable-font replay passed.
- ELF metadata/admission suite using real SDK objects: 53 passed.
- 256 KiB Vampire ROM with LTO: passed with 8,348 bytes free.
- Host CUIS/GEM suite, including concurrent contexts and non-recovering
  ASan/UBSan: passed.
- Installer selection/integrity fixtures: six passed, including corrupt files,
  missing checksums, unsupported hosts and installed-version mismatch.

Kernel builds and QEMU system runs use the same verified SDK payload relocated
under the managed Debian workspace. CI additionally covers the full build and
release matrix, ARM/m68k regression suites, native ELF/libaes/VDI and desktop
consumers, Hatari, Amiberry, pBench and macOS Homebrew builds. Final results and
any additional local runs are linked from the PR.
