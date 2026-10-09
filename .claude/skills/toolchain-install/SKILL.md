---
name: toolchain-install
description: Install and select pTOS cross toolchains on Ubuntu Intel or macOS. Use whenever a user asks how to build pTOS, install its ARM or m68k toolchain, configure x86-32/x86-64 builds, or troubleshoot missing cross compilers.
---

# pTOS Toolchain Installation

Use the toolchain that matches the selected pTOS configuration. Verify the
compiler prefix before building; a successful host compiler installation is
not enough if Make is still selecting a different prefix.

## Ubuntu Intel

### x86-32 / x32

Use the native Ubuntu compiler and binutils. Install the packages used by CI:

```sh
sudo apt-get update
sudo apt-get install -y gcc binutils gcc-multilib
```

The pTOS x86-64 kernel and x32 userland build must explicitly clear the cross
compiler setting:

```sh
make pc-x86_64_defconfig
make CROSS_COMPILE=
```

The variable is `CROSS_COMPILE`, not `CROSS_PREFIX`. An empty
`CROSS_COMPILE=` is required for this Ubuntu-native build.

### ARM

Install the package used by the CI workflow:

```sh
sudo apt-get update
sudo apt-get install -y gcc-arm-none-eabi
```

Then use the normal configuration build:

```sh
make rpi2_defconfig
make
```

### m68k

Download the latest `gcc-mintelf` and binutils toolchain from:

<https://tho-otto.m68k.eu/crossmint.php>

Use the `mintelf` toolchain, not the plain `mint`/cross-mint toolchain. The
compiler must support `-mfastcall`. At the time this skill was written, the
latest available GCC version on that page was `15.2.0`.

Verify the installed tools before building:

```sh
m68k-atari-mintelf-gcc --version
m68k-atari-mintelf-gcc -Q --help=target | grep mfastcall
m68k-atari-mintelf-ld --version
```

Use the `m68k-atari-mintelf-` prefix, either through the configured toolchain
choice or explicitly:

```sh
make atari512_defconfig
make CROSS_COMPILE=m68k-atari-mintelf-
```

Do not substitute `m68k-atari-mint-`; that is the plain mint/a.out toolchain
and is not the required ELF/mfastcall-capable toolchain for these builds.

## macOS

### x86-32 / x32

Install the bare-metal x86-64 compiler and binutils from Homebrew:

```sh
brew install x86_64-elf-gcc x86_64-elf-binutils
```

Build with the pTOS x86-64 default prefix:

```sh
make pc-x86_64_defconfig
make
```

If Make is configured with another prefix, override it explicitly:

```sh
make CROSS_COMPILE=x86_64-elf-
```

### ARM

Install the Homebrew ARM embedded toolchain:

```sh
brew install gcc-arm-embedded
```

Build normally after selecting an ARM configuration:

```sh
make rpi2_defconfig
make
```

### m68k

Download the latest `gcc-mintelf` and binutils toolchain from:

<https://tho-otto.m68k.eu/crossmint.php>

Use the `mintelf` variant, not plain `mint`, and choose the latest GCC release
available there. The current expected version is `15.2.0`. The compiler must
support `-mfastcall`.

Verify and use the `m68k-atari-mintelf-` prefix:

```sh
m68k-atari-mintelf-gcc --version
m68k-atari-mintelf-gcc -Q --help=target | grep mfastcall
m68k-atari-mintelf-ld --version
make atari512_defconfig
make CROSS_COMPILE=m68k-atari-mintelf-
```

## Troubleshooting

- `x86_64-elf-gcc: command not found` on Ubuntu usually means the build was not invoked with `CROSS_COMPILE=`.
- `m68k-atari-mint-gcc` is the wrong prefix for the required mintelf ELF toolchain.
- A m68k compiler without `-mfastcall` is not suitable; install the mintelf package from the tho-otto page instead.
- Confirm the selected prefix with `make V=1` when diagnosing a build.
