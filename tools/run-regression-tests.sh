#!/bin/sh
#
# run-regression-tests.sh - run the regression suite under QEMU
#
# Usage: run-regression-tests.sh <virt-arm|virt-m68k|virt-m68k-mmu|virt-m68k-aspace|pc-x86_64|raspi2b|raspi2b-aspace> [timeout-seconds]
#
# Expects the matching configuration to be built and `make test-dir` to have
# collected test programs into test-dir/. The virt boards use QEMU's FAT
# directory driver. x32 uses a real FAT disk image (dosfstools and mtools)
# so unlink lifetimes and shutdown writes are checked on block storage:
#
#   virt-arm     virt-arm-cli_defconfig       virt-arm.elf    qemu-system-arm
#   virt-m68k    virt-m68k-cli-elf_defconfig  virt-m68k.elf   qemu-system-m68k
#   virt-m68k-mmu  virt-m68k-mmu_defconfig    virt-m68k.elf   qemu-system-m68k -cpu m68040
#                (the higher-half kernel with the MMU on)
#   virt-m68k-aspace  virt-m68k-aspace_defconfig  virt-m68k.elf  qemu-system-m68k -cpu m68040
#                (the same, every process in an address space of its own)
#   pc-x86_64    pc-x86_64-desktop_defconfig  pc-x86_64.efi   qemu-system-x86_64 + OVMF
#   raspi2b      rpi2-test_defconfig          kernel7.img     qemu-system-arm
#   raspi2b-aspace  rpi2-aspace_defconfig           kernel7.img     qemu-system-arm -M raspi2b
#                (EmuCON on the UART, every process in an address space of its own; the
#                suite only: QEMU's raspi2b does not switch off)
#
# The virt machines are configured without the AES so that they boot to
# EmuCON, where programs are started by typing their name into the serial
# console. The x32 and Pi desktops launch the suite using emudesk.inf's
# autorun line. On x32, the shutdown probes also autorun in separate boots.
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
cuis_input_pid=
qemu_status=0
log=
disk=
disk_image=
results=$work/results       # "name|PASS" or "name|FAIL" per line, in order
logs=$work/logs             # the console logs, in order
autorun=false
[ "$machine" != raspi2b ] || autorun=true
if [ "$machine" = pc-x86_64 ] && grep -q '^#define CONF_EXTERNAL_DESK 1' "$top/obj/autoconf.h"; then
    autorun=true
fi
: >"$results"
: >"$logs"

stop_vm() {
    if [ -n "$cuis_input_pid" ]; then
        kill "$cuis_input_pid" 2>/dev/null
        wait "$cuis_input_pid" 2>/dev/null
        cuis_input_pid=
    fi
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
if [ "${SHARED_VM_PEXEC_TEST:-0}" = 1 ] &&
   ! grep -q '^#define CONF_WITH_SHARED_VM_PEXEC_TEST 1$' "$top/obj/autoconf.h"; then
    echo "$0: requested positive shared-Pexec but the effective gate is disabled" >&2
    exit 2
fi

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
    [ "$autorun" != true ] || return 0
    case $machine in
    pc-x86_64) python3 "$tools/qemu-type.py" "$work/mon.sock" "$1" ;;
    *) printf '%s\r' "$1" >&3 ;;
    esac
}

# start_vm <label>: boot a machine on a copy of test-dir/ and get it to the
# EmuCON prompt on C:, or to the desktop's INF autorun.
start_vm() {
    label=$1
    stop_vm
    log=$work/$label.log
    echo "$log" >>"$logs"
    disk=$work/disk-$label
    mkdir -p "$disk"
    cp -R "$top/test-dir/." "$disk/"
    if [ "$label" = cuis ]; then
        cp "$disk/runtests.tos" "$disk/RUNTESTS.PRG"
        : > "$disk/HISTGUI.ON"
    fi
    if [ "$machine" = pc-x86_64 ] && [ "$autorun" = true ]; then
        # Keep the same desktop autorun path for the suite and all shutdown
        # probes. No monitor keystrokes or early kernel test hook starts them.
        case $label in
        suite | sysfont) program=RUNTESTS.TOS ;;
        cuis) program=RUNTESTS.PRG ;;
        poweroff | reboot | coldboot | halt) program=$(printf '%s.TOS' "$label" | tr '[:lower:]' '[:upper:]') ;;
        *) die "unknown autorun label $label" "never started" ;;
        esac
        python3 - "$disk/emudesk.inf" "$program" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
