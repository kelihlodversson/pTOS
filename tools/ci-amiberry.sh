#!/bin/sh
#
# ci-amiberry.sh - boot the Amiga ROMs of pTOS on the pinned Amiberry, and run
# the regression tests on them (#737)
#
# Usage: tools/ci-amiberry.sh [CONFIG:CPU ...]
#
# Builds each configuration (make CONFIG_defconfig && make), starts it in
# Amiberry (tools/run-amiberry.py, headless) on an emulated 68030, 68040 or
# 68060 with its MMU, and checks that it booted: the CPU is idle in the kernel
# (in stop_until_interrupt, which the desktop waits in) and, for the images that
# run in the upper half, that it is fetching from the window (0x80000000 and
# up), which it can only do through the MMU tree that the ROM set up.  The
# screenshot of each is left in logs/.  Then the test disk is made (make test-hd:
# mkfs.fat and mtools) and tools/run-amiberry-tests.py runs the suite of tests/
# from it, on the same CPU; its log is logs/CONFIG-CPU-tests.log.  Amiberry is the one of
# tools/build-amiberry.sh (the setup-amiberry action sets $AMIBERRY).
#
# The default list is the Amiga configurations and the CPUs they are for.
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.

set -u

if [ "${IRQ_COPY_PROOF:-0}" = 1 ] && [ "${NATIVE_THREADS:-0}" != 1 ]; then
    echo "ci-amiberry: IRQ_COPY_PROOF requires NATIVE_THREADS=1" >&2
    exit 2
fi

if [ "${NATIVE_PREEMPT:-0}" = 1 ] &&
   { [ "${NATIVE_THREADS:-0}" != 1 ] || [ "${IRQ_COPY_PROOF:-0}" != 1 ]; }; then
    echo "ci-amiberry: NATIVE_PREEMPT requires NATIVE_THREADS=1 IRQ_COPY_PROOF=1" >&2
    exit 2
fi
if [ "${IRQ_RESTART_TEST:-0}" = 1 ] &&
   { [ "${NATIVE_THREADS:-0}" != 1 ] || [ "${NATIVE_PREEMPT:-0}" = 1 ]; }; then
    echo "ci-amiberry: IRQ_RESTART_TEST requires native threads and no timer policy" >&2
    exit 2
fi

combos=${*:-"amiga:68030 amiga:68040 amiga:68060 amiga-mmu:68030 amiga-mmu040:68040 amiga-mmu040:68060 amiga-aspace:68030 amiga-aspace040:68040 amiga-aspace040:68060"}
mkdir -p logs
failed=

