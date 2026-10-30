#!/bin/sh
#
# build-hatari.sh - build the pinned Hatari (tools/hatari-pin.sh) and install it
#
# Usage: tools/build-hatari.sh [prefix [source-directory]]
#
#   prefix            where it is installed (default $HATARI_PREFIX, then
#                     ~/.local/hatari): <prefix>/bin/hatari
#   source-directory  where it is fetched and built (default $HATARI_SRC, then
#                     ~/.local/src/hatari); a checkout of the pinned commit
#                     there is reused
#
# The packages it needs are in HATARI_APT_PACKAGES of the pin file.  CI builds
# it once and caches the prefix (.github/actions/setup-hatari).
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/hatari-pin.sh"

prefix=${1:-${HATARI_PREFIX:-$HOME/.local/hatari}}
src=${2:-${HATARI_SRC:-$HOME/.local/src/hatari}}

if [ ! -d "$src/.git" ]; then
    mkdir -p "$src"
    git -C "$src" init --quiet
    git -C "$src" remote add origin "$HATARI_URL"
    git -C "$src" fetch --quiet --depth 1 origin "$HATARI_COMMIT"
    git -C "$src" checkout --quiet FETCH_HEAD
fi

head=$(git -C "$src" rev-parse HEAD)
if [ "$head" != "$HATARI_COMMIT" ]; then
    echo "build-hatari: $src is at $head, the pin is $HATARI_COMMIT" >&2
    exit 1
fi

# shellcheck disable=SC2086  # the options are a list
cmake -S "$src" -B "$src/build" $HATARI_CMAKE_OPTIONS \
    -DCMAKE_INSTALL_PREFIX="$prefix" >/dev/null
cmake --build "$src/build" -j "$(nproc)"
cmake --install "$src/build" >/dev/null

echo "# Hatari $HATARI_COMMIT is installed in $prefix"
