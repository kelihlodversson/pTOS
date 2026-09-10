#!/bin/sh
#
# mkraspi-image.sh - pack a directory into a flashable Raspberry Pi SD image
#
# Produces a raw disk image with an MBR partition table and a single FAT16
# partition holding the given directory's contents (the GPU boot firmware
# plus the kernel*.img files for each Pi model).  Writing the result with dd,
# Raspberry Pi Imager or balenaEtcher gives a card that boots on real
# hardware with no manual partitioning or formatting.
#
# Built with mtools, dosfstools (mkfs.fat) and sfdisk, without ever
# mounting or loop-attaching anything: the FAT filesystem is assembled in
# a separate file at 1:1 size with the partition, then dd'd into place at
# the partition's offset in the disk image.
#
# Usage: mkraspi-image.sh <source-dir> <output.img>

set -eu

if [ $# -ne 2 ]; then
    echo "usage: $0 <source-dir> <output.img>" >&2
    exit 1
fi

srcdir=$1
output=$2

if [ ! -d "$srcdir" ]; then
    echo "$0: $srcdir: not a directory" >&2
    exit 1
fi

# An empty $srcdir leaves "$srcdir"/* unexpanded (the literal string, POSIX
# glob semantics), which would otherwise reach du below and fail there with
# a confusing "No such file or directory" instead of this.
set -- "$srcdir"/*
if [ ! -e "$1" ]; then
    echo "$0: $srcdir: directory is empty" >&2
    exit 1
fi

# 1 MiB alignment, as any modern partitioning tool would use.
sector_size=512
partition_start=2048

total_bytes=0
for f in "$srcdir"/*; do
    # du, not stat: a source entry may be a directory (e.g. doc/), whose
    # recursive content size stat -c%s would not give.
    size=$(du -sb "$f" | cut -f1)
    total_bytes=$((total_bytes + size))
done

# Round the content up to whole MiBs and pad generously for FAT overhead
# and cluster rounding, then enforce a floor well past that: real SD cards
# are gigabytes in size, so there is no reason to hand back a card with
# barely any room on it once flashed. 256 MiB leaves comfortable space to
# save files or drop data onto the card while staying well inside FAT16's
# own ~2 GiB limit (doc/fat16.txt) -- and since the extra space is unused,
# all-zero sectors, it costs virtually nothing in the zipped archive.
partition_mib=$(( (total_bytes + 1024 * 1024 - 1) / (1024 * 1024) + 8 ))
if [ "$partition_mib" -lt 256 ]; then
    partition_mib=256
fi

# doc/fat16.txt: DOS FAT16 tops out at 65525 clusters of at most 32768
# bytes each, ~2 GiB. Fail here with a clear reason instead of letting
# mkfs.fat -F 16 reject a too-large request below with a less specific
# "cluster count" error.
if [ "$partition_mib" -gt 2000 ]; then
    echo "$0: $srcdir is ${partition_mib} MiB, over FAT16's ~2 GiB limit (doc/fat16.txt)" >&2
    exit 1
fi

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

fs_image="$workdir/fs.img"
dd if=/dev/zero of="$fs_image" bs=1M count="$partition_mib" status=none
mkfs.fat -F 16 -n PTOS "$fs_image" >/dev/null

for f in "$srcdir"/*; do
    if [ "${f##*.}" = INF ]; then
        mcopy -t -i "$fs_image" "$f" ::
    else
        mcopy -s -i "$fs_image" "$f" ::
    fi
done
mdir -i "$fs_image" ::

partition_sectors=$((partition_mib * 1024 * 1024 / sector_size))
disk_sectors=$((partition_start + partition_sectors))

rm -f "$output"
dd if=/dev/zero of="$output" bs=$sector_size count="$disk_sectors" status=none

sfdisk "$output" <<EOF
label: dos
unit: sectors

start=$partition_start, size=$partition_sectors, type=e, bootable
EOF

# Embed the already-formatted partition at its offset in the disk image.
dd if="$fs_image" of="$output" bs=$sector_size seek="$partition_start" conv=notrunc status=none

echo "Wrote $output ($((disk_sectors * sector_size / 1024 / 1024)) MiB)"