lines = p.read_text().splitlines()
assert sum(line.startswith("#Z ") for line in lines) == 1
kind = "01" if sys.argv[2] == "RUNTESTS.PRG" else "00"
p.write_bytes(("\r\n".join("#Z " + kind + " C:\\" + sys.argv[2] + "@" if line.startswith("#Z ") else line
                          for line in lines) + "\r\n").encode("ascii"))
PY
    fi
    # scalable fonts for the built-in GDOS (gdos_answer), when the host
    # has them: the default set of the release images (DejaVu Sans, Serif
    # and Sans Mono, regular and bold: the names tools/fetch-fonts.sh
    # gives them), OpenType with CFF outlines (Inter, and its italic, whose
    # glyphs leave their cells), one whose ascender
    # is taller than its em (Loma) and a monospaced one whose combining
    # marks have no width (Liberation Mono), under 8.3 names
    dv=/usr/share/fonts/truetype/dejavu
    for f in $dv/DejaVuSans.ttf:DJVUSANS.TTF \
             $dv/DejaVuSans-Bold.ttf:DJVUSANB.TTF \
             $dv/DejaVuSerif.ttf:DJVUSERF.TTF \
             $dv/DejaVuSerif-Bold.ttf:DJVUSERB.TTF \
             $dv/DejaVuSansMono.ttf:DJVUMONO.TTF \
             $dv/DejaVuSansMono-Bold.ttf:DJVUMONB.TTF \
             /usr/share/fonts/opentype/inter/Inter-Regular.otf:INTER.OTF \
             /usr/share/fonts/opentype/inter/Inter-Italic.otf:INTERIT.OTF \
             /usr/share/fonts/opentype/tlwg/Loma.otf:LOMA.OTF \
             /usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf:LIBMONO.TTF; do
        if [ -f "${f%%:*}" ]; then
            mkdir -p "$disk/GEMSYS/FONTS"
            cp "${f%%:*}" "$disk/GEMSYS/FONTS/${f##*:}"
        fi
    done
    # and a TrueType collection of the regular and bold Liberation Sans
    # (fontTools), whose two faces are fonts of their own
    lib=/usr/share/fonts/truetype/liberation
    if [ -f "$lib/LiberationSans-Regular.ttf" ] && [ -f "$lib/LiberationSans-Bold.ttf" ]; then
        mkdir -p "$disk/GEMSYS/FONTS"
        python3 -c 'import sys
