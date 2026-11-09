#!/bin/sh
# Compile-only REVIEW layout: does not expose a transport selector/handler.
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d "${TMPDIR:-/tmp}/ptos-receipt-layout.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
case ${1:-} in
    x32) set -- "${X32CC:-cc}" -mx32 ;;
    arm) set -- "${ARMCC:-arm-none-eabi-gcc}" -marm ;;
    m68k) set -- "${M68KCC:-m68k-atari-mintelf-gcc}" -m68040 ;;
    *) echo 'Usage: check-publication-receipt-layout.sh x32|arm|m68k' >&2; exit 1 ;;
esac
"$@" -ffreestanding -std=gnu90 -Wall -Wextra -Werror -Iinclude -x c \
    -c -o "$work/layout.o" - <<'SOURCE'
#include "tests/cuis/publication_receipt_wire.h"
SOURCE
echo 'Receipt REVIEW record size, every offset, scalar/address width and alignment: PASS (compile only)'
