---
name: toolchain-install
description: >-
  Install and select pTOS cross toolchains on Ubuntu Intel or macOS,
  including without sudo. Use it BEFORE you report a pTOS configuration as
  unbuildable or skip it for lack of a compiler, whenever a build fails with
  "x86_64-elf-gcc: No such file or directory", "m68k-atari-mintelf-gcc does
  not support -mfastcall", "arm-none-eabi-gcc: not found", "command not
  found", Error 127, or any other missing cross-compiler error. It also
  covers building pc-x86_64 with the native gcc (make CROSS_COMPILE=),
  installing Thorsten Otto's -mfastcall m68k toolchain into a user directory
  for the amiga, amiga-kickdisk and *-fastcall configs, the ARM toolchain,
  and the answer when a user asks how to build pTOS or install its
  toolchains.
---

# pTOS Toolchain Installation

Use the toolchain that matches the selected pTOS configuration. Verify the
compiler prefix before building; a successful host compiler installation is
not enough if Make is still selecting a different prefix.

## Missing a toolchain? Fix it, don't skip the config

Every pTOS configuration can be built on an Ubuntu x86-64 machine, and
nothing below needs sudo unless it says so. Check what the build actually
complains about, then follow the matching row:

| Error | Configs | Fix |
|---|---|---|
| `x86_64-elf-gcc: No such file or directory` / Error 127 | `pc-x86_64` | `make CROSS_COMPILE=`. The host's native gcc builds it, and that is also what CI uses ([x86-32 / x32](#x86-32--x32)) |
| `m68k-atari-mintelf-gcc does not support -mfastcall` | `amiga`, `amiga-kickdisk`, `*-fastcall`, anything with `CONF_WITH_MFASTCALL=y` | Install Thorsten Otto's toolchain into a user directory and put it first on `PATH` ([without sudo](#-mfastcall-toolchain-without-sudo)) |
| `m68k-atari-mintelf-gcc: not found` | every other m68k/ColdFire config | If the PPA is not installed and sudo is unavailable, the user-directory toolchain below builds these too |
| `needs m68k-linux-gnu-gcc` (from lib/ldso/build.mk) | `virt-m68k-aspace`, anything with `CONF_WITH_ELF_SHARED=y` on m68k | `sudo apt-get install gcc-m68k-linux-gnu`. It makes the position-independent code the MiNT compilers cannot. Without sudo: find the versioned names with `apt-cache depends gcc-m68k-linux-gnu` (they are `gcc-<N>-m68k-linux-gnu`, `cpp-<N>-m68k-linux-gnu` and `libgcc-<N>-dev-m68k-cross` for the compiler `<N>` of the release, besides `gcc-m68k-linux-gnu`, `cpp-m68k-linux-gnu` and `binutils-m68k-linux-gnu`), `apt-get download` them and `dpkg -x` each into `~/.local/m68k-linux`, then `PATH=$HOME/.local/m68k-linux/usr/bin:$PATH LD_LIBRARY_PATH=$HOME/.local/m68k-linux/usr/lib/x86_64-linux-gnu` |
| `arm-none-eabi-gcc: not found` | `rpi*`, `virt-arm*` | `sudo apt-get install gcc-arm-none-eabi`. Without sudo, ask the user to run it (`! sudo apt-get install -y gcc-arm-none-eabi`) |

Only after the fix itself has failed should you report a configuration as
unbuildable, and then quote the error.

Several m68k toolchains can be installed side by side. The one that comes
first on `PATH` provides `m68k-atari-mintelf-gcc`, so prepend the Otto
toolchain for each build command that needs it, and keep the PPA toolchain at
`/usr/bin` for the rest:

```sh
PATH=$HOME/.local/th-otto-mintelf/usr/bin:$PATH make amiga_defconfig
PATH=$HOME/.local/th-otto-mintelf/usr/bin:$PATH make
```

Mixing toolchains in one object directory gives different code. Run
`make distclean` when you switch.

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

Vincent Rivière's PPA is the quickest install, though CI no longer uses it (it
uses Thorsten Otto's toolchain, below, for every m68k job):

```sh
sudo add-apt-repository -y ppa:vriviere/mintelf
sudo apt-get update
sudo apt-get install -y cross-mintelf-essential
```

This PPA gcc has **no `-mfastcall`**. It builds every m68k config except
the `CONF_WITH_MFASTCALL=y` ones (amiga, amiga-kickdisk, `*-fastcall`), and
the Makefile stops those with "does not support -mfastcall". CI installs only
Otto's toolchain (the same three archives as below). Do the same
locally with the user-directory install below.

### -mfastcall toolchain without sudo

