#!/bin/sh
#
# fetch-raspi-firmware.sh - vendor the Raspberry Pi GPU boot firmware
#
# Downloads the exact, unmodified binaries the Raspberry Pi's GPU needs to
# get from power-on to loading a kernel image (bootcode.bin, start.elf,
# start4.elf and their fixup.dat companions), plus the licence they are
# redistributed under.  boot/LICENCE.broadcom of raspberrypi/firmware (see
# below) permits exactly this: running or using a Raspberry Pi device, as
# long as the binaries are unmodified and the licence travels with them.
#
# Usage: fetch-raspi-firmware.sh <destination-dir>
#
# Everything is pinned to one commit of https://github.com/raspberrypi/firmware
# and verified by checksum, so what ends up on disk is always that exact,
# unmodified copy -- never a patched or re-derived one.  Bump FIRMWARE_COMMIT
# and the checksums below together, from a fresh clone of that commit, to
# pick up newer firmware.

set -eu

for tool in curl sha256sum; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "$0: $tool is required but not installed" >&2
        exit 1
    fi
done

FIRMWARE_COMMIT=3d301dd924bcd758a4c8cb19fe8531031f033f43

# file:sha256, one per line.  start.elf/fixup.dat boot the Pi 1-3 (and every
# other board through the Pi 3 that is not Pi 4), start4.elf/fixup4.dat boot
# the Pi 4; bootcode.bin is the second-stage loader that finds and starts
# the right one of those.
firmware_files='
bootcode.bin:4245ec23b58158dc3e55f3e065eb4ecc0c0705d76c8db506e9f824941feee32e
start.elf:6a79bc2f28a51be84e1be03d508c1c4aa205e287346ebf30df7a0961054993e8
fixup.dat:651632e140c932cba9ed64bb7c8871ec927202b000968009a055171f09ee96ff
start4.elf:6d540ddf77623976ba60de630e9300a5a099f7710e11ee85a7bf076f5931f4bc
fixup4.dat:5818b069814c7f051d4c9246307dced983bcbdcc6a90d9e5491fc5c68159d077
LICENCE.broadcom:c7283ff51f863d93a275c66e3b4cb08021a5dd4d8c1e7acc47d872fbe52d3d6b
'

if [ $# -ne 1 ]; then
    echo "usage: $0 <destination-dir>" >&2
    exit 1
fi

dest=$1
mkdir -p "$dest"

for entry in $firmware_files; do
    file=${entry%%:*}
    sha256=${entry#*:}
    url="https://raw.githubusercontent.com/raspberrypi/firmware/$FIRMWARE_COMMIT/boot/$file"

    echo "Fetching $file from raspberrypi/firmware@$FIRMWARE_COMMIT"
    curl -fsSL "$url" -o "$dest/$file"

    got=$(sha256sum "$dest/$file" | cut -d' ' -f1)
    if [ "$got" != "$sha256" ]; then
        echo "$file: checksum mismatch (expected $sha256, got $got)" >&2
        echo "the firmware at $FIRMWARE_COMMIT changed, or the download was corrupted" >&2
        exit 1
    fi
done