for combo in $combos; do
    config=${combo%%:*}
    cpu=${combo#*:}
    name=$config-$cpu
    [ "${NATIVE_THREADS:-0}" != 1 ] || name=$name-native
    [ "${NATIVE_PREEMPT:-0}" != 1 ] || name=$name-preempt
    [ "${IRQ_RESTART_TEST:-0}" != 1 ] || name=$name-b2
    echo "::group::$name"

    build_target() {
        make "${config}_defconfig" >/dev/null || return 1
        if [ "${NATIVE_THREADS:-0}" = 1 ]; then
            CONFIG_= python3 - <<'PYCONFIG' || return 1
import kconfiglib, os
k=kconfiglib.Kconfig()
k.load_config()
assert k.syms['CONF_WITH_USER_ASPACE'].str_value == 'y'
# The full native owner/admission kernel needs more than the 256-KB 030
# profile. Use the existing 512-KB image format on the same CPU/MMU; ordinary
# scheduler-disabled 256-KB lanes above remain part of the default matrix.
k.syms['CONF_AMIGA_ROM_512K'].set_value(2)
assert k.syms['CONF_AMIGA_ROM_512K'].str_value == 'y'
for name in ('CONF_WITH_NATIVE_SCHED','CONF_WITH_PCLONE','CONF_WITH_PTHJOIN','CONF_WITH_PTERMGRP','CONF_WITH_EXIT_HANDOFF_TEST'):
    k.syms[name].set_value(2)
    assert k.syms[name].str_value == 'y', name
if os.environ.get('IRQ_COPY_PROOF') == '1':
    # Copied return is qualified only for the stack ABI on 040. Keep the
    # ordinary 030/060 fastcall lanes; this separate proof uses the existing
    # 512-KB 040 ROM profile and must actually include the complete FPU helpers.
    for name in ('CONF_AMIGA_HIGHER_HALF_68040','CONF_AMIGA_ROM_512K'):
        assert k.syms[name].str_value == 'y', name
    k.syms['CONF_WITH_MFASTCALL'].set_value(0)
    assert k.syms['CONF_WITH_MFASTCALL'].str_value == 'n'
    if os.environ.get('NATIVE_PREEMPT') != '1' and os.environ.get('IRQ_RESTART_TEST') != '1':
        k.syms['CONF_WITH_FORK_RETURN_TEST'].set_value(2)
        assert k.syms['CONF_WITH_FORK_RETURN_TEST'].str_value == 'y'
        assert k.syms['CONF_WITH_SAVED_RETURN'].str_value == 'y'
if os.environ.get('NATIVE_PREEMPT') == '1':
    for name in ('CONF_WITH_ELF_LOADER', 'CONF_WITH_MMAP', 'CONF_WITH_PRIVATE_CLONE',
                 'CONF_WITH_PRIVATE_CLONE_TEST', 'CONF_WITH_PROCESS_FILES',
                 'CONF_WITH_PROCESS_FILES_TEST',
                 'CONF_WITH_NATIVE_PREEMPT'):
        k.syms[name].set_value(2)
        assert k.syms[name].str_value == 'y', name
if os.environ.get('NATIVE_PREEMPT') == '1':
    # Existing ROM-window backend refuses private cloning. Test independent
    # loader roots instead, without exposing an unsupported public Pfork.
    k.syms['CONF_WITH_PFORK'].set_value(0)
    k.syms['CONF_WITH_FORK_RETURN_TEST'].set_value(0)
    assert k.syms['CONF_WITH_SAVED_RETURN'].str_value == 'y'
if os.environ.get('IRQ_RESTART_TEST') == '1':
    fastcall = os.environ.get('IRQ_RESTART_FASTCALL') == '1'
    k.syms['CONF_WITH_MFASTCALL'].set_value(2 if fastcall else 0)
    assert k.syms['CONF_WITH_MFASTCALL'].str_value == ('y' if fastcall else 'n')
    for name in ('CONF_WITH_ELF_LOADER', 'CONF_WITH_MMAP', 'CONF_WITH_PRIVATE_CLONE',
                 'CONF_WITH_PRIVATE_CLONE_TEST', 'CONF_WITH_IRQ_RESTART_TEST'):
        k.syms[name].set_value(2)
        assert k.syms[name].str_value == 'y', name
    assert k.syms['CONF_WITH_NATIVE_PREEMPT'].str_value == 'n'
    assert k.syms['CONF_WITH_FORK_RETURN_TEST'].str_value == 'n'
    assert k.syms['CONF_WITH_SAVED_RETURN'].str_value == ('n' if fastcall else 'y')
k.write_config()
PYCONFIG
        fi
        make -j"$(nproc)" >"logs/$name.build" 2>&1
    }
    managed=
    [ "${NATIVE_PREEMPT:-0}" != 1 ] || managed=--require-native-preemption
    [ "${NATIVE_THREADS:-0}" != 1 ] || managed="$managed --require-managed-threads"
    if [ "${IRQ_RESTART_TEST:-0}" = 1 ]; then
        irq_profile=refused
        if [ "$cpu" = 68040 ] && [ "${IRQ_RESTART_FASTCALL:-0}" != 1 ]; then
            irq_profile=deferred
        fi
        managed="$managed --require-irq-restart $irq_profile"
    fi
    fpu=
    [ -z "${AMIBERRY_FPU:-}" ] || fpu="--fpu $AMIBERRY_FPU"
    [ "${AMIBERRY_SOFTFLOAT:-0}" != 1 ] || fpu="$fpu --softfloat"
    [ "${REQUIRE_FPIAR:-0}" != 1 ] || managed="$managed --require-nonzero-fpiar"
    if ! build_target; then
        echo "::error::$name: the build failed (logs/$name.build)"
        tail -20 "logs/$name.build"
        failed="$failed $name"
        echo "::endgroup::"
        continue
    fi

    if [ "${NATIVE_THREADS:-0}" = 1 ]; then
        irq_state=$(awk '/^ +0x[0-9a-f]+ +m68k_irq_test_state$/ { print $1; exit }' emutos.map)
        if [ -z "$irq_state" ]; then
            echo "::error::$name: native IRQ capture state is missing"
            failed="$failed $name"
            echo "::endgroup::"
            continue
        fi
        # IPC READ_MEM addresses physical RAM, not the kernel's MMU window.
        if grep -q '^#define CONF_AMIGA_HIGHER_HALF 1' obj/autoconf.h; then
            irq_state=$(python3 -c "print(hex(int('$irq_state',16) & 0x7fffffff))")
        fi
        managed="$managed --irq-capture-state $irq_state"
        [ "${IRQ_COPY_PROOF:-0}" != 1 ] || managed="$managed --require-owned-irq-copy"
    fi

    # an image with an address space for every process has no desktop: the
    # harness is the shell (make test-hd-shell), it halts the machine when it
    # is done, and the suite is the whole check
    case $config in
    *aspace*)
        halt=$(awk '/^ +0x[0-9a-f]+ +halt$/ { print $1; exit }' emutos.map)
        if [ -z "$halt" ]; then
            echo "::error::$name: no halt in emutos.map"
            failed="$failed $name"
            echo "::endgroup::"
            continue
        fi
        if make test-hd-shell >"logs/$name.testdisk" 2>&1; then
            for mode in "" "--caches"; do
                suffix=${mode:+-caches}
                if python3 tools/run-amiberry-tests.py --rom ptos-amiga.rom --disk test-hd-shell.img \
                    --cpu "$cpu" --halt "$halt" $mode $managed $fpu >"logs/$name$suffix-tests.log" 2>&1; then
                    echo "$name${suffix:+ ($suffix)}: $(grep 'Summary' "logs/$name$suffix-tests.log")"
                else
                    echo "::error::$name$suffix: the tests failed (logs/$name$suffix-tests.log)"
                    grep -E 'FAIL|Summary|run-amiberry-tests' "logs/$name$suffix-tests.log" | head -20
                    failed="$failed $name$suffix"
                fi
            done
        else
            echo "::error::$name: the test disk was not made (logs/$name.testdisk)"
            failed="$failed $name"
        fi
        echo "::endgroup::"
        continue
        ;;
    esac

    idle=$(awk '/^ +0x[0-9a-f]+ +stop_until_interrupt$/ { print $1; exit }' emutos.map)
    if [ -z "$idle" ]; then
        echo "::error::$name: no stop_until_interrupt in emutos.map"
        failed="$failed $name"
        echo "::endgroup::"
        continue
    fi

    # (the Zorro II fast RAM of the configuration answers only if the ROM has
    # configured the board, which is autoconfig through the I/O window)
    # A booted ROM is idle nearly all the time, but one PC sample at a fixed
    # wall-clock time can land in work still in progress (a slow runner may
    # still be drawing the desktop at 15 s): take up to ten samples a second
    # apart, and ask that one of them be idle.
    set -- --cmd GET_CPU_REGS
    for n in 2 3 4 5 6 7 8 9 10; do
        set -- "$@" --cmd "SLEEP 1" --cmd GET_CPU_REGS
    done
    python3 tools/run-amiberry.py --rom ptos-amiga.rom --cpu "$cpu" --wait 15 \
        "$@" --cmd "WRITE_MEM 0x300000 4 305419896" \
        --cmd "READ_MEM 0x300000 4" --screenshot "logs/$name.png" >"logs/$name.log" 2>&1
    pcs=$(sed -n 's/^PC=\([0-9A-Fa-f]*\)$/\1/p' "logs/$name.log" | tr '\n' ' ')
    fastram=$(sed -n '/^> READ_MEM 0x300000 4/{n;n;p}' "logs/$name.log" | head -1)

    # the first sample within the loop of stop_until_interrupt (a few instructions)
    pc=$(python3 -c "
idle = int('$idle', 16)
print(next((p for p in '$pcs'.split() if idle <= int(p, 16) < idle + 0x40), ''))")
    ok=0; [ -z "$pc" ] || ok=1
    if [ "$fastram" != 305419896 ]; then
        echo "::error::$name: the Zorro II fast RAM does not answer (the board was not configured): read ${fastram:-nothing}"
        failed="$failed $name"
    elif [ "$ok" != 1 ]; then
        echo "::error::$name: not idle in the kernel: PC samples ${pcs:-?}, stop_until_interrupt at $idle"
        cat "logs/$name.log"
        failed="$failed $name"
    elif grep -q 'CONF_AMIGA_HIGHER_HALF=y' .config \
         && [ "$(python3 -c "print(1 if int('$pc', 16) >= 0x80000000 else 0)")" != 1 ]; then
        echo "::error::$name: the kernel is not running in the window: PC=$pc"
        failed="$failed $name"
    else
        echo "$name: booted (idle at PC=$pc)"
        # (once with the caches of the CPU left out of the emulation, the baseline,
        # and once with the instruction and data caches emulated)
        if make test-hd >"logs/$name.testdisk" 2>&1; then
            for mode in "" "--caches"; do
                suffix=${mode:+-caches}
                if python3 tools/run-amiberry-tests.py --rom ptos-amiga.rom --disk test-hd.img \
                    --cpu "$cpu" $mode >"logs/$name$suffix-tests.log" 2>&1; then
                    echo "$name${suffix:+ ($suffix)}: $(grep 'Summary' "logs/$name$suffix-tests.log")"
                else
                    echo "::error::$name$suffix: the tests failed (logs/$name$suffix-tests.log)"
                    grep -E 'FAIL|Summary|run-amiberry-tests' "logs/$name$suffix-tests.log" | head -20
                    failed="$failed $name$suffix"
                fi
            done
        else
            echo "::error::$name: the test disk was not made (logs/$name.testdisk)"
            failed="$failed $name"
        fi
    fi
    echo "::endgroup::"
done

if [ -n "$failed" ]; then
    echo "FAILED:$failed"
    exit 1
fi
