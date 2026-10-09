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

This configuration is supplied by the x32 configuration work; use a checkout
that contains `configs/pc-x86_64_defconfig` before running these commands.

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

The recommended Ubuntu installation is the same PPA/package path used by CI:

```sh
sudo add-apt-repository -y ppa:vriviere/mintelf
sudo apt-get update
sudo apt-get install -y cross-mintelf-essential
```

If using archives from the tho-otto page instead, download these three
matching Linux 64-bit archives from the latest toolchain section:

- `binutils-2.45-mintelf-20250812-bin-linux64.tar.xz`
- `gcc-15.2.0-mintelf-20250810-bin-linux64.tar.xz`
- `mintlib-0.60.1-mintelf-20240718-dev.tar.xz`

The binutils and GCC archives share the filesystem root and are intended to be
merged. The mintlib development archive supplies target headers such as
`stdint.h`, which the compiler archive does not contain. Their contents include
`/usr/bin/m68k-atari-mintelf-*`,
`/usr/m68k-atari-mintelf/`, and the GCC runtime under `/usr/lib64/gcc/`.
Extract and validate all three archives as an ordinary user first. Their
`usr/m68k-atari-mintelf/sys-root` paths already match the intended destination,
so no component stripping is needed. Do not extract unverified archives as
root:

```sh
stage=$(mktemp -d)
for archive in \
    binutils-2.45-mintelf-20250812-bin-linux64.tar.xz \
    gcc-15.2.0-mintelf-20250810-bin-linux64.tar.xz \
    mintlib-0.60.1-mintelf-20240718-dev.tar.xz; do
    while IFS= read -r path; do
        case "$path" in
            usr/*) ;;
            *) echo "unexpected archive path: $path" >&2; exit 1 ;;
        esac
    done < <(tar -tJf "$archive")
    tar -xJf "$archive" -C "$stage"
done
# Compare `shasum -a 256 <archive>` with a trusted release record before this step.
sudo cp -a "$stage/usr/." /usr/
rm -rf "$stage"
```

The tools are installed in `/usr/bin`, so no PATH change is normally needed.
Verify the merged installation:

<https://tho-otto.m68k.eu/crossmint.php>

```sh
m68k-atari-mintelf-gcc --version
m68k-atari-mintelf-gcc -Q --help=target | grep mfastcall
m68k-atari-mintelf-ld --version
```

The pTOS m68k toolchain choice automatically selects the
`m68k-atari-mintelf-` prefix and the required compiler flags. No explicit
`CROSS_COMPILE` override is normally required:

```sh
make atari512_defconfig
make
```

Use `CROSS_COMPILE=m68k-atari-mintelf-` only when overriding the configured
prefix or diagnosing tool lookup:

```sh
make CROSS_COMPILE=m68k-atari-mintelf-
```

Do not substitute `m68k-atari-mint-`; that is the plain mint/a.out toolchain
and is not the required ELF/mfastcall-capable toolchain for these builds.

## macOS

Install GNU Make 4.3 or newer. Apple’s system `/usr/bin/make` is GNU Make
3.81, which is too old for pTOS; Homebrew installs a newer GNU Make as `gmake`:

```sh
brew install make
```

### x86-32 / x32

Install the bare-metal x86-64 compiler and binutils from Homebrew:

```sh
brew install x86_64-elf-gcc x86_64-elf-binutils
```

Build with the pTOS x86-64 default prefix:

```sh
gmake pc-x86_64_defconfig
gmake
```

If Make is configured with another prefix, override it explicitly:

```sh
gmake CROSS_COMPILE=x86_64-elf-
```

### ARM

Install the Homebrew ARM embedded toolchain:

```sh
brew install gcc-arm-embedded
```

Build normally after selecting an ARM configuration:

```sh
gmake rpi2_defconfig
gmake
```

### m68k

Download these matching macOS archives from the latest toolchain section at
the tho-otto page:

- `binutils-2.45-mintelf-20250812-bin-macos.tar.xz`
- `gcc-15.2.0-mintelf-20250810-bin-macos.tar.xz`
- `mintlib-0.60.1-mintelf-20240718-dev.tar.xz`

<https://tho-otto.m68k.eu/crossmint.php>

The binutils and GCC archives are designed to be extracted together and both
contain an `opt/cross-mint/` root. The mintlib archive contains a separate
`usr/m68k-atari-mintelf/sys-root/` root:

- Binutils provides `/opt/cross-mint/bin/m68k-atari-mintelf-*` and target linker files.
- GCC provides the matching compiler drivers, runtime, sysroot, and `mfastcall` multilibs.
- Mintlib provides target headers such as `stdint.h` and the development libraries.

Extract and validate the archives as an ordinary user first. Do not extract
unverified downloads as root:

```sh
stage=$(mktemp -d)
for archive in \
    binutils-2.45-mintelf-20250812-bin-macos.tar.xz \
    gcc-15.2.0-mintelf-20250810-bin-macos.tar.xz \
    mintlib-0.60.1-mintelf-20240718-dev.tar.xz; do
    while IFS= read -r path; do
        case "$archive:$path" in
            *mintlib*:usr/m68k-atari-mintelf/sys-root/*) ;;
            *:opt/cross-mint/*) ;;
            *) echo "unexpected archive path: $path" >&2; exit 1 ;;
        esac
    done < <(tar -tJf "$archive")
    tar -xJf "$archive" -C "$stage"
done
# Compare `shasum -a 256 <archive>` with a trusted release record before this step.
sudo mkdir -p /opt/cross-mint/m68k-atari-mintelf/sys-root
sudo cp -a "$stage/opt/cross-mint/." /opt/cross-mint/
sudo cp -a "$stage/usr/m68k-atari-mintelf/sys-root/." \
    /opt/cross-mint/m68k-atari-mintelf/sys-root/
rm -rf "$stage"
export PATH=/opt/cross-mint/bin:$PATH
```

Use the matching `mintelf` archives, not the plain `mint` archives. The compiler must
support `-mfastcall`.

The pTOS m68k toolchain choice automatically selects the
`m68k-atari-mintelf-` prefix and required flags. Verify the tools, then build
normally:

```sh
m68k-atari-mintelf-gcc --version
m68k-atari-mintelf-gcc -Q --help=target | grep mfastcall
m68k-atari-mintelf-ld --version
gmake atari512_defconfig
gmake
```

Only override the prefix explicitly when needed:

```sh
gmake CROSS_COMPILE=m68k-atari-mintelf-
```

## Troubleshooting

- `x86_64-elf-gcc: command not found` on Ubuntu usually means the build was not invoked with `CROSS_COMPILE=`.
- `m68k-atari-mint-gcc` is the wrong prefix for the required mintelf ELF toolchain.
- A m68k compiler without `-mfastcall` is not suitable; install the mintelf package from the tho-otto page instead.
- Confirm the selected prefix with `make V=1` on Ubuntu or `gmake V=1` on macOS when diagnosing a build.
