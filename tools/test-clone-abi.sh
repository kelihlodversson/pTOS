#!/bin/sh
# Check the public x32 layout and kernel-side unsigned pointer decoding.
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d "${TMPDIR:-/tmp}/ptos-clone-abi.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
${HOSTCC:-cc} -std=c99 -Wall -Wextra -Werror -iquote include -Ibdos \
    tests/clone/decode.c bdos/clone_args.c -o "$work/decode"
"$work/decode"
sh tools/check-clone-layout.sh x32
echo 'Pclone ABI layout, snapshot and argument validation: PASS'
