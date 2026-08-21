#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

rg -q '^#define CTRL_ARROW_LEFT[[:space:]]+0x7300$' "$repo_root/include/scancode.h"
rg -q '^#define CTRL_ARROW_RIGHT[[:space:]]+0x7400$' "$repo_root/include/scancode.h"
rg -U -q 'case CTRL_ARROW_LEFT:[\s\S]*word = 1;[\s\S]*case ARROW_LEFT:' "$repo_root/cli/cmdedit.c"
rg -U -q 'case CTRL_ARROW_RIGHT:[\s\S]*word = 1;[\s\S]*case ARROW_RIGHT:' "$repo_root/cli/cmdedit.c"
! rg -q 'case SHIFT_ARROW_LEFT:|case SHIFT_ARROW_RIGHT:' "$repo_root/cli/cmdedit.c"
rg -q 'control-left/right arrow = previous/next word' "$repo_root/cli/cmdint.c"

printf '%s\n' 'EmuCON Ctrl-arrow contract test passed'
