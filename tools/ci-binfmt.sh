#!/bin/bash
# Build a kernel and ordinary native interpreter fixtures, then boot-test #944.
# Like ci-regression.sh, this changes the ignored kernel/libcmini configuration.
set -euo pipefail
cd "$(dirname "$0")/.."
arch=${1:?usage: tools/ci-binfmt.sh arm|x32|m68k}
layout=${2:-private}
suffix=
case $arch in
    arm)
        config=virt-arm-aspace; prefix=arm-unknown-ptos-elf-; libc_config=ptos_arm_defconfig
        flags='-march=armv6 -marm -mfpu=vfp -mfloat-abi=hard -mword-relocations'
        libs=-lgcc ;;
    x32)
        config=pc-x86_64; prefix=x86_64-unknown-ptos-elfx32-; libc_config=ptos_x86_64_defconfig
        flags='-mx32 -march=x86-64 -fcf-protection=none'
        libs= ;;
    m68k)
        config=virt-m68k-aspace; prefix=m68k-unknown-ptos-prgelf-; libc_config=mintelf_defconfig
        flags=-m68000; libs=-lgcc ;;
    *) echo "unknown architecture: $arch" >&2; exit 2 ;;
esac
if [ "$layout" = shared ] && [ "$arch" = arm ]; then
    config=virt-arm-cli
    suffix=-shared
elif [ "$layout" != private ]; then
    echo 'only ARM supports the optional shared layout' >&2
    exit 2
fi
make "${config}_defconfig"
CONFIG_= python3 - <<'PY'
import kconfiglib
c = kconfiglib.Kconfig()
c.load_config()
for key, value in (("CONF_WITH_BINFMT", 2), ("CONF_WITH_MMAP", 2),
                   ("CONF_WITH_X86_64_MEMTEST", 0), ("CONF_WITH_ELF_SHARED", 0)):
    c.syms[key].set_value(value)
c.write_config()
PY
make CROSS_COMPILE="$prefix" -j4
build=$PWD/obj/binfmt/$arch$suffix
libc=$build/libcmini
mkdir -p "$build"
rm -rf "$libc"
make -C lib/libcmini BUILDDIR="$libc" CROSSPREFIX="$prefix" "$libc_config"
common='-fno-pie -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables'
make -C lib/libcmini BUILDDIR="$libc" CROSSPREFIX="$prefix" ONLY_68K=y dirs
make -C lib/libcmini BUILDDIR="$libc" CROSSPREFIX="$prefix" ONLY_68K=y \
    ARCH_CFLAGS="$flags $common" -j4 "$libc/./libcmini.a" "$libc/./objs/crt0.o"
includes=$(${prefix}gcc -print-file-name=include)
for fixture in driver interpreter; do
    link='-Wl,--emit-relocs,--build-id=none'
    if [ "$arch" != m68k ]; then
        address=0x00400000
        if [ "$fixture" = interpreter ]; then address=0x41000000; fi
        link="$link -Wl,-Ttext-segment=$address,-z,max-page-size=0x1000,-z,noseparate-code,-z,noexecstack"
    fi
    ${prefix}gcc $flags $common -std=gnu90 -Os -Wall -Wextra -Werror \
        -Wdeclaration-after-statement -ffreestanding -fno-builtin -nostdinc \
        -isystem lib/libcmini/include -isystem "$includes" \
        -nostdlib -static -no-pie $link "$libc/objs/crt0.o" \
        "tests/binfmt/fixtures/$fixture.c" "$libc/libcmini.a" $libs -o "$build/$fixture.tos"
done
python3 tools/run-binfmt-tests.py "$arch" "$build"
