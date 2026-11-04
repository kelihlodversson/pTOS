#!/bin/sh
# Check the public x32 layout and kernel-side unsigned pointer decoding.
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d "${TMPDIR:-/tmp}/ptos-clone-abi.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
${HOSTCC:-cc} -std=c99 -Wall -Wextra -Werror -iquote include -Ibdos \
    tests/clone/decode.c bdos/clone_args.c -o "$work/decode"
"$work/decode"
${HOSTCC:-cc} -std=c99 -Wall -Wextra -Werror -iquote include -Ibdos \
    tests/clone/unavailable.c bdos/clone.c -o "$work/unavailable"
"$work/unavailable"
${X32CC:-cc} -mx32 -ffreestanding -std=c99 -Wall -Wextra -Werror \
    -Iinclude -Ibdos -c tests/clone/abi_layout.c -o "$work/layout.o"
echo 'Pclone ABI layout, pointer decoding and unavailable backend: PASS'
