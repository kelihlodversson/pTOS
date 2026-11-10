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
