#!/bin/sh
#
# run-regression-tests.sh - run the regression suite under QEMU
#
# Usage: run-regression-tests.sh <virt-arm|virt-m68k|pc-x86_64> [timeout-seconds]
#
# Expects the matching configuration to be built and `make test-dir` to have
# collected the test programs into test-dir/ (QEMU's FAT driver presents the
# directory as the disk, so no image and no mtools are needed):
#
#   virt-arm     virt-arm-cli_defconfig   virt-arm.elf    qemu-system-arm
#   virt-m68k    virt-m68k-cli_defconfig  virt-m68k.elf   qemu-system-m68k
#   pc-x86_64    pc-x86_64_defconfig      pc-x86_64.efi   qemu-system-x86_64 + OVMF
#
# The virt machines are configured without the AES so that they boot to
# EmuCON, where the suite is started by typing its name into the serial
# console; pc-x86_64 boots to EmuCON in any case and is typed into through
# the QEMU monitor (PS/2 keyboard).  The script returns 0 when the harness
# printed "All tests passed." and its summary shows no failure, and prints
# the console log either way.
#
# QEMU does not exit by itself once the harness is done, so the log is
# polled and QEMU is killed when the verdict is in or the time is up.
#

set -u

machine=${1:-}
limit=${2:-300}
tools=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$tools")
work=$(mktemp -d "${TMPDIR:-/tmp}/ptos-regression.XXXXXX")
log=$work/console.log
qemu_pid=

cleanup() {
    [ -n "$qemu_pid" ] && kill "$qemu_pid" 2>/dev/null
    rm -rf "$work"
}
trap cleanup EXIT

[ -d "$top/test-dir" ] || { echo "$0: no test-dir/: run 'make test-dir' first" >&2; exit 2; }

vvfat="file.driver=vvfat,file.dir=$top/test-dir,file.rw=on"
virtio="-global virtio-mmio.force-legacy=false -drive if=none,id=hd0,format=raw,$vvfat -device virtio-blk-device,drive=hd0"

# show_log: the console log, and what QEMU itself said (only separate from the
# console log on x86-64: a missing firmware file or a bad drive option ends up
# there, and the work directory is deleted on exit)
show_log() {
    echo "::group::Console log ($machine)"
    cat "$log" 2>/dev/null
    echo "::endgroup::"
    if [ -s "$work/qemu.out" ]; then
        echo "::group::QEMU output ($machine)"
        cat "$work/qemu.out"
        echo "::endgroup::"
    fi
}

# summarize <verdict>: on GitHub Actions, put the result on the job's summary
# page (a table of the suites, and the failure details), not only in the log
summarize() {
    [ -n "${GITHUB_STEP_SUMMARY:-}" ] || return 0
    {
        echo "### Regression suite: $machine — $1"
        echo
        if grep -aqE '^  [A-Za-z0-9_]+.*\.\.\. (PASS|FAIL)' "$log" 2>/dev/null; then
            echo "| Suite | Result |"
            echo "| --- | --- |"
            grep -aE '^  [A-Za-z0-9_]+.*\.\.\. (PASS|FAIL)' "$log" | tr -d '\r' |
                sed -E 's/^ +(.*)\.\.\. (PASS|FAIL)$/| \1 | \2 |/'
            echo
            summary_line=$(grep -a -- '--- Summary' "$log" | tail -1 | tr -d '\r')
            [ -n "$summary_line" ] && echo "\`$summary_line\`" && echo
            if grep -aq 'FAIL: ' "$log"; then
                echo "Failures:"
                echo
                echo '```'
                grep -a 'FAIL: ' "$log" | tr -d '\r'
                echo '```'
            fi
        else
            echo "The suite never produced a result; see the console log in the job output."
        fi
    } >>"$GITHUB_STEP_SUMMARY"
}

# wait_for <text> <seconds>: wait until the console log contains the text
wait_for() {
    n=0
    while [ "$n" -lt "$2" ]; do
        grep -aqF "$1" "$log" 2>/dev/null && return 0
        sleep 1
        n=$((n + 1))
    done
    return 1
}

case $machine in
virt-arm | virt-m68k)
    if [ "$machine" = virt-arm ]; then
        set -- qemu-system-arm -M virt,highmem=off -cpu cortex-a7 -m 128 \
            -kernel "$top/virt-arm.elf"
    else
        set -- qemu-system-m68k -M virt -cpu m68020 -m 128 \
            -kernel "$top/virt-m68k.elf"
    fi
    # stdin is a FIFO held open from here, so that QEMU's serial console
    # never sees an end of file; the commands are written to it.
    mkfifo "$work/in"
    exec 3<>"$work/in"
    # shellcheck disable=SC2086
    "$@" $virtio -d guest_errors -serial stdio -display none -no-reboot \
        <"$work/in" >"$log" 2>&1 &
    qemu_pid=$!
    wait_for 'Type HELP for builtin commands' 120 || { echo "$0: EmuCON never started" >&2; show_log; summarize "never started"; exit 1; }
    sleep 2
    # the prompt is A:\ on the m68k machine and C:\ on ARM; the tests are on C:
    printf 'C:\r' >&3
    sleep 2
    printf 'runtests.tos\r' >&3
    ;;
pc-x86_64)
    mkdir -p "$work/esp/EFI/BOOT"
    cp "$top/pc-x86_64.efi" "$work/esp/EFI/BOOT/BOOTX64.EFI"
    cp -R "$top/test-dir/." "$work/esp/"
    firmware=${OVMF_DIR:-/usr/share/OVMF}
    cp "$firmware/OVMF_VARS_4M.fd" "$work/vars.fd"
    qemu-system-x86_64 -machine pc -m 256 -cpu qemu64,+pdpe1gb \
        -drive if=pflash,format=raw,readonly=on,file="$firmware/OVMF_CODE_4M.fd" \
        -drive if=pflash,format=raw,file="$work/vars.fd" \
        -drive format=raw,file=fat:rw:"$work/esp" \
        -serial file:"$log" -monitor unix:"$work/mon.sock",server,nowait \
        -display none >"$work/qemu.out" 2>&1 &
    qemu_pid=$!
    wait_for 'C:\>' 120 || { echo "$0: EmuCON never showed its prompt" >&2; show_log; summarize "never started"; exit 1; }
    sleep 1
    python3 "$tools/qemu-type.py" "$work/mon.sock" 'runtests.tos'
    ;;
*)
    echo "usage: $0 <virt-arm|virt-m68k|pc-x86_64> [timeout-seconds]" >&2
    exit 2
    ;;
esac

# The harness ends with one of two lines.
n=0
while [ "$n" -lt "$limit" ]; do
    grep -aqE 'All tests passed\.|Some tests FAILED\.' "$log" && break
    sleep 1
    n=$((n + 1))
done

show_log

if ! grep -aq 'All tests passed\.' "$log"; then
    summarize FAILED
    echo "$0: the regression suite did not pass on $machine" >&2
    exit 1
fi
if grep -aq 'FAIL' "$log"; then
    summarize FAILED
    echo "$0: a test reported FAIL on $machine" >&2
    exit 1
fi
summary=$(grep -a -- '--- Summary' "$log" | tail -1)
if [ -z "$summary" ]; then
    summarize FAILED
    echo "$0: no summary line in the output on $machine" >&2
    exit 1
fi
summarize passed
echo "$machine: $summary"
