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
The explicit release install permits downgrades to restore the pin when a
developer already has a newer SDK; ordinary repository installs are unaffected.
This does not require historical packages to remain on apt.ptos.dev.

Local developers may select the same release or install current packages from
the signed apt.ptos.dev repository. macOS uses the Homebrew tap and published
bottles on Apple Silicon macOS 26; CI pins the tap commit recorded in the same
manifest and disables auto-updates for the remaining job. This selects the matching
release's bottle declarations. Its smoke tests validate the compiler fix.
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
removed. The fixed compiler passed against the previous committed references.
The new SDK 1.0.0 references come from the complete green
[pBench run 43602387432](https://github.com/kelihlodversson/pTOS/actions/runs/43602387432),
source head `f741c117078b954ab9583d39afbea1057c4d85fb`, measured merge image
`3c0222aa`. All 20 mode/blitter configurations and their 200 measurements use
Ubuntu 26.04's Hatari 2.6.1. Test names, workload counts, screen geometry and
emulator settings are unchanged. The 3% mean and 15% primitive thresholds
remain unchanged as well.

The following comparison retains the change from the previous references;
negative values mean slower. Future changes compare against the new fixed-SDK
measurements, not the removed workaround's results.

| Mode | Display geometric mean | Slowest primitive change |
| --- | ---: | ---: |
| `atari256-high-blitter` | +1.46% | -3.55% |
| `atari256-high-noblitter` | +1.44% | -3.59% |
| `atari256-low-blitter` | +1.19% | -5.09% |
| `atari256-low-noblitter` | +2.36% | -2.54% |
| `atari256-med-blitter` | +0.95% | -6.81% |
| `atari256-med-noblitter` | +1.58% | -4.35% |
| `atari512-f030-blitter` | -1.26% | -6.23% |
| `atari512-f030-noblitter` | -1.41% | -5.79% |
| `atari512-f040-blitter` | +0.80% | -4.87% |
| `atari512-f040-noblitter` | +1.32% | -5.36% |
| `atari512-fastcall-f030-blitter` | -0.70% | -6.41% |
| `atari512-fastcall-f030-noblitter` | -2.50% | -9.65% |
| `atari512-fastcall-f040-blitter` | -0.15% | -4.97% |
| `atari512-fastcall-f040-noblitter` | +0.03% | -8.46% |
| `atari512-high-blitter` | -1.54% | -3.63% |
| `atari512-high-noblitter` | -1.38% | -3.63% |
| `atari512-low-blitter` | -2.37% | -8.16% |
| `atari512-low-noblitter` | -1.42% | -4.53% |
| `atari512-med-blitter` | -2.15% | -8.21% |
| `atari512-med-noblitter` | -1.62% | -5.43% |

## Local validation

- Actual APT installation of all three checksum-verified 1.0.0 release SDKs in
  Ubuntu 24.04 through the shared `--release` installer: passed. The managed
  environment still blocks apt.ptos.dev, so its repository path was not used.
- Installed SDK smoke tests for m68k, ARM and x32: passed as an ordinary user.
- Installed m68k assembly and QEMU runtime checks with auto-inc-dec enabled:
  passed across 68000/68020/68040, -Os/-O2 and stack/fastcall, covering 96 copies.
- Full m68k address-space QEMU regression without the disabling flag: 25 suites
  passed, zero failed; five shutdown checks and scalable-font replay passed.
- Full ARM address-space QEMU regression: 26 suites passed, zero failed; five
  shutdown checks and scalable-font replay passed.
- Native x32 interactive ELF/libaes/VDI/callback/mapping boot and AES ELF audit:
  passed with the SDK's default prefixes, without native-compiler overrides.
- ELF metadata/admission suite using real SDK objects: 53 passed.
- 256 KiB Vampire ROM with LTO: passed with 8,348 bytes free.
- Host CUIS/GEM suite, including concurrent contexts and non-recovering
  ASan/UBSan: passed.
- Installer selection/integrity fixtures: eight passed, including a complete
  release/tap pin, corrupt files, missing checksums, unsupported hosts,
  installed-version mismatch and
  restoration of the pin over a newer installation.

Kernel builds and QEMU system runs use the same verified SDK payload relocated
under the managed Debian workspace. CI additionally covers the full build and
release matrix, ARM/m68k regression suites, native ELF/libaes/VDI and desktop
consumers, Hatari, Amiberry, pBench and macOS Homebrew builds. Final results and
any additional local runs are linked from the PR.

## SDK 1.0.3 and optional m68k LRA

CI now pins SDK 1.0.3's Debian packages and matching tested Homebrew tap
commit in `tools/ptos-toolchain.sha256`. The installer requires SDK 1.0.3
or newer, including the m68k LRA address-displacement compiler fix.

The Toolchain menu exposes `BUILD_M68K_LRA` separately from the CPU flags.
It defaults to `y` for the standard pTOS m68k toolchain when the installed
compiler reports GCC 16 or newer and compiles a small `-mlra` probe. It
defaults to `n` with a missing/older compiler, another selected toolchain
or a nonstandard prefix. Such installations can opt in manually. The
option is unavailable on ARM and x86-64.

`BUILD_M68K_LRA=y` appends `-mlra` to the effective m68k `CPUFLAGS`, including
command-line CPU overrides, so kernel, libcmini, test programs, standalone
utilities and LTO links agree. `BUILD_M68K_LRA=n` leaves the compiler's
allocator default in effect. Clean before rebuilding after changing it.
Prebuilt SDK libraries such as libgcc retain their original allocator.
