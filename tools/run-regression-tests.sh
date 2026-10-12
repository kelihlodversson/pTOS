#!/bin/sh
#
# run-regression-tests.sh - run the regression suite under QEMU
#
# Usage: run-regression-tests.sh <virt-arm|virt-m68k|pc-x86_64> [timeout-seconds]
#
# Expects the matching configuration to be built and `make test-dir` to have
# collected the test programs into test-dir/ (QEMU's FAT driver presents a copy
# of the directory as the disk, so no image and no mtools are needed):
#
#   virt-arm     virt-arm-cli_defconfig       virt-arm.elf    qemu-system-arm
#   virt-m68k    virt-m68k-cli-elf_defconfig  virt-m68k.elf   qemu-system-m68k
#   pc-x86_64    pc-x86_64_defconfig          pc-x86_64.efi   qemu-system-x86_64 + OVMF
#
# The virt machines are configured without the AES so that they boot to
# EmuCON, where programs are started by typing their name into the serial
# console; pc-x86_64 boots to EmuCON in any case and is typed into through
# the QEMU monitor (PS/2 keyboard).
#
# Machine 1 runs runtests.tos, which must end with "All tests passed.", and
# then Shutdown(SHUT_POWER) (poweroff.tos): the machine must switch itself off,
# which ends QEMU (with a normal exit status; QEMU is not run with -no-reboot,
# so a reset in its place would show).  More machines follow for the rest of
# Shutdown() (reboot.tos, coldboot.tos: the machine starts again, a second
# EmuCON banner; halt.tos: the message, and the machine still running).  Each of the shutdown
# programs leaves a file open with data written, and the file must be on the
# disk afterwards, whole: Shutdown() closes the files and writes the buffers.
# The script returns 0 when all of that holds, and prints the console logs
# either way.
#
# QEMU does not exit by itself once a program is done, so the log is polled and
# QEMU is killed when the verdict is in or the time is up.
#

set -u

machine=${1:-}
limit=${2:-300}
tools=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$tools")
work=$(mktemp -d "${TMPDIR:-/tmp}/ptos-regression.XXXXXX")
qemu_pid=
qemu_status=0
log=
disk=
results=$work/results       # "name|PASS" or "name|FAIL" per line, in order
logs=$work/logs             # the console logs, in order
: >"$results"
: >"$logs"

stop_vm() {
    if [ -n "$qemu_pid" ]; then
        kill "$qemu_pid" 2>/dev/null
        wait "$qemu_pid" 2>/dev/null
        qemu_pid=
    fi
}

cleanup() {
    stop_vm
    rm -rf "$work"
}
trap cleanup EXIT

[ -d "$top/test-dir" ] || { echo "$0: no test-dir/: run 'make test-dir' first" >&2; exit 2; }

# record <name> <PASS|FAIL> [detail]
record() {
    echo "$1|$2" >>"$results"
    [ "$2" = PASS ] || echo "FAIL: $1 ${3:-}" >>"$log"
}

# show_logs: every console log, and what QEMU itself said (only separate from
# the console log on x86-64: a missing firmware file or a bad drive option ends
# up there, and the work directory is deleted on exit)
show_logs() {
    for l in $(cat "$logs"); do
        echo "::group::Console log ($machine, ${l##*/})"
        cat "$l" 2>/dev/null
        echo "::endgroup::"
        if [ -s "${l%.log}.qemu" ]; then
            echo "::group::QEMU output ($machine, ${l##*/})"
            cat "${l%.log}.qemu"
            echo "::endgroup::"
        fi
    done
}

# summarize <verdict>: on GitHub Actions, put the result on the job's summary
# page (a table of the suites and the shutdown checks, and the failure
# details), not only in the log
summarize() {
    [ -n "${GITHUB_STEP_SUMMARY:-}" ] || return 0
    first=$(head -1 "$logs")
    {
        echo "### Regression suite: $machine — $1"
        echo
        if [ -n "$first" ] && grep -aqE '^  [A-Za-z0-9_]+.*\.\.\. (PASS|FAIL)' "$first" 2>/dev/null; then
            echo "| Suite | Result |"
            echo "| --- | --- |"
            grep -aE '^  [A-Za-z0-9_]+.*\.\.\. (PASS|FAIL)' "$first" | tr -d '\r' |
                sed -E 's/^ +(.*)\.\.\. (PASS|FAIL)$/| \1 | \2 |/'
            sed -E 's/^(.*)\|(PASS|FAIL)$/| \1 | \2 |/' "$results"
            echo
            summary_line=$(grep -a -- '--- Summary' "$first" | tail -1 | tr -d '\r')
            [ -n "$summary_line" ] && echo "\`$summary_line\`" && echo
            if cat $(cat "$logs") 2>/dev/null | grep -aq 'FAIL: '; then
                echo "Failures:"
                echo
                echo '```'
                cat $(cat "$logs") | grep -a 'FAIL: ' | tr -d '\r'
                echo '```'
            fi
        else
            echo "The suite never produced a result; see the console log in the job output."
        fi
    } >>"$GITHUB_STEP_SUMMARY"
}

