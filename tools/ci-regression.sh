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
pmmu_check=false
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
        # the console is on the UART as well (CONF_SERIAL_CONSOLE, the
        # default on the Pi), where the script reads the verdict; no shutdown
        # checks, which type into EmuCON
        config=rpi2
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
        # the higher-half kernel with the 68040 MMU on (#569); the same image
        # is booted on CPUs without a PMMU as well, which it has to refuse
        # (#564, tools/test-pmmu-check.sh)
        config=virt-m68k-mmu
        pmmu_check=true
        ;;
    virt-m68k-aspace)
        # every process in an address space of its own, user mode and page
        # tables per process (#568, #570)
        config=virt-m68k-aspace
        ;;
    virt-m68k-aspace-defconfig)
        # the same configuration exactly as shipped: virt-m68k-aspace above
        # adds test profiles to it, so this is the only run of the suite
        # (shared-VM Pexec included, #1018) with the defconfig's own options
        config=virt-m68k-aspace
        script_machine=virt-m68k-aspace
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
        ;;
    *)
        echo "usage: $0 MACHINE (virt-arm, virt-arm-traditional-fat, raspi2b, virt-m68k, virt-m68k-fastcall, virt-m68k-mmu, virt-m68k-aspace, virt-m68k-aspace-defconfig, virt-arm-aspace, raspi2b-aspace, pc-x86_64)" >&2
        exit 2
        ;;
esac

make distclean >/dev/null 2>&1
# The kernel of these configurations has no build job of its own on a pull
# request (.github/pr-skip-configs), so the warning check of those jobs is
# made here.
make "${config}_defconfig" 2>&1 | tee build.log
case "$machine" in
    virt-arm-aspace|virt-m68k-aspace|pc-x86_64)
        CONFIG_= python3 - <<'PY'
import kconfiglib, os
config = kconfiglib.Kconfig()
config.load_config()
for required in ('CONF_WITH_MULTITHREADING', 'CONF_WITH_FORK', 'CONF_WITH_EXEC_REPLACE'):
    assert config.syms[required].str_value == 'y', required
for probe in ('CONF_WITH_PRIVATE_CLONE_TEST', 'CONF_WITH_FORK_RETURN_TEST'):
    config.syms[probe].set_value(2)
    assert config.syms[probe].str_value == 'y', probe
if os.environ.get('IRQ_RESTART_TEST') == '1':
    config.syms['CONF_WITH_PREEMPTION'].set_value(0)
    config.syms['CONF_WITH_MMAP'].set_value(2)
    config.syms['CONF_WITH_IRQ_RESTART_TEST'].set_value(2)
    assert config.syms['CONF_WITH_IRQ_RESTART_TEST'].str_value == 'y'
    assert config.syms['CONF_WITH_PREEMPTION'].str_value == 'n'
else:
    assert config.syms['CONF_WITH_PREEMPTION'].str_value == 'y'
    assert config.syms['CONF_WITH_IRQ_RESTART_TEST'].str_value == 'n'
    if config.syms['CONF_WITH_CUIS'].str_value == 'y':
        config.syms['CONF_WITH_CUIS_WAIT_TEST'].set_value(2)
        assert config.syms['CONF_WITH_CUIS_WAIT_TEST'].str_value == 'y'
if config.syms['ARCH_X86_64'].str_value != 'y':
    config.syms['CONF_WITH_EXIT_HANDOFF_TEST'].set_value(2)
    assert config.syms['CONF_WITH_EXIT_HANDOFF_TEST'].str_value == 'y'
assert config.syms['CONF_WITH_MULTITHREADING'].str_value == 'y'
if os.environ.get('PRG_ONLY_EXEC') == '1':
    assert config.syms['MACHINE_VIRT_M68K'].str_value == 'y'
    config.syms['CONF_WITH_ELF_SHARED'].set_value(0)
    config.syms['CONF_WITH_ELF_LOADER'].set_value(0)
    for name in ('CONF_WITH_ELF_LOADER', 'CONF_WITH_ELF_SHARED'):
        assert config.syms[name].str_value == 'n', name
    for name in ('CONF_WITH_PRG_LOADER', 'CONF_WITH_EXEC_REPLACE', 'CONF_WITH_FORK'):
        assert config.syms[name].str_value == 'y', name
if os.environ.get('EXEC_REPLACEMENT_TEST') == '1':
    config.syms['CONF_WITH_EXEC_REPLACEMENT_TEST'].set_value(2)
    assert config.syms['CONF_WITH_EXEC_REPLACEMENT_TEST'].str_value == 'y'
