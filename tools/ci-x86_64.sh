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

# A build that links is not a build that boots: without this, a regression in
# the boot path, the trap-dispatch/machine-hook layer, or the exception/panic
# path would still be a green job, since nothing above actually runs the
# image.  Only pc-x86_64 boots this way (EFI/OVMF); a future non-EFI x86-64
# config would need its own check instead of this one.
case $config in
    pc-x86_64|pc-x86_64-vdi|pc-x86_64-aes|pc-x86_64-callbacks|pc-x86_64-accessories*)
        tools/ci-x86_64-boot.sh
        ;;
esac

case $config in
    pc-x86_64-desktop-tests*)
        make CROSS_COMPILE= -j2 x32-desktop-test x32-desktop-window-test
        console_args=()
        if grep -q '^# CONF_EXTERNAL_CLI is not set$' .config; then
            console_args+=(--embedded-console)
        fi
        rc=0
        rm -rf obj/x32-desktop-runtime
        python3 tests/x32_desktop/run.py --output obj/x32-desktop-runtime "${console_args[@]}" || rc=$?
        # what it left, for the artifact, whether it passed or not
        if [ -d obj/x32-desktop-runtime ]; then
            cp -r obj/x32-desktop-runtime "products/$config/desktop-runtime"
        fi
        exit $rc
        ;;
esac
