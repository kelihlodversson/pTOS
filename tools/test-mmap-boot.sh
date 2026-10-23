#!/bin/sh
# Exercise anonymous mapping syscalls and failure cleanup through the x32 MMU.
set -eu

top=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$top"
work=$(mktemp -d "${TMPDIR:-/tmp}/ptos-mmap.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
log=${MMAP_BOOT_LOG:-$work/boot.log}
firmware=${OVMF_DIR:-/usr/share/OVMF}
case ${1:-} in
    '') expected=PASS ;;
    --disabled) expected=disabled ;;
    *) echo "Usage: $0 [--disabled]" >&2; exit 1 ;;
esac

mkdir -p "$work/EFI/BOOT"
cp pc-x86_64.efi "$work/EFI/BOOT/BOOTX64.EFI"
cp x32hello.elf "$work/X32HELLO.TOS"
cp x32reloc.elf "$work/X32RELOC.TOS"
cp x32noreloc.elf "$work/X32NOREL.TOS"
cp x32big.elf "$work/X32BIG.TOS"
head -c 2000 x32hello.elf > "$work/X32TRUNC.TOS"
cp "$firmware/OVMF_VARS_4M.fd" "$work/vars.fd"

# A raw image also works in restricted containers where QEMU fat:rw cannot
# create its temporary files in /var/tmp. Always use private disk/NVRAM copies.
tools/mkhdisk.sh "$work/disk.img" 33554432 "$work/EFI" \
    "$work/X32HELLO.TOS" "$work/X32RELOC.TOS" "$work/X32NOREL.TOS" \
    "$work/X32BIG.TOS" "$work/X32TRUNC.TOS" > "$work/disk.log" 2>&1 || {
        cat "$work/disk.log"
        exit 1
    }
set -- qemu-system-x86_64
if [ -n "${QEMU_DATA_DIR:-}" ]; then
    set -- "$@" -L "$QEMU_DATA_DIR"
fi
set +e
timeout "${MMAP_BOOT_TIMEOUT:-45}" "$@" -machine pc -m 256 \
    -cpu qemu64,+pdpe1gb -nic none \
    -drive if=pflash,format=raw,readonly=on,file="$firmware/OVMF_CODE_4M.fd" \
    -drive if=pflash,format=raw,file="$work/vars.fd" \
    -drive format=raw,file="$work/disk.img" \
    -serial file:"$log" -display none > "$log.qemu" 2>&1
rc=$?
set -e
cat "$log.qemu"
if [ -f "$log" ]; then cat "$log"; fi
[ "$rc" -eq 124 ] || { echo "QEMU exited unexpectedly: $rc"; exit 1; }
[ "$(grep -c 'EFI entry reached' "$log" || true)" -eq 1 ]
! grep -qE 'panic:|memtest: FAIL|mmap syscalls: FAIL' "$log"
grep -q "x86-64 mmap syscalls: $expected" "$log"
if grep -q '^CONF_WITH_ELF_SHARED=y' .config; then
    grep -q 'x86-64 ldso relocations: PASS' "$log"
fi
grep -q 'x86-64 memtest: PASS' "$log"
grep -q 'Welcome to EmuCON2' "$log"
! sed -n '/x86-64 memtest: PASS/,$p' "$log" | grep -q 'process fault:'
echo "Anonymous Mmap/Munmap/Mprot boot tests passed ($expected)."
