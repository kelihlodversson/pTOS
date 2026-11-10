#!/bin/sh
# Compile the production freestanding parser/argv code with a host harness.
set -eu
top=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
flags=
if [ "${BINFMT_SANITIZE:-0}" = 1 ]; then
    flags="-fsanitize=address,undefined -fno-omit-frame-pointer"
fi
${CC_HOST:-cc} -std=gnu90 -O1 -g -Wall -Wextra -Werror -Wdeclaration-after-statement \
    -UNDEBUG -DBINFMT_HOST_TEST $flags -iquote "$top/include" -iquote "$top/bdos" \
    "$top/bdos/binfmt_rules.c" "$top/bdos/binfmt_args.c" \
    "$top/tests/binfmt_host.c" -o "$build/binfmt-test"
"$build/binfmt-test"