These are the archives and sha256 pins that
`.github/actions/setup-toolchain/action.yml` uses, plus mintlib for the C
headers, which the gcc archive lacks. All three unpack below `usr/`, so they
extract together into one directory under `$HOME`, and the result survives
from one session to the next. If `$HOME` is not writable from a sandbox, use
the session's scratchpad directory instead.

```sh
dest=$HOME/.local/th-otto-mintelf
# The subshell keeps the cd and any exit from leaving the current shell,
# so the build commands afterwards still run in the pTOS checkout.
(
    dl=$HOME/.local/th-otto-dl
    # Download and extract exactly these files, never a *.tar.xz glob: a
    # leftover archive in the reused download directory has not been checked.
    archives="gcc-14.3.0-mintelf-20250702-bin-linux64.tar.xz
              binutils-2.45-mintelf-20250812-bin-linux64.tar.xz
              mintlib-0.60.1-mintelf-20240718-dev.tar.xz"
    mkdir -p "$dest" "$dl" && cd "$dl" || exit 1
    for f in $archives; do
        curl -sSLO --fail --retry 3 "https://tho-otto.m68k.eu/download/mint/$f" || exit 1
    done
    sha256sum -c - <<'EOF' || exit 1
2c582c04d689e8fb8c3568155246fa8039a320c1fad95a02c228a001f284f57b  gcc-14.3.0-mintelf-20250702-bin-linux64.tar.xz
e37c22f84baaf8ffc890a11265d4369498f9bc4e68aaac21dcef887d3655f025  binutils-2.45-mintelf-20250812-bin-linux64.tar.xz
0a9f4f61c976af26b3b4c2fdfbfcaaa19c671174a6a1c874496375f775ff33ec  mintlib-0.60.1-mintelf-20240718-dev.tar.xz
EOF
    for f in $archives; do tar -xJf "$f" -C "$dest" || exit 1; done
) &&
"$dest/usr/bin/m68k-atari-mintelf-gcc" -Q --help=target | grep mfastcall
```

The last line should print `-mfastcall  [disabled]`. "Disabled" is just the
default, and the pTOS Makefile turns the flag on. Then build with
`PATH=$dest/usr/bin:$PATH make ...` as shown at the top. Before downloading,
check whether `$dest/usr/bin/m68k-atari-mintelf-gcc` is already there.

If a pin fails because the site has published a new build, do not drop the
check. Bump the URL and hash in the CI action and here together.

If using archives from the tho-otto page instead, download these three
matching Linux 64-bit archives from the latest toolchain section:

- `binutils-2.45-mintelf-20250812-bin-linux64.tar.xz`
- `gcc-14.3.0-mintelf-20250702-bin-linux64.tar.xz`
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
trap 'rm -rf "$stage"' EXIT
for archive in \
    binutils-2.45-mintelf-20250812-bin-linux64.tar.xz \
    gcc-14.3.0-mintelf-20250702-bin-linux64.tar.xz \
    mintlib-0.60.1-mintelf-20240718-dev.tar.xz; do
    entries=$(tar -tJf "$archive") || exit 1
    while IFS= read -r path; do
        case "$path" in
            usr/*) ;;
            *) echo "unexpected archive path: $path" >&2; exit 1 ;;
        esac
    done <<EOF
$entries
EOF
    tar -xJf "$archive" -C "$stage" || exit 1
done
# Compare `shasum -a 256 <archive>` with a trusted release record before this step.
sudo cp -a "$stage/usr/." /usr/
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
- `gcc-14.3.0-mintelf-20250702-bin-macos.tar.xz`
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
trap 'rm -rf "$stage"' EXIT
for archive in \
    binutils-2.45-mintelf-20250812-bin-macos.tar.xz \
    gcc-14.3.0-mintelf-20250702-bin-macos.tar.xz \
    mintlib-0.60.1-mintelf-20240718-dev.tar.xz; do
    entries=$(tar -tJf "$archive") || exit 1
    while IFS= read -r path; do
        case "$archive:$path" in
            *mintlib*:usr/m68k-atari-mintelf/sys-root/*) ;;
            *:opt/cross-mint/*) ;;
            *) echo "unexpected archive path: $path" >&2; exit 1 ;;
        esac
    done <<EOF
$entries
EOF
    tar -xJf "$archive" -C "$stage" || exit 1
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
- `m68k-atari-mintelf-gcc does not support -mfastcall` means the config has `CONF_WITH_MFASTCALL=y` and `PATH` finds the PPA gcc first. Prepend the Otto toolchain ([without sudo](#-mfastcall-toolchain-without-sudo)). The PPA gcc is fine for every other m68k config.
- Confirm the selected prefix with `make V=1` on Ubuntu or `gmake V=1` on macOS when diagnosing a build.
