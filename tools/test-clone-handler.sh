#!/bin/sh
# Hosted checks for the x32 Pclone handler and MMU permissions.
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d "${TMPDIR:-/tmp}/ptos-clone-handler.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
# These fixtures link the x32 Pclone handler with a small set of models.
# Start with the CI preset so local GUI and native diagnostics cannot select
# kernel services that the hosted executable does not link.
mkdir "$work/base"
CONFIG_= python3 - "$work/base/config" <<'PY'
import kconfiglib, sys
k = kconfiglib.Kconfig(warn_to_stderr=False)
k.load_config('configs/pc-x86_64_defconfig')
k.load_config('tools/hosted-legacy-profile.config', replace=False)
k.syms['CONF_WITH_ELF_SHARED'].set_value(2)
k.syms['CONF_WITH_PCLONE'].set_value(2)
assert k.syms['CONF_WITH_ELF_SHARED'].str_value == 'y'
assert k.syms['CONF_WITH_PCLONE'].str_value == 'y'
assert k.syms['CONF_WITH_NATIVE_SCHED'].str_value == 'n'
assert k.syms['CONF_WITH_PROCESS_FILES_TEST'].str_value == 'n'
k.write_config(sys.argv[1])
PY
CONFIG_= python3 tools/genconfig.py --kconfig Kconfig --config "$work/base/config" \
    --header "$work/base/autoconf.h" --makefile "$work/base/auto.conf"
${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror \
    -iquote include -I"$work/base" -Iobj -Iinclude/arch/x86_64 -Iinclude/machine/pc-x86_64 \
    -iquote bdos -iquote fs -iquote bios -iquote util \
    tests/clone/handler.c bdos/clone.c bdos/clone_args.c -o "$work/handler"
"$work/handler"
echo 'Production xclone handler negative-argument tests: PASS'

# Actual KPROC logical entry policy with either demand-paging mode. Only the
# paging option changes in this host test header; no shipped preset changes.
for demand in 0 1; do
    sed "s/#define CONF_WITH_DEMAND_PAGING [01]/#define CONF_WITH_DEMAND_PAGING $demand/" \
        "$work/base/autoconf.h" > "$work/autoconf.h"
    ${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -fno-builtin \
        -ffunction-sections -fdata-sections -fno-pie -no-pie \
        -iquote include -I"$work" -I"$work/base" -Iobj -Iinclude/arch/x86_64 -Iinclude/machine/pc-x86_64 \
        -idirafter include -iquote bdos -iquote fs -iquote bios -iquote util \
        tests/clone/exec_permissions.c -Wl,--gc-sections -o "$work/exec-permissions"
    "$work/exec-permissions"
    ${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -fno-builtin \
        -ffunction-sections -fdata-sections -fno-pie -no-pie \
        -iquote include -I"$work" -I"$work/base" -Iobj -Iinclude/arch/x86_64 -Iinclude/machine/pc-x86_64 \
        -idirafter include -iquote bdos -iquote fs -iquote bios -iquote util \
        tests/clone/reservation_permissions.c -Wl,--gc-sections -o "$work/reservation-permissions"
    "$work/reservation-permissions"
done
echo 'Production KPROC staged-image/heap entry permissions (host page-table model): PASS'
echo 'Production KPROC reservation exact bounds and broad/narrow rights order (host MMU model): PASS'

${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -fno-builtin \
    -ffunction-sections -fdata-sections -fno-pie -no-pie \
    -iquote include -I"$work/base" -Iobj -Iinclude/arch/x86_64 -Iinclude/machine/pc-x86_64 \
    -idirafter include -iquote bdos -iquote fs -iquote bios -iquote util \
    tests/clone/procmem_text.c -Wl,--gc-sections -o "$work/procmem-text"
"$work/procmem-text"
echo 'Production PRG borrowed-text preflight/rights publication (host MMU model): PASS'
