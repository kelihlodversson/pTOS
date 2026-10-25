#!/usr/bin/env python3
#
# make-test-font2.py - the second font of the scalable font rescan test (#648)
#
# Usage: make-test-font2.py <PTOSTEST.TTF> <PTOSTES2.TTF>
#
# The test font under the name "PTOS Test Mon2": the one character of the
# family name that differs, in the name table's 8-bit and 16-bit strings.  The
# test puts it into the font directory while the system runs, to see the font
# list grow.
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.

import sys


def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: %s <PTOSTEST.TTF> <PTOSTES2.TTF>\n" % sys.argv[0])
        sys.exit(2)
    src, dst = sys.argv[1:]
    old, new = b"PTOS Test Mono", b"PTOS Test Mon2"

    def wide(s):
        return b"".join(bytes([0, c]) for c in s)

    data = open(src, "rb").read()
    data = data.replace(old, new).replace(wide(old), wide(new))
    open(dst, "wb").write(data)


if __name__ == "__main__":
    main()