# die <message>: log, summary, exit
die() {
    echo "$0: $1" >&2
    show_logs
    summarize "$2"
    exit 1
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

# wait_exit <seconds>: wait until QEMU has gone by itself, and normally: the
# exit status is collected (so a QEMU that crashed after the guest printed its
# PASS is not taken for a power off) and must be 0
wait_exit() {
    n=0
    while kill -0 "$qemu_pid" 2>/dev/null && [ "$n" -lt "$1" ]; do
        sleep 1
        n=$((n + 1))
    done
    if kill -0 "$qemu_pid" 2>/dev/null; then
        return 1
    fi
    wait "$qemu_pid"
    qemu_status=$?
    qemu_pid=
    [ "$qemu_status" -eq 0 ]
}

# type_line <text>: type a line into the guest's console
type_line() {
    case $machine in
    pc-x86_64) python3 "$tools/qemu-type.py" "$work/mon.sock" "$1" ;;
    *) printf '%s\r' "$1" >&3 ;;
    esac
}

# start_vm <label>: boot a machine on a copy of test-dir/ and get it to the
# EmuCON prompt on C:
start_vm() {
    label=$1
    stop_vm
    log=$work/$label.log
    echo "$log" >>"$logs"
    disk=$work/disk-$label
    mkdir -p "$disk"
    cp -R "$top/test-dir/." "$disk/"
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
        rm -f "$work/in"
        mkfifo "$work/in"
        exec 3<>"$work/in"
        # shellcheck disable=SC2086
        "$@" -global virtio-mmio.force-legacy=false \
            -drive if=none,id=hd0,format=raw,file.driver=vvfat,file.dir="$disk",file.rw=on \
            -device virtio-blk-device,drive=hd0 \
            -d guest_errors -serial stdio -display none \
            <"$work/in" >"$log" 2>"${log%.log}.qemu" &
        qemu_pid=$!
        wait_for 'Type HELP for builtin commands' 120 || die "EmuCON never started ($label)" "never started"
        sleep 2
        # the prompt is A:\ on the m68k machine and C:\ on ARM; the tests are on C:
        type_line 'C:'
        sleep 2
        ;;
    pc-x86_64)
        mkdir -p "$disk/EFI/BOOT"
        cp "$top/pc-x86_64.efi" "$disk/EFI/BOOT/BOOTX64.EFI"
        firmware=${OVMF_DIR:-/usr/share/OVMF}
        cp "$firmware/OVMF_VARS_4M.fd" "$work/vars-$label.fd"
        rm -f "$work/mon.sock"
        qemu-system-x86_64 -machine pc -m 256 -cpu qemu64,+pdpe1gb \
            -drive if=pflash,format=raw,readonly=on,file="$firmware/OVMF_CODE_4M.fd" \
            -drive if=pflash,format=raw,file="$work/vars-$label.fd" \
            -drive format=raw,file=fat:rw:"$disk" \
            -serial file:"$log" -monitor unix:"$work/mon.sock",server,nowait \
            -display none >"${log%.log}.qemu" 2>&1 &
        qemu_pid=$!
        wait_for 'C:\>' 120 || die "EmuCON never showed its prompt ($label)" "never started"
        sleep 1
        ;;
    *)
        echo "usage: $0 <virt-arm|virt-m68k|pc-x86_64> [timeout-seconds]" >&2
        exit 2
        ;;
    esac
}

# data_file_ok: after QEMU has gone, SHUTDOWN.DAT (shutdown_prog.c) must be on
# the disk, 512 bytes, byte i being i * 7 + 3
data_file_ok() {
    python3 - "$disk" <<'PY'
import os, sys
d = sys.argv[1]
names = {n.upper(): n for n in os.listdir(d)}
if "SHUTDOWN.DAT" not in names:
    sys.exit(1)
data = open(os.path.join(d, names["SHUTDOWN.DAT"]), "rb").read()
sys.exit(0 if data == bytes((i * 7 + 3) & 255 for i in range(512)) else 1)
PY
}

