#!/bin/sh
#
# fetch-fonts.sh - the default scalable fonts of the pTOS release images
#
# Downloads DejaVu Sans, Serif and Sans Mono, each in regular and bold,
# from the DejaVu fonts release below, checks them against the checksums
# pinned here, and writes them to <destination-dir> under 8.3 names, the
# only ones FAT16 volumes have.  The destination is the directory of the
# scalable fonts the built-in GDOS scans, \GEMSYS\FONTS on the boot drive
# (CONF_VDI_FONT_DIR, vdi/Kconfig), so for an image it is the GEMSYS/FONTS
# directory of the files that go into it.  Their licence (the Bitstream
# Vera licence, which allows redistribution of the unmodified fonts) is
# written next to them as LICENSE.TXT, as it requires; the same text is in
# fonts/dejavu/LICENSE and is quoted in doc/license.txt.
#
# Usage: fetch-fonts.sh <destination-dir>
#
# The release is pinned and the files verified by checksum, so what ends up
# on disk is always that exact, unmodified copy.  Bump DEJAVU_VERSION and
# the checksums together, from the new release's tarball, to move to a new
# version (and check that fonts/dejavu/LICENSE is still its LICENSE).

set -eu

for tool in curl sha256sum tar; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "$0: $tool is required but not installed" >&2
        exit 1
    fi
done

DEJAVU_VERSION=2.37
DEJAVU_TARBALL_SHA256=fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7
DEJAVU_TAG=version_$(echo "$DEJAVU_VERSION" | tr . _)
DEJAVU_URL=https://github.com/dejavu-fonts/dejavu-fonts/releases/download/$DEJAVU_TAG/dejavu-fonts-ttf-$DEJAVU_VERSION.tar.bz2

# file in the tarball : its name here : sha256
fonts='
DejaVuSans.ttf:DJVUSANS.TTF:7da195a74c55bef988d0d48f9508bd5d849425c1770dba5d7bfc6ce9ed848954
DejaVuSans-Bold.ttf:DJVUSANB.TTF:e6476c1b80502924294eed40894c5b18e06c181444ca953e5334262df9c27724
DejaVuSerif.ttf:DJVUSERF.TTF:42d1edeb7952f31b1f96d767ed7030b08a39e0c372b0071641518864e2bffb51
DejaVuSerif-Bold.ttf:DJVUSERB.TTF:c47b5527bcdc8dcf9ea8c77054454c5a884beaca2f44851a2a823ee639cbf07f
DejaVuSansMono.ttf:DJVUMONO.TTF:b4a6c3e4faab8773f4ff761d56451646409f29abedd68f05d38c2df667d3c582
DejaVuSansMono-Bold.ttf:DJVUMONB.TTF:bce60f1b4421acd9ea51ba6623d7024ecbe6817a953e3654df62a5e6bdf8f769
'
LICENSE_SHA256=7a083b136e64d064794c3419751e5c7dd10d2f64c108fe5ba161eae5e5958a93

if [ $# -ne 1 ]; then
    echo "usage: $0 <destination-dir>" >&2
    exit 1
fi

dest=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

verify() {
    got=$(sha256sum "$1" | cut -d' ' -f1)
    if [ "$got" != "$2" ]; then
        echo "$0: $3: checksum mismatch (got $got, expected $2)" >&2
        exit 1
    fi
}

echo "Fetching DejaVu fonts $DEJAVU_VERSION"
curl -fsSL "$DEJAVU_URL" -o "$work/dejavu.tar.bz2"
verify "$work/dejavu.tar.bz2" "$DEJAVU_TARBALL_SHA256" "the DejaVu tarball"
tar -xjf "$work/dejavu.tar.bz2" -C "$work"
src=$work/dejavu-fonts-ttf-$DEJAVU_VERSION

mkdir -p "$dest"
for entry in $fonts; do
    from=${entry%%:*}
    rest=${entry#*:}
    to=${rest%%:*}
    sha256=${rest#*:}
    verify "$src/ttf/$from" "$sha256" "$from"
    cp "$src/ttf/$from" "$dest/$to"
    echo "  $to"
done

verify "$src/LICENSE" "$LICENSE_SHA256" "the DejaVu licence"
cp "$src/LICENSE" "$dest/LICENSE.TXT"
