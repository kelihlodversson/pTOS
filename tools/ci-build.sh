#!/bin/bash
#
# ci-build.sh - build one configuration the way CI does, and keep its products
#
# Usage: tools/ci-build.sh CONFIG        (products/CONFIG/ gets the images)
#
# The build must print no warning (#584): keep it so.
#

set -e -o pipefail
config=$1

make distclean >/dev/null 2>&1
# Kconfig warns about a discarded assignment only here, so the log starts
# with the defconfig
make "${config}_defconfig" 2>&1 | tee build.log
make 2>&1 | tee -a build.log
tools/check-build-warnings.sh build.log

# The names of the products depend on the configuration, so take whatever the
# build produced rather than guessing: the images (kernel.img, kernel7.img,
# ... on the Raspberry Pi), the symbols, the map, and EmuCON and EmuDesk, the
# programs CONF_EXTERNAL_CLI and CONF_EXTERNAL_DESK load from the boot drive.
mkdir -p "products/$config"
for product in *.img *.prg *.st *.s19 *.stc *.rom *.adf *.sym \
               emutos.elf emutos.map cli/emucon.tos emudesk.prg; do
    if [ -e "$product" ]; then
        cp "$product" "products/$config/"
    fi
done
