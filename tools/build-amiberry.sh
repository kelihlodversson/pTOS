#!/bin/sh
#
# build-amiberry.sh - build the pinned Amiberry (tools/amiberry-pin.sh) and
# install it
#
# Usage: tools/build-amiberry.sh [prefix [source-directory]]
#
#   prefix            where it is installed (default $AMIBERRY_PREFIX, then
#                     ~/.local/amiberry): <prefix>/bin/amiberry
#   source-directory  where it is cloned and built (default $AMIBERRY_SRC, then
#                     ~/.local/src/amiberry); a clone of the pinned commit
#                     there is reused
#
# The packages it needs are in AMIBERRY_APT_PACKAGES of the pin file; a user
# without sudo can unpack them (doc/debugging.txt).  CI builds it once and
# caches the prefix (.github/actions/setup-amiberry).
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/amiberry-pin.sh"

prefix=${1:-${AMIBERRY_PREFIX:-$HOME/.local/amiberry}}
src=${2:-${AMIBERRY_SRC:-$HOME/.local/src/amiberry}}

if [ ! -d "$src/.git" ]; then
    mkdir -p "$(dirname "$src")"
    git clone --quiet --branch "$AMIBERRY_TAG" --depth 1 \
        --recurse-submodules --shallow-submodules "$AMIBERRY_URL" "$src"
fi

head=$(git -C "$src" rev-parse HEAD)
if [ "$head" != "$AMIBERRY_COMMIT" ]; then
    echo "build-amiberry: $src is at $head, the pin is $AMIBERRY_COMMIT ($AMIBERRY_TAG)" >&2
    exit 1
fi

# shellcheck disable=SC2086  # the options are a list
cmake -S "$src" -B "$src/build" $AMIBERRY_CMAKE_OPTIONS \
    -DCMAKE_INSTALL_PREFIX="$prefix" >/dev/null
cmake --build "$src/build" -j "$(nproc)"
cmake --install "$src/build" >/dev/null

echo "# Amiberry $AMIBERRY_TAG ($AMIBERRY_COMMIT) is installed in $prefix"
