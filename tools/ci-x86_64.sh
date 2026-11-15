#!/bin/bash
#
# ci-x86_64.sh - build and test one pc-x86_64 configuration the way CI does
#
# Usage: tools/ci-x86_64.sh CONFIG       (products/CONFIG/ gets the image)
#
# Builds with the host's gcc (CROSS_COMPILE=), as CI does instead of the
# x86_64-elf- cross toolchain of the developer default, checks the x32 VDI
# parameter contract and, for the configurations that boot or run something,
# boots it under QEMU/OVMF (tools/ci-x86_64-boot.sh) or runs the external
# desktop tests.  QEMU and OVMF have to be installed.
#

set -e -o pipefail
config=$1

make distclean >/dev/null 2>&1
make "${config}_defconfig" 2>&1 | tee build.log
make CROSS_COMPILE= 2>&1 | tee -a build.log
tools/check-build-warnings.sh build.log

make CROSS_COMPILE= vdi-contract-test

# The PE32+ EFI application, plus the map file with its symbols.
mkdir -p "products/$config"
for product in *.efi emutos.map cli/emucon.tos emudesk.prg; do
    if [ -e "$product" ]; then
        cp "$product" "products/$config/"
    fi
done

# Shared desktop and console executables need the loader and libraries on
# their boot drive.  Keep the GEMSYS layout relative to the artifact root.
if grep -q '^CONF_WITH_ELF_SHARED=y$' .config; then
    runtime=obj/ldso/runtime/GEMSYS
    test -d "$runtime"
    rm -rf "products/$config/GEMSYS"
    cp -R "$runtime" "products/$config/"
fi

# A build that links is not a build that boots: without this, a regression in
# the boot path, the trap-dispatch/machine-hook layer, or the exception/panic
# path would still be a green job, since nothing above actually runs the
# image.  Only pc-x86_64 and pc-x86_64-eager boot this way (EFI/OVMF); a future non-EFI x86-64
# config would need its own check instead of this one.
case $config in
    pc-x86_64|pc-x86_64-vdi|pc-x86_64-callbacks|pc-x86_64-eager)
        tools/ci-x86_64-boot.sh
        ;;
esac

case $config in
    pc-x86_64-desktop-tests*)
        make CROSS_COMPILE= -j2 x32-desktop-test

        # The shared desktop links against the loader and the libraries, and
        # the kernel has no AES of its own (the external libaes desktop).
        readelf -l emudesk.prg | grep '/GEMSYS/LIB/X32/LD.SO'
        readelf -d emudesk.prg | grep 'libptos.so'
        readelf -d emudesk.prg | grep 'libaes.so'
        nm emutos.efi > obj/desktop-kernel.symbols
        grep -q ' T cuis_service$' obj/desktop-kernel.symbols
        if grep -Eq ' [Tt] (ui_start|aes_service|aes_tree_validate)$' obj/desktop-kernel.symbols; then
            echo 'External libaes desktop must not link kernel AES services' >&2
            exit 1
        fi

        console_args=()
        if grep -q '^# CONF_EXTERNAL_CLI is not set$' .config; then
            console_args+=(--embedded-console)
            # the built-in EmuCON keeps its static entry path
            if readelf -l obj/x32/emucon.elf | grep 'INTERP'; then
                echo 'Embedded EmuCON must retain its static entry path' >&2
                exit 1
            fi
        else
            readelf -l cli/emucon.tos | grep '/GEMSYS/LIB/X32/LD.SO'
            readelf -d cli/emucon.tos | grep 'libptos.so'
        fi
        rc=0
        rm -rf obj/x32-desktop-runtime obj/x32-desktop-shutdown
        python3 tests/x32_desktop/run.py --output obj/x32-desktop-runtime "${console_args[@]}" || rc=$?
        # the shutdown request of the desktop (CONF_WITH_SHUTDOWN)
        if grep -q '^CONF_WITH_SHUTDOWN=y$' .config; then
            python3 tests/x32_desktop/run.py --mode shutdown --output obj/x32-desktop-shutdown \
                "${console_args[@]}" || rc=$?
        fi
        # what they left, for the artifact, whether they passed or not
        if [ -d obj/x32-desktop-runtime ]; then
            cp -r obj/x32-desktop-runtime "products/$config/desktop-runtime"
        fi
        if [ -d obj/x32-desktop-shutdown ]; then
            cp -r obj/x32-desktop-shutdown "products/$config/desktop-shutdown"
        fi
        exit $rc
        ;;
esac
