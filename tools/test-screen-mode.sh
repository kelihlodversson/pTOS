#!/bin/sh
set -eu
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
${CC:-cc} -std=gnu90 -Wall -Wextra -Werror \
    -Iinclude tools/test-screen-mode.c bios/screen_mode.c \
    -o "$tmpdir/test-screen-mode"
"$tmpdir/test-screen-mode"
