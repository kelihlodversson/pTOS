#!/usr/bin/env python3
#
# make-test-font.py - the font of the scalable font regression tests (#579)
#
# Usage: make-test-font.py <DejaVuSansMono.ttf> <PTOSTEST.TTF>
#
# Subsets DejaVu Sans Mono (the pinned release of tools/fetch-fonts.sh) to
# the printable ASCII and Latin-1 characters and renames it "PTOS Test
# Mono", as the Bitstream Vera licence (tests/fonts/LICENSE) requires of a
# modified version.  The result is deterministic for a given fontTools, and
# is committed as tests/fonts/PTOSTEST.TTF so that the tests do not depend
# on the fonts a host has installed.
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.

import sys

from fontTools import subset
from fontTools.ttLib import TTFont

FAMILY = "PTOS Test Mono"
STYLE = "Regular"
POSTSCRIPT = "PTOSTestMono-Regular"


def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: %s <DejaVuSansMono.ttf> <PTOSTEST.TTF>\n" % sys.argv[0])
        sys.exit(2)
    src, dst = sys.argv[1:]

    opts = subset.Options()
    opts.layout_features = []
    opts.hinting = True
    opts.notdef_outline = True
    opts.name_IDs = [0, 1, 2, 3, 4, 5, 6, 13, 14]
    opts.name_languages = [0x409]
    opts.drop_tables += ["DSIG", "GPOS", "GSUB", "GDEF", "BASE", "JSTF", "MATH"]
    font = TTFont(src, recalcTimestamp=False)
    sub = subset.Subsetter(opts)
    sub.populate(unicodes=list(range(0x20, 0x7f)) + list(range(0xa0, 0x100)))
    sub.subset(font)

    name = font["name"]
    for rec in list(name.names):
        if rec.nameID in (1, 16):
            rec.string = FAMILY
        elif rec.nameID in (2, 17):
            rec.string = STYLE
        elif rec.nameID == 3:
            rec.string = "PTOS Test Mono " + str(font["head"].fontRevision)
        elif rec.nameID == 4:
            rec.string = FAMILY
        elif rec.nameID == 6:
            rec.string = POSTSCRIPT
    # a fixed date, so that the file is the same every time
    font["head"].created = font["head"].modified = 0
    font.save(dst)


if __name__ == "__main__":
    main()