if os.environ.get('EXEC_HANDOFF_TEST') == '1':
    for required in ('CONF_WITH_EXEC_REPLACEMENT_TEST', 'CONF_WITH_EXEC_HANDOFF_TEST'):
        config.syms[required].set_value(2)
        assert config.syms[required].str_value == 'y', required
if os.environ.get('SHARED_VM_PEXEC') == '1':
    # m68k MMU kernels only (#1018); derived, never forced.
    assert config.syms['CONF_WITH_SHARED_VM_PEXEC'].str_value == 'y'
if os.environ.get('SHARED_VM_PEXEC') == '1' or os.environ.get('EXEC_STAGING_PROFILE') == '1':
    for required in ('CONF_WITH_IMAGE_POLICY_TEST', 'CONF_WITH_PRIVATE_SNAPSHOT_TEST'):
        config.syms[required].set_value(2)
        assert config.syms[required].str_value == 'y', required
    for required in ('CONF_WITH_ELF_SHARED', 'CONF_WITH_MMAP'):
        config.syms[required].set_value(2)
        assert config.syms[required].str_value == 'y', required
    backend = os.environ.get('SHARED_VM_PEXEC_FS', 'fat')
    assert backend in ('fat', 'pfs'), backend
    config.syms['CONF_WITH_PLUGGABLE_FS'].set_value(2 if backend == 'pfs' else 0)
    assert config.syms['CONF_WITH_PLUGGABLE_FS'].str_value == ('y' if backend == 'pfs' else 'n')
if os.environ.get('PROCESS_FILES_TEST') == '1':
    config.syms['CONF_WITH_PROCESS_FILES_TEST'].set_value(2)
    assert config.syms['CONF_WITH_PROCESS_FILES_TEST'].str_value == 'y'
    # Public Pfork/Pvfork over the PROCESS descriptor snapshot.
    config.syms['CONF_WITH_FORK'].set_value(2)
    assert config.syms['CONF_WITH_FORK'].str_value == 'y'
    backend = os.environ.get('PROCESS_FILES_FS', 'fat')
    assert backend in ('fat', 'pfs'), backend
    config.syms['CONF_WITH_PLUGGABLE_FS'].set_value(2 if backend == 'pfs' else 0)
    assert config.syms['CONF_WITH_PLUGGABLE_FS'].str_value == ('y' if backend == 'pfs' else 'n')
if config.syms['CONF_WITH_ELF_SHARED'].str_value == 'y' and any(
        config.syms[name].str_value == 'y' for name in
        ('CONF_WITH_X86_64_MEMTEST', 'CONF_WITH_EXIT_HANDOFF_TEST')):
    config.syms['CONF_WITH_ELF_VECTOR_ENTRY_TEST'].set_value(2)
    assert config.syms['CONF_WITH_ELF_VECTOR_ENTRY_TEST'].str_value == 'y'
config.write_config()
PY
        ;;
esac
if [ "$traditional_fat" = true ]; then
    sed -i 's/^CONF_WITH_FAT_FILE_LIFETIMES=y$/# CONF_WITH_FAT_FILE_LIFETIMES is not set/' .config
fi
make $make_args 2>&1 | tee -a build.log
tools/check-build-warnings.sh build.log
if [ "${IRQ_RESTART_TEST:-0}" = 1 ]; then
    grep -qx '#define CONF_WITH_IRQ_RESTART_TEST 1' obj/autoconf.h
    if grep -qx '#define CONF_WITH_PREEMPTION 1' obj/autoconf.h; then
        echo 'B2 validation must run with production timer preemption disabled' >&2
        exit 2
    fi
    echo 'B2 effective gates: retained IRQ test enabled, production timer preemption disabled'
fi
if [ "${SHARED_VM_PEXEC:-0}" = 1 ]; then
    for symbol in CONF_WITH_SHARED_VM_PEXEC CONF_WITH_ELF_SHARED CONF_WITH_MMAP \
                  CONF_WITH_PRIVATE_CLONE_TEST CONF_WITH_MULTITHREADING; do
        grep -q "^#define $symbol 1$" obj/autoconf.h || {
            echo "positive shared-Pexec configuration missing effective $symbol" >&2
            exit 2
        }
    done
    echo 'shared-VM Pexec effective gates: shared/mmap/private-test/scheduler/clone=1'
fi
make $make_args test-dir

tools/run-regression-tests.sh "$script_machine"

if [ "$pmmu_check" = true ]; then
    if ! tools/test-pmmu-check.sh virt-m68k.elf; then
        echo "::error title=PMMU startup check::the virt-m68k-mmu image did not refuse a CPU without a PMMU, or did not boot on the 68040"
        exit 1
    fi
fi