# shutdown_check <label> <text>: the program has been started; QEMU must go by
# itself, normally, having printed the text, and the data file must be on the
# disk.  (QEMU is not run with -no-reboot: a reset that should have been a power
# off would restart the machine instead of ending QEMU.)
shutdown_check() {
    if ! wait_for "shutdown: PASS ($2)" 60; then
        record "shutdown ($1)" FAIL "the program never ran"
    elif ! wait_exit 60; then
        if [ -z "$qemu_pid" ]; then
            record "shutdown ($1)" FAIL "QEMU ended with exit status $qemu_status"
        else
            record "shutdown ($1)" FAIL "QEMU was still running 60 s after it"
        fi
    elif ! data_file_ok; then
        record "shutdown ($1)" FAIL "the file left open was not on the disk"
    else
        record "shutdown ($1)" PASS
    fi
}

# reboot_run <label> <program> <text>: a machine of its own runs the program;
# it must print the text, not switch off, and start again (a second EmuCON
# banner), and the data file must be on the disk
reboot_run() {
    start_vm "$1"
    type_line "$2"
    if ! wait_for "shutdown: PASS ($3)" 60; then
        record "shutdown ($1)" FAIL "the program never ran"
        return
    fi
    # a machine that was switched off, not restarted, takes QEMU with it
    sleep 15
    if ! kill -0 "$qemu_pid" 2>/dev/null; then
        record "shutdown ($1)" FAIL "the machine switched off instead of restarting"
        return
    fi
    # ... and one whose reset did nothing falls back to the halt message and
    # stays up, which looks the same to QEMU
    if grep -aq 'safe to turn off your computer' "$log"; then
        record "shutdown ($1)" FAIL "the reset did nothing, the machine halted"
        return
    fi
    # ... and a restart brings a second EmuCON banner
    n=0
    while [ "$(grep -ac 'Type HELP for builtin commands' "$log")" -lt 2 ] && [ "$n" -lt 90 ]; do
        sleep 1
        n=$((n + 1))
    done
    if [ "$(grep -ac 'Type HELP for builtin commands' "$log")" -lt 2 ]; then
        record "shutdown ($1)" FAIL "the machine did not start again"
        return
    fi
    stop_vm
    if data_file_ok; then
        record "shutdown ($1)" PASS
    else
        record "shutdown ($1)" FAIL "the file left open was not on the disk"
    fi
}

# -- machine 1: the suite, then the power off ---------------------------------

start_vm suite
type_line 'runtests.tos'

# The harness ends with one of two lines.
n=0
while [ "$n" -lt "$limit" ]; do
    grep -aqE 'All tests passed\.|Some tests FAILED\.' "$log" && break
    sleep 1
    n=$((n + 1))
done
suite_log=$log

if grep -aq 'All tests passed\.' "$suite_log" && ! grep -aq 'FAIL' "$suite_log"; then
    type_line 'poweroff.tos'
    shutdown_check poweroff "powering off"

    # -- the machines of the other modes --------------------------------------

    reboot_run reboot reboot.tos "rebooting"
    reboot_run coldboot coldboot.tos "cold booting"

    # halt: the message, the machine still there, and the file on the disk
    start_vm halt
    type_line 'halt.tos'
    if ! wait_for 'safe to turn off your computer' 60; then
        record "shutdown (halt)" FAIL "no message"
    else
        sleep 3
        if ! kill -0 "$qemu_pid" 2>/dev/null; then
            record "shutdown (halt)" FAIL "QEMU left, the machine should have stopped"
        else
            stop_vm
            if data_file_ok; then
                record "shutdown (halt)" PASS
            else
                record "shutdown (halt)" FAIL "the file left open was not on the disk"
            fi
        fi
    fi
fi

# -- the verdict ----------------------------------------------------------------

show_logs

if ! grep -aq 'All tests passed\.' "$suite_log"; then
    summarize FAILED
    echo "$0: the regression suite did not pass on $machine" >&2
    exit 1
fi
if grep -aq 'FAIL' "$suite_log" || grep -q '|FAIL$' "$results"; then
    summarize FAILED
    echo "$0: a test reported FAIL on $machine" >&2
    exit 1
fi
summary=$(grep -a -- '--- Summary' "$suite_log" | tail -1)
if [ -z "$summary" ]; then
    summarize FAILED
    echo "$0: no summary line in the output on $machine" >&2
    exit 1
fi
summarize passed
echo "$machine: $summary, shutdown checks: $(grep -c '|PASS$' "$results") passed"
