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
cp obj/x32/x32probe.elf "$work/X32CB.TOS"
cp obj/x32/x32probe.elf "$work/X32VDI.TOS"
cp obj/x32/x32probe.elf "$work/X32AES.TOS"
if grep -q '^CONF_EXTERNAL_CLI *= *y' obj/auto.conf; then
    cp cli/emucon.tos "$work/EMUCON.TOS"
fi
cp x32reloc.elf "$work/X32RELOC.TOS"
cp x32noreloc.elf "$work/X32NOREL.TOS"
cp x32big.elf "$work/X32BIG.TOS"
# Include the AES/VDI import fixtures when built, so the same boot exercises
# their admission and teardown alongside the standard shared-library path.
if [ -f obj/x32/VDIIMP.TOS ] && [ -f obj/x32/AESIMP.TOS ]; then
    cp obj/x32/VDI*.TOS obj/x32/AES*.TOS "$work/"
fi
head -c 2000 x32hello.elf > "$work/X32TRUNC.TOS"
if grep -q '^CONF_WITH_ELF_SHARED=y' .config; then
    mkdir -p "$work/GEMSYS/LIB/X32"
    cp obj/ldso/libptos.so obj/ldso/test/libuse.so "$work/GEMSYS/LIB/X32/"
    cp obj/ldso/test/client.elf "$work/CLIENT.TOS"
    cp obj/ldso/test/clientpie.elf "$work/CLIENTP.TOS"
    cp obj/ldso/ld.so "$work/GEMSYS/LIB/X32/LD.SO"
    if grep -q '^CONF_WITH_LIBAES *= *y' obj/auto.conf; then
        cp obj/libaes/libaes.so "$work/GEMSYS/LIB/X32/LIBAES.SO"
        cp obj/libaes/CUIS.TOS "$work/CUIS.TOS"
    fi
    cp obj/ldso/test/librt.so obj/ldso/test/libdyn.so "$work/GEMSYS/LIB/X32/"
    cp obj/ldso/test/BD.SO "$work/GEMSYS/LIB/X32/"
    cp obj/ldso/test/SO*.TOS "$work/"
    cp obj/ldso/test/soapp.elf "$work/SOAPP.TOS"
    cp obj/ldso/test/probe.so "$work/GEMSYS/LIB/X32/PROBE.SO"
    cp obj/ldso/test/STATICPH.TOS "$work/"
    cp obj/ldso/test/dyntest.elf "$work/DYNTEST.TOS"
    cp obj/ldso/test/dynpie.elf "$work/DYNPIE.TOS"
    cp obj/ldso/test/BADPR.SO obj/ldso/test/NESTD.SO "$work/GEMSYS/LIB/X32/"
    cp obj/ldso/test/DYN*.TOS "$work/"
fi
# If available from an enabled build, also prove disabled kernels reject it.
if [ -f obj/ldso/test/dyntest.elf ]; then
    cp obj/ldso/test/dyntest.elf "$work/DYNTEST.TOS"
fi
cp "$firmware/OVMF_VARS_4M.fd" "$work/vars.fd"

# A raw image also works in restricted containers where QEMU fat:rw cannot
# create its temporary files in /var/tmp. Always use private disk/NVRAM copies.
set -- "$work/EFI" "$work/X32CB.TOS" "$work/X32VDI.TOS" "$work/X32AES.TOS"
if [ -f "$work/EMUCON.TOS" ]; then
    set -- "$@" "$work/EMUCON.TOS"
fi
if [ -f "$work/VDIIMP.TOS" ]; then
    set -- "$@" "$work"/VDI*.TOS "$work"/AES*.TOS
fi
if [ -d "$work/GEMSYS" ]; then
    set -- "$@" "$work/GEMSYS" "$work"/SO*.TOS "$work"/CLIENT*.TOS "$work/STATICPH.TOS"
fi
if [ -f "$work/CUIS.TOS" ]; then
    set -- "$@" "$work/CUIS.TOS"
fi
if [ -f "$work/DYNTEST.TOS" ]; then
    set -- "$@" "$work"/DYN*.TOS
fi
tools/mkhdisk.sh "$work/disk.img" 33554432 "$@" \
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
if grep -qE 'panic:|memtest: FAIL|mmap syscalls: FAIL' "$log"; then
    echo 'Boot log contains a panic or failed test' >&2
    exit 1
fi
grep -q 'x86-64 unlinked file backing: PASS' "$log"
grep -q "x86-64 mmap syscalls: $expected" "$log"
if [ "$expected" = PASS ]; then grep -q 'x86-64 file mappings/COW: PASS' "$log"; fi
if grep -q '^CONF_WITH_ELF_SHARED=y' .config; then
    grep -q 'x86-64 ldso relocations: PASS' "$log"
    [ "$(grep -c 'x32 dynamic CRT lifecycle: PASS' "$log")" -eq 8 ]
    grep -q 'x86-64 LD.SO runtime: PASS' "$log"
    grep -q 'x86-64 PT_INTERP startup: PASS' "$log"
fi
if [ -f "$work/VDIIMP.TOS" ]; then
    grep -q 'x86-64 VDI imports: PASS' "$log"
    grep -q 'x86-64 AES imports: PASS' "$log"
fi
if grep -q '^CONF_WITH_LIBAES *= *y' obj/auto.conf; then
    if grep -q '^CONF_WITH_X32_VDI *= *y' obj/auto.conf; then
        grep -q 'x32 CUIS VDI enforcement: PASS' "$log"
    fi
    grep -q 'x32 CUIS clipping: PASS' "$log"
    grep -q 'x32 CUIS visibility: PASS' "$log"
    grep -q 'x32 libaes objects: PASS' "$log"
    grep -q 'x32 libaes/CUIS sessions: PASS' "$log"
    grep -q 'x86-64 CUIS sessions: PASS' "$log"
fi
grep -q 'x86-64 memtest: PASS' "$log"
grep -q 'Welcome to EmuCON2' "$log"
if sed -n '/x86-64 memtest: PASS/,$p' "$log" | grep -q 'process fault:'; then
    echo 'Process fault after memory tests completed' >&2
    exit 1
fi
echo "Private Mmap/Munmap/Mprot boot tests passed ($expected)."
