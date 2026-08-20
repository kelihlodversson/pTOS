#!/bin/sh
#
# mkhdisk.sh - create a raw HD image with MBR and FAT16 partition
#
# Produces a raw disk image of exactly <size> bytes with an MBR partition
# table containing a single active FAT16 (LBA) partition starting at
# sector 2048 (1 MiB offset).  The total image size must be a power of
# two (required by QEMU for SD/eMMC images).
#
# Uses the same toolchain as tools/mkraspi-image.sh: mkfs.fat and mtools
# (mcopy), but replaces sfdisk with printf+dd for the MBR partition entry
# so the script works on macOS where sfdisk is unavailable.
#
# Usage: mkhdisk.sh <output.img> <size_in_bytes> <file-or-dir> ...

set -eu

if [ $# -lt 3 ]; then
    echo "usage: $0 <output.img> <size_in_bytes> <file-or-dir> ..." >&2
    exit 1
fi

output=$1
size=$2
shift 2

# Sanity-check: size must be a power of two, 4 MiB..32 MiB. The 1 MiB
# partition offset (see partition_start below) leaves too little room for
# a FAT16 partition below 4 MiB -- mkfs.fat fails with "Not enough or too
# many clusters for filesystem" at 2 MiB, the previous floor here. Above
# 32 MiB, the fixed 512-byte cluster size below (mkfs.fat -s 1, needed to
# keep small images formattable) exceeds FAT16's cluster-count limit and
# the same mkfs.fat error reappears at the other end.
if [ "$((size & (size - 1)))" -ne 0 ] || [ "$size" -lt 4194304 ] || [ "$size" -gt 33554432 ]; then
    echo "$0: $size: not a valid power of two (4 MiB..32 MiB)" >&2
    exit 1
fi

sector_size=512
partition_start=2048
partition_sectors=$((size / sector_size - partition_start))
disk_sectors=$((partition_start + partition_sectors))

# ---- build the FAT16 filesystem image in a temp file ----

workdir=$(mktemp -d "${TMPDIR:-/tmp}/mkhdisk.XXXXXX")
trap 'rm -rf "$workdir"' EXIT

fs_image="$workdir/fs.img"
dd if=/dev/zero of="$fs_image" bs=$sector_size count=$partition_sectors
mkfs.fat -F 16 -s 1 -n PTOS "$fs_image" >/dev/null

for f in "$@"; do
    if [ -d "$f" ]; then
        mcopy -s -i "$fs_image" "$f" ::
    else
        mcopy -i "$fs_image" "$f" ::
    fi
done

# ---- write the disk image with MBR ----

dd if=/dev/zero of="$output" bs=$sector_size count=$disk_sectors

# Build the 512-byte MBR sector: zeros everywhere except the partition
# table entry at offset 446 and the boot signature at offset 510.
#
# Partition entry (16 bytes at offset 446):
#   [0]     boot indicator: 0x80 = active
#   [1-3]   starting CHS: 0x00, 0x01, 0x00 (head 0, sector 1, cyl 0)
#   [4]     partition type: 0x0E = FAT16 LBA
#   [5-7]   ending CHS: 0xFE, 0xFF, 0xFF
#   [8-11]  starting LBA: 2048 (0x800)
#   [12-15] number of sectors: $partition_sectors (32-bit LE)

ps_lo=$((partition_sectors & 0xFF))
ps_md=$((partition_sectors >> 8 & 0xFF))
ps_hi=$((partition_sectors >> 16 & 0xFF))
ps_xl=$((partition_sectors >> 24 & 0xFF))

# Write the MBR partition entry (16 bytes at offset 446) and boot
# signature (2 bytes at offset 510) with separate dd calls to avoid
# NUL-byte truncation that occurs when printf stores output in a
# shell variable.
#
# Partition entry layout:
#   [0]     boot indicator: 0x80 = active
#   [1-3]   starting CHS: 0x00, 0x01, 0x00 (head 0, sector 1, cyl 0)
#   [4]     partition type: 0x0E = FAT16 LBA
#   [5-7]   ending CHS: 0xFE, 0xFF, 0xFF
#   [8-11]  starting LBA: 2048 (0x800, LE)
#   [12-15] number of sectors: $partition_sectors (32-bit LE)

printf '\200\000\001\000'    | dd of="$output" bs=1 seek=446 conv=notrunc
printf '\016\376\377\377'    | dd of="$output" bs=1 seek=450 conv=notrunc
printf '\000\010\000\000'    | dd of="$output" bs=1 seek=454 conv=notrunc
printf '%b' "$(printf '\\%03o\\%03o\\%03o\\%03o' "$ps_lo" "$ps_md" "$ps_hi" "$ps_xl")" \
                             | dd of="$output" bs=1 seek=458 conv=notrunc
printf '\125\252'            | dd of="$output" bs=1 seek=510 conv=notrunc

# Embed the already-formatted partition at its offset in the disk image.
dd if="$fs_image" of="$output" bs=$sector_size seek=$partition_start conv=notrunc

echo "Wrote $output ($((disk_sectors * sector_size / 1024 / 1024)) MiB)"
