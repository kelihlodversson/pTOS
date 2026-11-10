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

# Actual KPROC logical entry policy, including demand-paged PE_GO images.
${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -fno-builtin \
    -ffunction-sections -fdata-sections -fno-pie -no-pie \
    -iquote include -Iobj -Iinclude/arch/x86_64 -Iinclude/machine/pc-x86_64 \
    -idirafter include -iquote bdos -iquote fs -iquote bios -iquote util \
    tests/clone/exec_permissions.c -Wl,--gc-sections -o "$work/exec-permissions"
"$work/exec-permissions"
echo 'Production KPROC staged-image/heap entry permissions (host page-table model): PASS'
