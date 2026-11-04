#!/bin/sh
# Compile-only ABI checks using the same compilers as the architecture builds.
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d "${TMPDIR:-/tmp}/ptos-clone-layout.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
case ${1:-} in
    x32) set -- "${X32CC:-cc}" -mx32 -DEXPECTED_ALIGNMENT=4 ;;
    arm) set -- "${ARMCC:-arm-none-eabi-gcc}" -marm -DEXPECTED_ALIGNMENT=4 ;;
    m68k) set -- "${M68KCC:-m68k-atari-mintelf-gcc}" -m68000 -DEXPECTED_ALIGNMENT=2 ;;
    *) echo 'Usage: check-clone-layout.sh x32|arm|m68k' >&2; exit 1 ;;
esac
"$@" -ffreestanding -std=c99 -Wall -Wextra -Werror -Iinclude -Ibdos \
    -c tests/clone/abi_layout.c -o "$work/layout.o"
echo 'Clone user ABI size, offsets, pointer widths and alignment: PASS (compile only)'
