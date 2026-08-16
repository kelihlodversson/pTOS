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
in doc/bios.txt: '#' through '######' headings and '**bold**' spans become
ESC-p ... ESC-q (reverse video on/off), the only styling VT52 has.
Everything else passes through unchanged, deliberately: this covers what
the pTOS readmes actually use, not the whole of Markdown.

Every line is written with a trailing CR LF, markdown or not: VT52's LF
moves the cursor down without returning it to column 0 (ascii_cr() is a
separate escape from ascii_lf() in bios/vt52.c), so a bare LF staircases
each line under TYPE instead of starting a new one. unix2dos will not do
this conversion for us here, because it refuses a file containing escape
bytes as binary.

Any file after the first is appended verbatim (CRLF aside, no markdown
rendering) -- this is how readme_emutos.txt, the plain boilerplate every
pTOS readme ends with, gets attached.
"""

import re
import sys

ESC = '\x1b'
REVERSE_ON = ESC + 'p'
REVERSE_OFF = ESC + 'q'

HEADING_RE = re.compile(r'^#{1,6}\s+(.*)$')
BOLD_RE = re.compile(r'\*\*(.+?)\*\*')


def render_markdown_line(line):
    heading = HEADING_RE.match(line)
    if heading:
        return REVERSE_ON + heading.group(1) + REVERSE_OFF
    return BOLD_RE.sub(lambda m: REVERSE_ON + m.group(1) + REVERSE_OFF, line)


def write_lines(path, render):
    with open(path, encoding='utf-8') as f:
        for line in f:
            rendered = render(line.rstrip('\n')) if render else line.rstrip('\n')
            sys.stdout.write(rendered + '\r\n')


def main(argv):
    if len(argv) < 2:
        print(f'usage: {argv[0]} <input.md> [plain-file ...]', file=sys.stderr)
        return 1

    write_lines(argv[1], render_markdown_line)
    for path in argv[2:]:
        write_lines(path, None)

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
