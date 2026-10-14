#!/usr/bin/env python3
#
# compare.py - compare pBench results with a baseline, like GEM Bench does
#
# Copyright (C) 2026 The pTOS development team.
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.
#
# Usage: compare.py BASELINE.txt RESULT.txt ...
#
# Prints, for each result file, how fast it is relative to the baseline for
# every test, in percent (100% = as fast, more is faster), and the geometric
# mean of those, which is the "Display" score of GEM Bench.
#
import math
import re
import sys

LINE = re.compile(r'^(\D.*?)\s+(\d+)\.(\d{3}) s\s')
SHORT = ['Dialog', 'Text', 'Effects', 'Small', 'Graphics', 'Window', 'Blit',
         'Scroll', 'Justify', 'Enquire']


def read(path):
    times = {}
    for line in open(path):
        m = LINE.match(line)
        if m:
            times[m.group(1)] = int(m.group(2)) * 1000 + int(m.group(3))
    return times


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    base = read(sys.argv[1])
    names = list(base)
    print('%-34s' % '', ' '.join('%7s' % n for n in SHORT), '  Display')
    for path in sys.argv[2:]:
        res = read(path)
        pct = [base[n] * 100.0 / res[n] for n in names]
        mean = math.exp(sum(math.log(p) for p in pct) / len(pct))
        label = path.rsplit('/', 1)[-1][:-4]
        print('%-34s' % label, ' '.join('%6.0f%%' % p for p in pct),
              '  %5.0f%%' % mean)


if __name__ == '__main__':
    main()
