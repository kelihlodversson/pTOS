---
name: toolchain-install
description: >-
  Install and select the packaged pTOS SDKs on Debian/Ubuntu or macOS.
  Use before skipping a configuration or reporting a missing compiler,
  unsupported -mfastcall, PIC, x32 or PE32+ linker error. Covers the signed
  apt.ptos.dev APT repository, the pTOS Homebrew tap, compiler overrides,
  and validation of the actual installed SDKs.
---

# pTOS toolchain installation

Use SDK 1.0.0 or newer, which fixes the m68k auto-inc-dec miscompilation.
The installer is shared with
`.github/actions/setup-toolchain/action.yml`:

```sh
bash tools/install-ptos-toolchain.sh all
```

Pass `m68k`, `arm` or `x86_64` to install only one SDK. It uses APT on
Debian/Ubuntu and Homebrew on macOS. It installs SDKs only; also install GNU
Make 4.3+, a native C compiler, Python 3 and `kconfiglib==14.1.0`.

| Target | APT package | Homebrew formula | Default compiler prefix |
| --- | --- | --- | --- |
| Atari, Amiga, ColdFire, virt-m68k | `ptos-sdk-m68k` | `ptos-m68k` | `m68k-unknown-ptos-prgelf-` |
| Raspberry Pi, virt-arm | `ptos-sdk-arm` | `ptos-arm` | `arm-unknown-ptos-elf-` |
| PC UEFI kernel and x32 applications | `ptos-sdk-x86-64` | `ptos-x86_64` | `x86_64-unknown-ptos-elfx32-` |

## Debian/Ubuntu APT

Published host packages are amd64 and are validated on Ubuntu 24.04 and
26.04. Other Debian/Ubuntu releases need an actual installation/build check;
do not assume their runtime libraries are compatible.

The installer configures the signed repository. Equivalent manual commands:

```sh
sudo apt-get update
sudo apt-get install -y ca-certificates curl
curl -fsSL https://apt.ptos.dev/ptos-archive-keyring.gpg |
    sudo tee /usr/share/keyrings/ptos-archive-keyring.gpg >/dev/null
printf '%s\n' 'deb [arch=amd64 signed-by=/usr/share/keyrings/ptos-archive-keyring.gpg] https://apt.ptos.dev stable main' |
    sudo tee /etc/apt/sources.list.d/ptos.list >/dev/null
sudo apt-get update
sudo apt-get install -y ptos-sdk-m68k ptos-sdk-arm ptos-sdk-x86-64
sudo apt-get install -y make gcc binutils python3-venv zip dos2unix
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install kconfiglib==14.1.0
```

Install the individual SDKs rather than requiring the convenience metapackage:
`ptos-toolchain` was added after the initial v0.1.0 release. All three SDKs
coexist under `/opt/ptos`, with their prefixed commands exposed in `/usr/bin`.
There is no tarball overlay or PATH precedence between two compilers with the
same name, and no separate Linux m68k compiler is needed for PIC.

### Pinned GitHub Releases in Linux CI

CI downloads the `.deb` files from GitHub Releases, validates every package
against `tools/ptos-toolchain.sha256`, then installs the local files with APT:

```sh
bash tools/install-ptos-toolchain.sh all --release
```

The manifest pins the release version and each package checksum. Update them
together when upgrading CI; do not select `latest` or fetch runtime checksums
from the same download. GitHub Releases retain older versions independently
of the limited APT repository storage. Local developers may use this same
reproducible path or the current packages from apt.ptos.dev above.
The explicit release option may downgrade an already newer SDK to restore
the pin; this permission applies only to the verified local SDK package install.

For an environment without permission to install system packages, use an
isolated Ubuntu 24.04/26.04 container and run this same installer there. Keep
build commands under the ordinary user. Consult the managed environment's
package/network/container instructions before installing anything. If the
repository is blocked, request access and continue independent work; official
release `.deb` files can also be verified with their `SHA256SUMS` and installed
with `apt-get install ./package.deb`. Report which installation path was tested.
Do not disable APT signature checks or TLS verification.

## macOS Homebrew

Install Apple's Command Line Tools and Homebrew first. Then:

```sh
brew install make coreutils dos2unix python
bash tools/install-ptos-toolchain.sh all
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install kconfiglib==14.1.0
```

The installer taps
`kelihlodversson/ptos-toolchain` from
`https://github.com/kelihlodversson/pTOS-toolchain.git`, trusts the individual
formulae when Homebrew provides `brew trust`, then installs them normally.
Homebrew downloads the published SDK bottles from GitHub Releases; APT's
`apt.ptos.dev` endpoint is not a Homebrew package index. Published bottles
currently cover Apple Silicon macOS 26 (Tahoe) under `/opt/homebrew`.
Other host/CPU combinations may build from source; do not claim they were
validated by the macOS 26 CI job.

Use `gmake` because Apple's `/usr/bin/make` is too old. If a build tool needs
GNU coreutils, prepend `$(brew --prefix coreutils)/libexec/gnubin` to PATH.

## Build and verify

Check the actual tools before building:

```sh
m68k-unknown-ptos-prgelf-gcc -dumpmachine
arm-unknown-ptos-elf-gcc -dumpmachine
x86_64-unknown-ptos-elfx32-gcc -dumpmachine
m68k-unknown-ptos-prgelf-gcc --version
x86_64-unknown-ptos-elfx32-ld -V
```

Each `-dumpmachine` must match the prefix without its trailing dash. The m68k
SDK supports `-mfastcall` and ordinary GOT/PLT PIC. Its default is plain ELF;
the makefiles select `-mmintelf` when linking conventional TOS PRG programs.
The PC SDK supports both the LP64 no-red-zone kernel and x32 applications,
including the `i386pep` linker emulation needed by UEFI.

```sh
make distclean
make virt-m68k_defconfig
make -j4
# Or rpi2_defconfig / pc-x86_64_defconfig; use gmake on macOS.
```

Do not clear `CROSS_COMPILE` for ordinary PC builds: the pTOS SDK is now the
default on both hosts. `CROSS_COMPILE`, `X32_CROSS_COMPILE` and
`LDSO_M68K_PREFIX` remain explicit overrides. Legacy MiNT and bare m68k-elf
choices remain available in Kconfig; select the matching choice and flags
when deliberately using them. Do not silently substitute a legacy toolchain
when validating this migration.

Run `make distclean` when changing toolchains, and record compiler/package
versions with the results. A missing compiler is a setup problem: fix the
installation, then retry the original build. Only report a configuration as
unbuildable after the appropriate installation or build has actually failed.

## m68k compiler regression

SDK 1.0.0 fixes [toolchain issue #15](https://github.com/kelihlodversson/pTOS-toolchain/issues/15).
pTOS uses the normal auto-inc-dec optimization again, including LTO and native
libraries/programs. Do not reintroduce the global disabling flag. The SDK's
installed smoke tests include assembly checks for the address-register
dependency; Linux can also execute its bundled regression using qemu-m68k.
Verify runtime boot and address-space tests when upgrading the compiler.
