#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tmpdir=${TMPDIR:-/tmp}/ptos-bug-translate-test.$$
CC=${CC:-cc}

cleanup()
{
    rm -rf "$tmpdir"
}

trap cleanup EXIT HUP INT TERM

mkdir -p "$tmpdir"

"$CC" -std=gnu90 -pedantic -g -O1 -fsanitize=address \
    -fno-omit-frame-pointer "$repo_root/tools/bug.c" -o "$tmpdir/bug"

mkdir -p "$tmpdir/bios" "$tmpdir/po"
cp "$repo_root/bios/bios.c" "$tmpdir/bios/bios.c"
printf 'bios/bios.c\n' >"$tmpdir/po/POTFILES.in"

cd "$tmpdir"
"$tmpdir/bug" xgettext
"$tmpdir/bug" translate all bios/bios.c

echo 'bug translate safety test passed'
