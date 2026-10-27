#!/bin/bash
#
# ci-regression.sh - build and run the regression suite on one machine, as CI does
#
# Usage: tools/ci-regression.sh MACHINE
#   MACHINE: virt-arm, virt-arm-traditional-fat, raspi2b, virt-m68k,
#            virt-m68k-fastcall, virt-m68k-mmu, virt-m68k-aspace,
#            virt-arm-aspace, raspi2b-aspace or pc-x86_64
#
# The regression suite (tests/, `make test-dir`) under QEMU.  The virt
# machines are built without the AES (the *-cli configurations) so that they
# boot to EmuCON, where tools/run-regression-tests.sh types the name of the
# suite into the serial console.  The x32 and Pi desktops start it from
# tests/emudesk.inf's autorun line.  See tests/readme.md.  QEMU and the
# fonts the gdos_answer suite uses have to be installed.
#

set -e -o pipefail
machine=$1

script_machine=$machine
traditional_fat=false
make_args=
case $machine in
    virt-arm)
        config=virt-arm-cli
        ;;
    virt-arm-traditional-fat)
        # the small-ROM deletion implementation, on a board that can run the
        # complete regression suite automatically
        config=virt-arm-cli
        script_machine=virt-arm
        traditional_fat=true
        ;;
    raspi2b)
        # a Raspberry Pi 2 with the desktop: the AES, the chunky 16 bit
        # screen of the Pi's framebuffer.  The desktop starts the suite and
        # the console is on the UART as well (rpi2-test_defconfig), where the
        # script reads the verdict; no shutdown checks, which type into EmuCON
        config=rpi2-test
        ;;
    virt-m68k)
        config=virt-m68k-cli-elf
        ;;
    virt-m68k-fastcall)
        # the register-argument calling convention (-mfastcall): the VDI's
        # scalable fonts call FreeType on a stack of its own, which it has to
        # survive
        config=virt-m68k-cli-fastcall
        script_machine=virt-m68k
        ;;
    virt-m68k-mmu)
        # the higher-half kernel with the 68040 MMU on (#569)
        config=virt-m68k-mmu
        ;;
    virt-m68k-aspace)
        # every process in an address space of its own, user mode and page
        # tables per process (#568, #570)
        config=virt-m68k-aspace
        ;;
    virt-arm-aspace)
        config=virt-arm-aspace
        script_machine=virt-arm
        ;;
    raspi2b-aspace)
        # EmuCON on the UART; no shutdown checks (QEMU's raspi2b does not switch off)
        config=rpi2-aspace
        ;;
    pc-x86_64)
        config=pc-x86_64-desktop
        make_args=CROSS_COMPILE=     # native gcc, as in the x86_64 job
        ;;
    *)
        echo "usage: $0 MACHINE (virt-arm, virt-arm-traditional-fat, raspi2b, virt-m68k, virt-m68k-fastcall, virt-m68k-mmu, virt-m68k-aspace, virt-arm-aspace, raspi2b-aspace, pc-x86_64)" >&2
        exit 2
        ;;
esac

make distclean >/dev/null 2>&1
# The kernel of these configurations has no build job of its own on a pull
# request (.github/pr-skip-configs), so the warning check of those jobs is
# made here.
make "${config}_defconfig" 2>&1 | tee build.log
if [ "$traditional_fat" = true ]; then
    sed -i 's/^CONF_WITH_FAT_FILE_LIFETIMES=y$/# CONF_WITH_FAT_FILE_LIFETIMES is not set/' .config
fi
make $make_args 2>&1 | tee -a build.log
tools/check-build-warnings.sh build.log
make $make_args test-dir

tools/run-regression-tests.sh "$script_machine"
