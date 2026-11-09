#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d "${TMPDIR:-/tmp}/ptos-private-aspace.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
CONFIG_= python3 - "$work/config" <<'PY'
import kconfiglib,sys
k=kconfiglib.Kconfig(warn_to_stderr=False)
k.load_config('configs/pc-x86_64_defconfig')
k.syms['CONF_WITH_MMAP'].set_value(0)
k.syms['CONF_WITH_PRIVATE_CLONE'].set_value(2)
assert k.syms['CONF_WITH_PRIVATE_CLONE'].str_value=='y'
k.write_config(sys.argv[1])
PY
CONFIG_= python3 tools/genconfig.py --kconfig Kconfig --config "$work/config" \
    --header "$work/autoconf.h" --makefile "$work/auto.conf"
${HOSTCC:-cc} -std=gnu90 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -iquote include -I"$work" -Iinclude/arch/x86_64 -idirafter include -iquote bdos \
    tests/aspace_cow.c bdos/kaspace.c -Wl,--gc-sections -o "$work/cow"
"$work/cow"
