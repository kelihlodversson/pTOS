#!/bin/bash
# Build a kernel and ordinary native interpreter fixtures, then boot-test #944.
# Like ci-regression.sh, this changes the ignored kernel/libcmini configuration.
set -euo pipefail
cd "$(dirname "$0")/.."
arch=${1:?usage: tools/ci-binfmt.sh arm|x32|m68k}
layout=${2:-private}
suffix=
vector_args=-DNATIVE_VECTOR_FIXTURE
startup=elfcrt0
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
        # These ELF probes start with vectors and exercise the legacy calls.
        flags=-m68000; libs=-lgcc; vector_args= ;;
    *) echo "unknown architecture: $arch" >&2; exit 2 ;;
esac
if [ "$layout" = shared ] && [ "$arch" = arm ]; then
    config=virt-arm-cli
    suffix=-shared
elif [ "$layout" = shared ] && [ "$arch" = m68k ]; then
    config=virt-m68k-cli-elf
    suffix=-shared
    vector_args='-DNATIVE_VECTOR_FIXTURE -DSHARED_M68K_VECTOR_FIXTURE'
elif [ "$layout" != private ]; then
    echo 'only ARM and m68k support the optional shared layout' >&2
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
process_directory=
if grep -qx '#define CONF_WITH_PROCESS_EXEC 1' obj/autoconf.h; then
    process_directory=-DPROCESS_DIRECTORY_TEST
fi
build=$PWD/obj/binfmt/$arch$suffix
libc=$build/libcmini
mkdir -p "$build"
rm -rf "$libc"
make -C lib/libcmini BUILDDIR="$libc" CROSSPREFIX="$prefix" "$libc_config"
common='-fno-pie -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables'
make -C lib/libcmini BUILDDIR="$libc" CROSSPREFIX="$prefix" ONLY_68K=y dirs
make -C lib/libcmini BUILDDIR="$libc" CROSSPREFIX="$prefix" ONLY_68K=y \
    ARCH_CFLAGS="$flags $common" -j4 "$libc/./libcmini.a" "$libc/./objs/$startup.o"
includes=$(${prefix}gcc -print-file-name=include)
for fixture in driver interpreter; do
    link='-Wl,--emit-relocs,--build-id=none'
    fixture_startup=$startup
    if [ "$arch" != m68k ]; then
        address=0x00400000
        if [ "$fixture" = interpreter ]; then address=0x41000000; fi
        link="$link -Wl,-Ttext-segment=$address,-z,max-page-size=0x1000,-z,noseparate-code,-z,noexecstack"
    elif [ "$layout:$fixture" = shared:driver ]; then
        # The boot shell still uses legacy Pexec on non-MMU m68k.
        fixture_startup=crt0
        link='-mmintelf -Wl,-e_start'
        make -C lib/libcmini BUILDDIR="$libc" CROSSPREFIX="$prefix" ONLY_68K=y \
            ARCH_CFLAGS="$flags $common" "$libc/./objs/crt0.o"
    fi
    ${prefix}gcc $flags $common $vector_args $process_directory -std=gnu90 -Os -Wall -Wextra -Werror \
        -Wdeclaration-after-statement -ffreestanding -fno-builtin -nostdinc \
        -isystem lib/libcmini/include -isystem "$includes" \
        -nostdlib -static -no-pie $link "$libc/objs/$fixture_startup.o" \
        "tests/binfmt/fixtures/$fixture.c" "$libc/libcmini.a" $libs -o "$build/$fixture.tos"
done
if [ "$arch:$layout" = m68k:shared ]; then
    ${prefix}gcc $flags $common -std=gnu90 -Os -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -isystem lib/libcmini/include \
        -nostdlib -static -no-pie -mmintelf -Wl,-e_start "$libc/objs/crt0.o" \
        tests/ldso_runtime/pexecve_prg.c "$libc/libcmini.a" $libs -o "$build/vectors.tos"
fi
python3 tools/run-binfmt-tests.py "$arch" "$build"
