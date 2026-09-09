#!/usr/bin/env python3
#
# fix-m68k-local-labels.py - Make GCC's local m68k symbol names acceptable
#                            to mintelf gas
#
# Copyright (C) 2026 The pTOS development team.
#
# This file is distributed under the GPL, version 2 or at your option any
# later version.  See doc/license.txt for details.
#
"""Make GCC's local m68k symbol names acceptable to mintelf gas."""

import re
import sys


GLOBAL_LABEL = re.compile(r"^\s*\.globl\s+([A-Za-z_.][A-Za-z0-9_.]*)", re.M)
LOCAL_LABEL = re.compile(r"^\s*\.local\s+([A-Za-z_.][A-Za-z0-9_.]*)", re.M)
TYPED_LABEL = re.compile(r"^\s*\.type\s+([A-Za-z_.][A-Za-z0-9_.]*),", re.M)


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: fix-m68k-local-labels.py INPUT OUTPUT")

    with open(sys.argv[1], "r", encoding="utf-8") as source:
        text = source.read()

    global_labels = set(GLOBAL_LABEL.findall(text))
    local_labels = set(LOCAL_LABEL.findall(text))
    local_labels.update(label for label in TYPED_LABEL.findall(text)
                        if label not in global_labels)

    # mintelf gas accepts GCC's private names only with a leading underscore.
    for label in sorted(local_labels, key=len, reverse=True):
        # A dot normally continues a symbol name, except for m68k operand
        # size suffixes (for example, "symbol.w").
        symbol = re.compile(r"(?<![A-Za-z0-9_.])" + re.escape(label)
                            + r"(?![A-Za-z0-9_]|\.(?![bwl](?:\b)))")
        text = symbol.sub("_" + label, text)

    with open(sys.argv[2], "w", encoding="utf-8") as destination:
        destination.write(text)


if __name__ == "__main__":
    main()
