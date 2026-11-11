#!/bin/sh
# Run after generating an enabled x32 configuration in obj/.
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d "${TMPDIR:-/tmp}/ptos-clone-handler.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror \
    -iquote include -Iobj -Iinclude/arch/x86_64 -Iinclude/machine/pc-x86_64 \
    -iquote bdos -iquote fs -iquote bios -iquote util \
    tests/clone/handler.c bdos/clone.c bdos/clone_args.c -o "$work/handler"
"$work/handler"
echo 'Production xclone handler negative-argument tests: PASS'

# Actual KPROC logical entry policy with either demand-paging mode. Only the
# paging option changes in this host test header; no shipped preset changes.
for demand in 0 1; do
    sed "s/#define CONF_WITH_DEMAND_PAGING [01]/#define CONF_WITH_DEMAND_PAGING $demand/" \
        obj/autoconf.h > "$work/autoconf.h"
    ${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -fno-builtin \
        -ffunction-sections -fdata-sections -fno-pie -no-pie \
        -iquote include -I"$work" -Iobj -Iinclude/arch/x86_64 -Iinclude/machine/pc-x86_64 \
        -idirafter include -iquote bdos -iquote fs -iquote bios -iquote util \
        tests/clone/exec_permissions.c -Wl,--gc-sections -o "$work/exec-permissions"
    "$work/exec-permissions"
done
echo 'Production KPROC staged-image/heap entry permissions (host page-table model): PASS'

${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -fno-builtin \
    -ffunction-sections -fdata-sections -fno-pie -no-pie \
    -iquote include -Iobj -Iinclude/arch/x86_64 -Iinclude/machine/pc-x86_64 \
    -idirafter include -iquote bdos -iquote fs -iquote bios -iquote util \
    tests/clone/procmem_text.c -Wl,--gc-sections -o "$work/procmem-text"
"$work/procmem-text"
echo 'Production PRG borrowed-text preflight/rights publication (host MMU model): PASS'
