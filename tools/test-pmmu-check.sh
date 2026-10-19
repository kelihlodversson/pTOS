#!/bin/sh
#
# test-pmmu-check.sh - boot a CONF_REQUIRE_PMMU virt-m68k image on CPUs
# with and without a usable PMMU, and check that it refuses the ones
# without one (#564)
#
# Usage: tools/test-pmmu-check.sh [IMAGE]     (default: virt-m68k.elf)
#
# Build the image first: make virt-m68k-mmu_defconfig && make
#
# A CPU without a PMMU must print the startup check's message on the
# serial console, and nothing of the boot after it; the 68040 must boot as
# usual (the "pTOS Version" banner) without the message.  -cpu m68020 and
# m68040 are always tested.  m68000, m68010 and m68030 are tested as well
# where this QEMU can run them on the virt machine; QEMU emulates no 68030
# PMMU, so its m68030 counts as a CPU without one.  The 68060 is left out:
# an image built for -m68020 is not meant to run on QEMU's 68060
# (doc/install.txt), and on QEMU 6.2 it panics or goes quiet at some point
# after the check, so it cannot tell the check working from failing.

image=${1:-virt-m68k.elf}
qemu=${QEMU:-qemu-system-m68k}
if [ ! -r "$image" ]; then
    echo "usage: $0 [IMAGE]" >&2
    exit 2
fi

message='needs a 68030, 68040 or 68060 with an MMU'
banner='pTOS Version'
tmp=$(mktemp -d) || exit 2
trap 'rm -rf "$tmp"' EXIT
failed=0

# boot CPU WANT MANDATORY: WANT is "refuse" or "boot"; MANDATORY is "yes"
# or "no"
boot() {
    cpu=$1 want=$2 mandatory=$3
    serial=$tmp/$cpu.serial
    : > "$serial"
    "$qemu" -M virt -m 128 -cpu "$cpu" -kernel "$image" \
        -global virtio-mmio.force-legacy=false -display none \
        -monitor none -serial "file:$serial" > "$tmp/$cpu.log" 2>&1 &
    pid=$!
    # until either line shows up, QEMU exits, or 20 s have gone
    status=124
    i=0
    while [ $i -lt 40 ]; do
        if grep -q -e "$message" -e "$banner" "$serial"; then
            sleep 1             # let the rest of the message arrive
            break
        fi
        if ! kill -0 $pid 2>/dev/null; then
            wait $pid
            status=$?
            break
        fi
        sleep 0.5
        i=$((i + 1))
    done
    kill $pid 2>/dev/null
    wait $pid 2>/dev/null
    if [ $status -ne 124 ] && [ ! -s "$serial" ]; then
        # QEMU stopped by itself without any output: it cannot run this CPU
        # on the virt machine
        if [ "$mandatory" = yes ]; then
            echo "FAIL $cpu: QEMU exited ($status): $(head -c 200 "$tmp/$cpu.log")"
            failed=1
        else
            echo "skip $cpu: this QEMU cannot run it on the virt machine"
        fi
        return
    fi
    if grep -q "$message" "$serial"; then
        refused=yes
    else
        refused=no
    fi
    if grep -q "$banner" "$serial"; then
        booted=yes
    else
        booted=no
    fi
    case $want:$refused:$booted in
    refuse:yes:no) echo "PASS $cpu: refused with the message" ;;
    boot:no:yes)   echo "PASS $cpu: booted" ;;
    *)
        echo "FAIL $cpu: wanted $want, message=$refused, banner=$booted"
        sed 's/^/    /' "$serial" | head -20
        failed=1
        ;;
    esac
}

boot m68020 refuse yes
boot m68040 boot yes
boot m68000 refuse no
boot m68010 refuse no
boot m68030 refuse no

if [ $failed -ne 0 ]; then
    echo "The PMMU startup check failed." >&2
    exit 1
fi
echo "The PMMU startup check works."
