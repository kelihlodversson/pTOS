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

combos=${*:-"amiga:68030 amiga:68040 amiga:68060 amiga-mmu:68030 amiga-mmu040:68040 amiga-mmu040:68060 amiga-aspace:68030 amiga-aspace040:68040 amiga-aspace040:68060"}
mkdir -p logs
failed=

for combo in $combos; do
    config=${combo%%:*}
    cpu=${combo#*:}
    name=$config-$cpu
    echo "::group::$name"

    if ! { make "${config}_defconfig" >/dev/null && make -j"$(nproc)" >"logs/$name.build" 2>&1; }; then
        echo "::error::$name: the build failed (logs/$name.build)"
        tail -20 "logs/$name.build"
        failed="$failed $name"
        echo "::endgroup::"
        continue
    fi

    # an image with an address space for every process has no desktop: the
    # harness is the shell (make test-hd-shell), it halts the machine when it
    # is done, and the suite is the whole check
    case $config in
    *aspace*)
        if make test-hd-shell >"logs/$name.testdisk" 2>&1; then
            for mode in "" "--caches"; do
                suffix=${mode:+-caches}
                if python3 tools/run-amiberry-tests.py --rom ptos-amiga.rom --disk test-hd-shell.img \
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
    python3 tools/run-amiberry.py --rom ptos-amiga.rom --cpu "$cpu" --wait 15 \
        --cmd GET_CPU_REGS --cmd "WRITE_MEM 0x300000 4 305419896" \
        --cmd "READ_MEM 0x300000 4" --screenshot "logs/$name.png" >"logs/$name.log" 2>&1
    pc=$(sed -n 's/^PC=\([0-9A-Fa-f]*\)$/\1/p' "logs/$name.log" | head -1)
    fastram=$(sed -n '/^> READ_MEM 0x300000 4/{n;n;p}' "logs/$name.log" | head -1)

    # the PC is within the loop of stop_until_interrupt (a few instructions)
    ok=$(python3 -c "
import sys
pc = int('${pc:-0}', 16); idle = int('$idle', 16)
print(1 if idle <= pc < idle + 0x40 else 0)")
    if [ "$fastram" != 305419896 ]; then
        echo "::error::$name: the Zorro II fast RAM does not answer (the board was not configured): read ${fastram:-nothing}"
        failed="$failed $name"
    elif [ "$ok" != 1 ]; then
        echo "::error::$name: not idle in the kernel: PC=${pc:-?}, stop_until_interrupt at $idle"
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
