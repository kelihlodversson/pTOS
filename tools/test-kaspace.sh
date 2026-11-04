#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d "${TMPDIR:-/tmp}/ptos-kaspace.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
CONFIG_= python3 tools/genconfig.py --kconfig Kconfig --config configs/pc-x86_64_defconfig \
    --header "$work/autoconf.h" --makefile "$work/auto.conf"
${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -iquote include -I"$work" \
    -Iinclude/arch/x86_64 -Iinclude/machine/pc-x86_64 -iquote bdos \
    tests/kaspace_lifetime.c bdos/kaspace.c -o "$work/test"
"$work/test"
echo 'KASPACE ownership and failed adoption: PASS'
