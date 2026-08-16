#!/usr/bin/env python3
#
# md2atari.py - render a small Markdown subset with Atari VT52 escapes
#
# Copyright (C) 2026 The pTOS development team.
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.
#
"""Convert a Markdown readme source to plain text with Atari escape codes.

The output is meant to be read by EmuCON's TYPE command, or anything else
that feeds it through the BIOS conout() VT52-style interpreter documented
in doc/bios.txt: '#'/'##' headings and '**bold**' spans become ESC-p ...
ESC-q (reverse video on/off), the only styling VT52 has. Everything else
passes through unchanged, deliberately: this covers what the pTOS readmes
actually use, not the whole of Markdown.
"""

import re
import sys

ESC = '\x1b'
REVERSE_ON = ESC + 'p'
REVERSE_OFF = ESC + 'q'

HEADING_RE = re.compile(r'^#{1,6}\s+(.*)$')
BOLD_RE = re.compile(r'\*\*(.+?)\*\*')


def render_line(line):
    heading = HEADING_RE.match(line)
    if heading:
        return REVERSE_ON + heading.group(1) + REVERSE_OFF
    return BOLD_RE.sub(lambda m: REVERSE_ON + m.group(1) + REVERSE_OFF, line)


def main(argv):
    if len(argv) != 2:
        print(f'usage: {argv[0]} <input.md>', file=sys.stderr)
        return 1

    with open(argv[1], encoding='utf-8') as f:
        for line in f:
            sys.stdout.write(render_line(line.rstrip('\n')) + '\n')

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