from fontTools.ttLib import TTFont
from fontTools.ttLib.ttCollection import TTCollection
c = TTCollection()
c.fonts = [TTFont(f) for f in sys.argv[2:]]
c.save(sys.argv[1])' "$disk/GEMSYS/FONTS/LIBSANS.TTC" \
            "$lib/LiberationSans-Regular.ttf" "$lib/LiberationSans-Bold.ttf" 2>/dev/null \
            || { rm -f "$disk/GEMSYS/FONTS/LIBSANS.TTC"
                 echo "$0: no TrueType collection for the font tests (python3 fontTools missing?)" >&2; }
    fi
    # a scalable font as the system font (#600), for the machine that is to
    # have one: the setting file, with a font that is on the disk
    if [ -n "${system_font:-}" ] && [ -f "$disk/GEMSYS/FONTS/$system_font" ]; then
        printf 'SYSTEMFONT=%s\r\n' "$system_font" >"$disk/GEMSYS/PTOS.INF"
    fi
    case $machine in
    virt-arm | virt-m68k | virt-m68k-mmu | virt-m68k-aspace)
        if [ "$machine" = virt-arm ]; then
            set -- qemu-system-arm -M virt,highmem=off -cpu cortex-a7 -m 128 \
                -kernel "$top/virt-arm.elf"
        elif [ "$machine" = virt-m68k-mmu ] || [ "$machine" = virt-m68k-aspace ]; then
            set -- qemu-system-m68k -M virt -cpu m68040 -m 128 \
                -kernel "$top/virt-m68k.elf"
        else
            set -- qemu-system-m68k -M virt -cpu m68020 -m 128 \
                -kernel "$top/virt-m68k.elf"
        fi
        if [ -n "${QEMU_DATA_DIR:-}" ]; then
            set -- "$@" -L "$QEMU_DATA_DIR"
        fi
        # a host directory over virtio-9p as well (Q:), for the suites
        # that test the pluggable file system (pfs_seek)
        host=$work/host-$label
        mkdir -p "$host"
        # stdin is a FIFO held open from here, so that QEMU's serial console
        # never sees an end of file; the commands are written to it.
        rm -f "$work/in"
        mkfifo "$work/in"
        exec 3<>"$work/in"
        # Raw sectors are useful where vvfat's /var/tmp backing is unavailable.
        if [ "${REGRESSION_RAW_DISK:-0}" = 1 ]; then
            disk_image=$work/disk-$label.img
            "$tools/mkhdisk.sh" "$disk_image" 33554432 "$disk"/* >"$work/image-$label.log" 2>&1 ||
                die "cannot create regression disk" "disk creation failed"
            set -- "$@" -drive "if=none,id=hd0,format=raw,file=$disk_image"
        else
            set -- "$@" -drive "if=none,id=hd0,format=raw,file.driver=vvfat,file.dir=$disk,file.rw=on"
        fi
        # shellcheck disable=SC2086
        "$@" -global virtio-mmio.force-legacy=false \
            -device virtio-blk-device,drive=hd0 \
            -fsdev local,id=host0,path="$host",security_model=none \
            -device virtio-9p-device,fsdev=host0,mount_tag=host \
            -d guest_errors -serial stdio -display none \
            <"$work/in" >"$log" 2>"${log%.log}.qemu" &
        qemu_pid=$!
        wait_for 'Type HELP for builtin commands' 120 || die "EmuCON never started ($label)" "never started"
        sleep 2
        # the prompt is A:\ on the m68k machine and C:\ on ARM; the tests are on C:
        type_line 'C:'
        sleep 2
        ;;
    raspi2b-aspace)
        # EmuCON on the UART (rpi2-aspace_defconfig), its programs in address spaces of
        # their own; the SD card as below
        rm -f "$work/in"
        mkfifo "$work/in"
        exec 3<>"$work/in"
        qemu-system-arm -M raspi2b -bios "$top/kernel7.img" \
            -drive if=sd,driver=raw,size=256M,file.driver=vvfat,file.dir="$disk",file.rw=on \
            -d guest_errors -serial stdio -display none -monitor none \
            <"$work/in" >"$log" 2>"${log%.log}.qemu" &
        qemu_pid=$!
        wait_for 'Type HELP for builtin commands' 120 || die "EmuCON never started ($label)" "never started"
        sleep 2
        type_line 'C:'
        sleep 2
        ;;
    raspi2b)
        # the desktop, with the console on the UART too (rpi2-test_defconfig);
        # emudesk.inf's autorun line starts runtests.tos, whose output is the log.
        # The SD card has to be a power of two in size: vvfat gets a window of 256M
        qemu-system-arm -M raspi2b -bios "$top/kernel7.img" \
            -drive if=sd,driver=raw,size=256M,file.driver=vvfat,file.dir="$disk",file.rw=on \
            -d guest_errors -serial file:"$log" -display none -monitor none \
            >"${log%.log}.qemu" 2>&1 &
        qemu_pid=$!
        ;;
    pc-x86_64)
        mkdir -p "$disk/EFI/BOOT"
        cp "$top/pc-x86_64.efi" "$disk/EFI/BOOT/BOOTX64.EFI"
        firmware=${OVMF_DIR:-/usr/share/OVMF}
        cp "${OVMF_VARS:-$firmware/OVMF_VARS_4M.fd}" "$work/vars-$label.fd"
        # vvfat mirrors a host directory asynchronously and can lose the
        # shutdown file when a reset/halt test stops QEMU. Use real sectors,
        # including unlinked-but-referenced files' allocated FAT chains.
        disk_image=$work/disk-$label.img
        if ! "$tools/mkhdisk.sh" "$disk_image" 33554432 "$disk"/* >"$work/image-$label.log" 2>&1; then
            cat "$work/image-$label.log" >&2
            die "cannot create regression disk" "disk creation failed"
        fi
        rm -f "$work/mon.sock"
        qemu-system-x86_64 -machine pc -m 256 -cpu qemu64,+pdpe1gb \
            -drive if=pflash,format=raw,readonly=on,file="${OVMF_CODE:-$firmware/OVMF_CODE_4M.fd}" \
            -drive if=pflash,format=raw,file="$work/vars-$label.fd" \
            -drive format=raw,file="$disk_image" \
            -serial file:"$log" -monitor unix:"$work/mon.sock",server,nowait \
            -display none >"${log%.log}.qemu" 2>&1 &
        qemu_pid=$!
        case $label in
        cuis)
            if grep -q '^#define CONF_WITH_CUIS 1' "$top/obj/autoconf.h" &&
               grep -q '^#define CONF_WITH_PTERMGRP 1' "$top/obj/autoconf.h" &&
               grep -q '^#define CONF_WITH_PTHJOIN 1' "$top/obj/autoconf.h"; then
                python3 "$tools/exercise-cuis-resume.py" "$work/mon.sock" "$log" "$limit" >"$work/cuis-$label.log" 2>&1 &
                cuis_input_pid=$!
            fi
            ;;
        esac
        if [ "$autorun" != true ]; then
            wait_for 'C:\>' 120 || die "EmuCON never showed its prompt ($label)" "never started"
            sleep 1
        fi
        ;;
    *)
        echo "usage: $0 <virt-arm|virt-m68k|virt-m68k-mmu|virt-m68k-aspace|pc-x86_64|raspi2b|raspi2b-aspace> [timeout-seconds]" >&2
        exit 2
        ;;
    esac
}

# data_file_ok: after QEMU has gone, SHUTDOWN.DAT (shutdown_prog.c) must be on
# the disk, 512 bytes, byte i being i * 7 + 3
data_file_ok() {
    python3 - "$disk" "$disk_image" <<'PY'
import os, subprocess, sys
if sys.argv[2]:
    try:
        data = subprocess.check_output(["mtype", "-i", sys.argv[2] + "@@1048576",
                                        "::SHUTDOWN.DAT"], stderr=subprocess.DEVNULL)
    except subprocess.CalledProcessError:
        sys.exit(1)
else:
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
    # With INF autorun a reset immediately starts the next probe. Wait for
    # its EFI entry instead of allowing a reboot loop during a fixed delay.
    banner='Type HELP for builtin commands'
    if [ "$machine" = pc-x86_64 ] && [ "$autorun" = true ]; then
        banner='EFI entry reached'
    else
        sleep 15
    fi
    # a machine that was switched off, not restarted, takes QEMU with it
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
    # ... and a restart brings a second boot banner
    n=0
    while [ "$(grep -ac "$banner" "$log")" -lt 2 ] && [ "$n" -lt 90 ]; do
        sleep 1
        n=$((n + 1))
    done
    if [ "$(grep -ac "$banner" "$log")" -lt 2 ]; then
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

finish_cuis_input() {
    if [ -n "$cuis_input_pid" ]; then
        if wait "$cuis_input_pid"; then echo "native CUIS hardware resume: PASS"
        else record "native CUIS hardware resume" FAIL "hardware input or native worker failed"
        fi
        cuis_input_pid=
        cat "$work/cuis-$label.log"
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
if grep -aq 'Panic:' "$suite_log"; then
    record "kernel panic" FAIL "regression execution crossed a kernel panic"
fi
finish_cuis_input

if [ "$machine" != raspi2b ] && [ "$machine" != raspi2b-aspace ] && grep -aq 'All tests passed\.' "$suite_log" && ! grep -aq 'FAIL' "$suite_log"; then
    if [ "$machine" = pc-x86_64 ] && [ "$autorun" = true ]; then
        start_vm poweroff
    fi
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

# -- a machine with a scalable system font (#600) -----------------------------
#
# The whole suite again, on a screen whose system font is a monospaced
# scalable one, when the host has one (DejaVu Sans Mono, that is on the disk
# for the other tests): every program and test that draws text then does it
# with that font, and the log must say that it was made.

# (and only for a build that has scalable fonts: the kernel of the tree in $top)
# A console-owned TOS launch deliberately suppresses GUI button input. Keep
# that compatibility run, and exercise the public GUI wait in a separate PRG
# launch without a console lease. Never bypass ownership in the kernel.
if [ "$machine" = pc-x86_64 ] && [ "$autorun" = true ] &&
   grep -aq 'All tests passed\.' "$suite_log" &&
   grep -q '^#define CONF_WITH_CUIS 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_PTERMGRP 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_PTHJOIN 1' "$top/obj/autoconf.h"; then
    start_vm cuis
    type_line 'runtests.tos'
    n=0
    while [ "$n" -lt "$limit" ]; do
        grep -aqE 'All tests passed\.|Some tests FAILED\.' "$log" && break
        sleep 1
        n=$((n+1))
    done
    finish_cuis_input
    if grep -aq 'All tests passed\.' "$log" && ! grep -aq 'FAIL' "$log" &&
       grep -aq 'native CUIS resume/live commit: PASS' "$log" &&
       grep -aq 'native CUIS history cancellation: PASS' "$log"; then
        echo "native GUI resume worker: PASS"
    else
        record "native GUI resume worker" FAIL "public GUI worker did not pass"
    fi
    stop_vm
fi

if [ "$machine" != raspi2b ] && [ "$machine" != raspi2b-aspace ] && grep -aq 'All tests passed\.' "$suite_log" && [ -f /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf ] \
   && grep -q '^#define CONF_WITH_VDI_SCALABLE_FONTS 1' "$top/obj/autoconf.h" 2>/dev/null; then
    system_font=DJVUMONO.TTF
    start_vm sysfont
    system_font=
    type_line 'runtests.tos'
    n=0
    while [ "$n" -lt "$limit" ]; do
        grep -aqE 'All tests passed\.|Some tests FAILED\.' "$log" && break
        sleep 1
        n=$((n + 1))
    done
    if ! grep -aq 'is the system font' "$log"; then
        record "scalable system font" FAIL "the kernel did not make the font the system font"
    elif grep -aq 'All tests passed\.' "$log" && ! grep -aq 'FAIL' "$log"; then
        record "scalable system font" PASS
    else
        record "scalable system font" FAIL "the suite did not pass with it"
    fi
    finish_cuis_input
    stop_vm
fi

# A positive shared-Pexec build must execute every configured consumer fixture.
# The ordinary gate-off jobs remain independent compatibility evidence.
if grep -q '^#define CONF_WITH_SHARED_VM_PEXEC_TEST 1' "$top/obj/autoconf.h"; then
    for marker in \
        'native shared-VM Pexec4/104 execution and canonical reap: tested' \
        'native shared-VM full-status/fault and canonical wait: tested' \
        'native shared-VM unstarted-child source retirement: tested' \
        'native shared-VM running orphan and nested unstarted cleanup: tested' \
        'native shared-VM orphan grandchild execution after source retirement: tested' \
        'native shared-VM inherited limits and exact stack authority: tested' \
        'native shared-VM live-target and self-relaunch ownership rejection: tested'; do
        if ! grep -aqF "$marker" "$suite_log"; then
            record "native shared-Pexec execution" FAIL "missing consumer assertion: $marker"
        fi
    done
    if grep -q '^#define CONF_WITH_PTERMGRP 1' "$top/obj/autoconf.h" &&
       ! grep -aqF 'native shared-VM PROCESS-local group cancellation and orphan survival: tested' "$suite_log"; then
        record "native shared-Pexec group exit" FAIL "PROCESS-local group assertion did not execute"
    fi
    if grep -q '^#define CONF_WITH_PTHJOIN 1' "$top/obj/autoconf.h" &&
       grep -q '^#define CONF_WITH_PCLONE 1' "$top/obj/autoconf.h" &&
       ! grep -aqF 'native shared-VM final-THREAD completion and competing waiter: tested' "$suite_log"; then
        record "native shared-Pexec final THREAD" FAIL "managed lifetime assertions did not execute"
    fi
    if grep -q '^#define CONF_WITH_PRIVATE_CLONE_TEST 1' "$top/obj/autoconf.h" &&
       ! grep -aqF 'native shared-VM constructor/observer/prepublication rollback and retry: tested' "$suite_log"; then
        record "native shared-Pexec rollback" FAIL "configured factory injections did not execute"
    fi
    if grep -q '^#define CONF_WITH_PRIVATE_CLONE_TEST 1' "$top/obj/autoconf.h"; then
        for marker in \
            'native shared-VM production kernel memmove overlap: tested' \
            'native shared-VM four-boundary resource conservation and multiple surviving orphans: tested' \
            'native shared-VM exhaustive preparation prefixes and exact rollback conservation: tested'; do
            if ! grep -aqF "$marker" "$suite_log"; then
                record "native shared-Pexec ownership/cancellation" FAIL "missing consumer assertion: $marker"
            fi
        done
    fi
    if grep -q '^#define CONF_WITH_PRIVATE_CLONE_TEST 1' "$top/obj/autoconf.h" &&
       grep -q '^#define CONF_WITH_PCLONE 1' "$top/obj/autoconf.h" &&
       grep -q '^#define CONF_WITH_PTHJOIN 1' "$top/obj/autoconf.h" &&
       grep -q '^#define CONF_WITH_PTERMGRP 1' "$top/obj/autoconf.h" &&
       ! grep -aqF 'native shared-VM Pclone and nested source convergence, wait ABIs and protected cancellation: tested' "$suite_log"; then
        record "native shared-Pexec protected wait" FAIL "logical-owner and cancellation assertions did not execute"
    fi
    if grep -q '^#define CONF_WITH_MMAP 1' "$top/obj/autoconf.h" &&
       ! grep -aqF 'native shared-VM absent file backing and resident file-COW: tested' "$suite_log"; then
        record "native shared-Pexec file mappings" FAIL "file backing/COW assertions did not execute"
    fi
    if grep -q '^#define CONF_WITH_ELF_SHARED 1' "$top/obj/autoconf.h"; then
        for marker in \
            'native shared-VM PT_INTERP Pexec4/104 standard entry: tested' \
            'native shared-VM simultaneous and completed-image peers: tested' \
            'native shared-VM parent-first interpreter execution and finalization: tested'; do
            if ! grep -aqF "$marker" "$suite_log"; then
                record "native shared-Pexec interpreter" FAIL "missing consumer assertion: $marker"
            fi
        done
        if grep -q '^#define CONF_WITH_PRIVATE_CLONE_TEST 1' "$top/obj/autoconf.h"; then
            for marker in \
                'native shared-VM 128 interpreted Pexec4/104 normal/fault-before-fini exact 45-counter conservation: tested' \
                'native shared-VM generic DSO peer creator-first normal/fault lifetime and exact 45-counter conservation: tested' \
                'native shared-VM generic executable dependency prefixes, surviving owner rollback and retry: tested' \
                'native shared-VM late runtime file-COW and loaded descendant after both creators retire: tested'; do
                if ! grep -aqF "$marker" "$suite_log"; then
                    record "native shared-Pexec DSO lifetime" FAIL "missing positive consumer execution: $marker"
                fi
            done
        fi
    fi
    if grep -q '^#define ARCH_M68K 1' "$top/obj/autoconf.h" &&
       ! grep -aqF 'native shared-VM PRG Pexec4/104 entry, status and shared data: tested' "$suite_log"; then
        record "native shared-Pexec PRG" FAIL "PRG assertions did not execute"
    fi
fi

# Configured nested ownership checks must execute as managed THREADs.
if grep -q '^#define CONF_WITH_PRIVATE_CLONE_TEST 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_NATIVE_SCHED 1' "$top/obj/autoconf.h"; then
    for marker in \
        'native nested Pexec identity/exit modes/restoration: PASS' \
        'native nested Pexec construction rollback: PASS' \
        'native nested Pexec resource conservation: PASS'; do
        if ! grep -aq "$marker" "$suite_log"; then
            record "native nested continuation ownership" FAIL "missing $marker"
        fi
    done
    if grep -q '^#define CONF_WITH_PTERMGRP 1' "$top/obj/autoconf.h" &&
       ! grep -aq 'native nested Pexec cancellation during reclamation: PASS' "$suite_log"; then
        record "native nested continuation cancellation" FAIL "configured reclamation checkpoint did not pass"
    fi
fi

# Configured shared-root lifetime probes must execute on the real launcher.
if grep -q '^#define CONF_WITH_PRIVATE_CLONE_TEST 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_NATIVE_SCHED 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_MMAP 1' "$top/obj/autoconf.h"; then
    for marker in \
        'native shared-root common/scoped/late mapping lifetime: PASS' \
        'native shared-root peer/last-THREAD/orphan safety: PASS' \
        'native shared-root guarded peer contention: PASS' \
        'native shared-root exact mapping resource conservation: PASS'; do
        if ! grep -aq "$marker" "$suite_log"; then
            record "native shared-root mapping ownership" FAIL "missing $marker"
        fi
    done
    if grep -q '^#define CONF_WITH_PTERMGRP 1' "$top/obj/autoconf.h" &&
       ! grep -aq 'native shared-root guarded waiter cancellation: PASS' "$suite_log"; then
        record "native shared-root waiter cancellation" FAIL "configured guarded wait cancellation did not pass"
    fi
fi

# The experimental VM test must execute, not merely compile.
if grep -q '^#define CONF_WITH_PRIVATE_CLONE_TEST 1' "$top/obj/autoconf.h" &&
   { ! grep -aq 'native independent KASPACE/COW entry probe (not Pfork): PASS' "$suite_log" ||
     ! grep -aq 'native private orphan COW: PASS' "$suite_log"; }; then
    record "native independent VM/COW" FAIL "configured private clone probe did not pass"
fi

# A configured rollback injection must actually have reached a user exit.
if grep -q '^#define CONF_WITH_EXIT_HANDOFF_TEST 1' "$top/obj/autoconf.h" &&
   ! grep -aq 'exit handoff: injected preparation rollback' "$suite_log"; then
    record "exit handoff rollback" FAIL "configured injection was not exercised"
fi

# The configured ownership probe must execute actual allocator/MMU operations.
if grep -q '^#define CONF_WITH_EXIT_HANDOFF_TEST 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_NATIVE_SCHED 1' "$top/obj/autoconf.h" &&
   ! grep -aq 'native allocation/reservation ownership and orphan reclamation: PASS' "$suite_log"; then
    record "native region ownership" FAIL "configured allocator/reservation probe did not execute"
fi

# Generation publication in the check-to-block window must reject sleeping.
if grep -q '^#define CONF_WITH_EXIT_HANDOFF_TEST 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_NATIVE_SCHED 1' "$top/obj/autoconf.h" &&
   ! grep -aq 'native completion sequence check/block retry: PASS' "$suite_log"; then
    record "native completion sequence" FAIL "check/block sequence assertion did not execute"
fi

# A generation change after BLOCKED must wake even if readiness is now false.
if grep -q '^#define CONF_WITH_EXIT_HANDOFF_TEST 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_NATIVE_SCHED 1' "$top/obj/autoconf.h" &&
   ! grep -aq 'native owner generation wake with false predicate: PASS' "$suite_log"; then
    record "native owner generation wake" FAIL "blocked owner-generation assertion did not execute"
fi

if grep -q '^#define CONF_WITH_EXIT_HANDOFF_TEST 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_NATIVE_SCHED 1' "$top/obj/autoconf.h" &&
   ! grep -aq 'native IRQ wake/idle admission and blocked-owner resume: PASS' "$suite_log"; then
    record "native IRQ wake/idle" FAIL "real IRQ idle/owner wake assertion did not execute"
fi

# Test-only assertions cross a real user PROCESS retirement and root reap.
if grep -q '^#define CONF_WITH_EXIT_HANDOFF_TEST 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_PCLONE 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_NATIVE_SCHED 1' "$top/obj/autoconf.h" &&
   ! grep -aq 'native scheduler durable controller/completion abandonment: PASS' "$suite_log"; then
    record "native scheduler lifetime" FAIL "retained-result retirement assertions did not execute"
fi

# Internal TID result waits execute through native root suspension/resume;
# these are kernel assertions, not global background-admission evidence.
if grep -q '^#define CONF_WITH_EXIT_HANDOFF_TEST 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_PCLONE 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_NATIVE_SCHED 1' "$top/obj/autoconf.h"; then
    if ! grep -aq 'native managed TID admission wait/resume: PASS' "$suite_log" ||
       ! grep -aq 'native quiescent waiter retirement: PASS' "$suite_log"; then
        record "native TID admission waiter" FAIL "suspend/resume and quiescent retirement assertions did not execute"
    fi
fi

# Compilation and legacy compatibility are insufficient evidence of native
# startup. Require managed startup and scheduler evidence on all three MMUs.
case "$machine" in
    virt-arm|virt-m68k-aspace|pc-x86_64)
        if grep -q '^#define CONF_WITH_NATIVE_SCHED 1' "$top/obj/autoconf.h" &&
           grep -q '^#define CONF_WITH_PCLONE 1' "$top/obj/autoconf.h"; then
            if ! grep -aq 'pclone native runnable NEW_PROCESS/SAME_PROCESS: PASS' "$suite_log"; then
                record "native managed clone execution" FAIL "native clone worker did not execute"
            fi
            if ! grep -aq 'native Pclone ABI prefix/flags/pointers/atomic outputs: PASS' "$suite_log"; then
                record "native Pclone ABI contract" FAIL "public trap matrix did not execute successfully"
            fi
            if ! grep -aq 'native shared image execute policy nested/orphan: PASS' "$suite_log"; then
                record "native shared image execute policy" FAIL "nested/orphan public clone policy did not pass"
            fi
            if ! grep -aq 'native scheduler growth and mixed PROCESS/THREAD round-robin: tested' "$suite_log"; then
                record "native scheduler growth/fairness" FAIL "mixed twelve-peer runtime assertions did not execute"
            fi
        fi
        ;;
esac

# Native initial-entry marker: independent of the older machine-case matrix.
case $machine in
virt-arm|virt-m68k-aspace|virt-m68k-mmu|raspi2b-aspace)
    if grep -q '^#define CONF_WITH_NATIVE_SCHED 1' "$top/obj/autoconf.h" &&
       grep -q '^#define CONF_WITH_PCLONE 1' "$top/obj/autoconf.h"; then
        if ! grep -aq 'native clone first-instruction state: PASS' "$suite_log"; then
            record "native clone initial register state" FAIL "first-instruction probe did not pass"
        fi
    fi
    ;;
esac
# End native initial-entry marker.


if [ "$machine" = pc-x86_64 ] &&
   grep -q '^#define CONF_WITH_NATIVE_SCHED 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_PCLONE 1' "$top/obj/autoconf.h" &&
   grep -Eq '^#define CONF_WITH_(EXIT_HANDOFF_TEST|X86_64_MEMTEST) 1' "$top/obj/autoconf.h"; then
    if ! grep -aq 'native x32 interrupted-user GPR/FXSAVE/iret capture: PASS' "$suite_log"; then
        record "native user IRQ capture" FAIL "real ring-3 register pattern was not captured"
    fi
fi

if [ "$machine" = virt-arm ] &&
   grep -q '^#define CONF_WITH_NATIVE_SCHED 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_PCLONE 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_EXIT_HANDOFF_TEST 1' "$top/obj/autoconf.h"; then
    if ! grep -aq 'native ARM interrupted-user integer/VFP/banked capture: PASS' "$suite_log"; then
        record "native ARM user IRQ capture" FAIL "real user integer/VFP/banked pattern was not captured"
    fi
fi

if [ "$machine" = virt-m68k-aspace ] &&
   grep -q '^#define CONF_WITH_NATIVE_SCHED 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_PCLONE 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_EXIT_HANDOFF_TEST 1' "$top/obj/autoconf.h"; then
    if ! grep -aq 'native m68k interrupted-user GPR/USP/FPU format-0 capture: PASS' "$suite_log"; then
        record "native m68k user IRQ capture" FAIL "real user GPR/USP/FPU pattern was not captured"
    fi
fi

if grep -q '^#define CONF_WITH_FORK_RETURN_TEST 1' "$top/obj/autoconf.h" &&
   ! grep -aq 'native shared-VM syscall-return probe (not Pfork): PASS' "$suite_log"; then
    record "native saved syscall return" FAIL "configured shared-VM return probe did not pass"
fi

if grep -q '^#define CONF_WITH_PTHJOIN 1' "$top/obj/autoconf.h" &&
   ! grep -aq 'native Pthjoin: PASS' "$suite_log"; then
    record "native TID joining" FAIL "configured Pthjoin worker did not pass"
fi

if grep -q '^#define CONF_WITH_PTERMGRP 1' "$top/obj/autoconf.h" &&
   ! grep -aq 'native Ptermgrp: PASS' "$suite_log"; then
    record "native PROCESS group exit" FAIL "configured Ptermgrp worker did not pass"
fi

if grep -q '^#define CONF_WITH_PTERMGRP 1' "$top/obj/autoconf.h" &&
   ! grep -aq 'native libc exit: PASS' "$suite_log"; then
    record "native libc group exit" FAIL "libc exit worker did not pass"
fi
if grep -q '^#define CONF_WITH_PTERMGRP 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_CUIS 1' "$top/obj/autoconf.h" &&
   ! grep -aq 'native CUIS delay cancellation: PASS' "$suite_log"; then
    record "native CUIS delay cancellation" FAIL "CUIS adapter worker did not pass"
fi

if grep -q '^#define CONF_WITH_PTERMGRP 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_CUIS 1' "$top/obj/autoconf.h" &&
   ! grep -aq 'native CUIS retained event cancellation: PASS' "$suite_log"; then
    record "native CUIS retained event cancellation" FAIL "retained event worker did not pass"
fi

if grep -q '^#define CONF_WITH_PTERMGRP 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_CUIS 1' "$top/obj/autoconf.h" &&
   ! grep -aq 'native CUIS window/session retirement: PASS' "$suite_log"; then
    record "native CUIS window/session retirement" FAIL "retirement worker did not pass"
fi

if grep -q '^#define CONF_WITH_PTERMGRP 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_CUIS 1' "$top/obj/autoconf.h" &&
   ! grep -aq 'native CUIS exclusive combined wait lifetime: PASS' "$suite_log"; then
    record "native CUIS exclusive combined wait lifetime" FAIL "combined wait lifetime worker did not pass"
fi

# Ownership evidence comes from the parent after the complete worker verdict,
# including its final mapped-page baseline, never an early worker print.
if [ "$machine" = pc-x86_64 ] &&
   grep -q '^#define CONF_WITH_PTERMGRP 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_PTHJOIN 1' "$top/obj/autoconf.h" &&
   grep -q '^#define CONF_WITH_CUIS 1' "$top/obj/autoconf.h" &&
   ! grep -aq 'native CUIS retained stream ownership: PASS (parent verified)' "$suite_log"; then
    record "native CUIS stream ownership" FAIL "parent did not verify the complete stream worker"
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
