#!/bin/sh
set -eu

if ! command -v rg >/dev/null 2>&1; then
    echo 'tools/test-emucon-ctrl-arrows.sh requires ripgrep (rg)'
    exit 1
fi

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

require_file()
{
    if ! rg -q "$1" "$2"; then
        echo "missing EmuCON Ctrl-arrow contract: $3"
        exit 1
    fi
}

require_multiline()
{
    if ! rg -U -q "$1" "$2"; then
        echo "missing EmuCON Ctrl-arrow contract: $3"
        exit 1
    fi
}

require_absent()
{
    if rg -q "$1" "$2"; then
        echo "unexpected EmuCON Ctrl-arrow contract: $3"
        exit 1
    fi
}

require_file '^#define CTRL_ARROW_LEFT[[:space:]]+0x7300([[:space:]]|/\*|//|$)' \
    "$repo_root/include/scancode.h" 'Ctrl-left scan code'
require_file '^#define CTRL_ARROW_RIGHT[[:space:]]+0x7400([[:space:]]|/\*|//|$)' \
    "$repo_root/include/scancode.h" 'Ctrl-right scan code'
require_multiline 'case CTRL_ARROW_LEFT:[\s\S]*word = 1;[\s\S]*case ARROW_LEFT:' \
    "$repo_root/cli/cmdedit.c" 'Ctrl-left word movement'
require_multiline 'case CTRL_ARROW_RIGHT:[\s\S]*word = 1;[\s\S]*case ARROW_RIGHT:' \
    "$repo_root/cli/cmdedit.c" 'Ctrl-right word movement'
require_absent 'case SHIFT_ARROW_LEFT:|case SHIFT_ARROW_RIGHT:' \
    "$repo_root/cli/cmdedit.c" 'Shift-arrow word movement'
require_file 'control-left/right arrow = previous/next word' \
    "$repo_root/cli/cmdint.c" 'Ctrl-arrow help text'

printf '%s\n' 'EmuCON Ctrl-arrow contract test passed'
