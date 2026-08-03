#!/bin/sh
set -eu
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

# vdi_backend.c includes config.h, which needs obj/autoconf.h.  To keep
# this test independent of a target .config, synthesize a minimal one in
# a private include directory.  CONF_WITH_VDI_TRUECOLOR=1 keeps the
# truecolor op-table reference compiled in, exercising the full selector.
mkdir -p "$tmpdir/inc"
printf '#define CONF_WITH_VDI_TRUECOLOR 1\n' > "$tmpdir/inc/autoconf.h"

${CC:-cc} -std=gnu90 -Wall -Wextra -Werror \
    -I"$tmpdir/inc" -Iinclude -Ivdi \
    tools/test-vdi-backend.c vdi/vdi_backend.c bios/screen_mode.c \
    -o "$tmpdir/test-vdi-backend"
"$tmpdir/test-vdi-backend"
