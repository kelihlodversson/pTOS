#!/usr/bin/env python3
#
# analyse-trace.py - summarise an AES/VDI call trace made with trace.patch
#
# Copyright (C) 2026 The pTOS development team.
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.
#
# Usage: analyse-trace.py LOG                  phases of the run
#        analyse-trace.py LOG FROM TO          calls in a time window
#
# LOG is what Hatari printed (--natfeats on) while a program ran under a
# kernel patched with trace.patch.  Times are in seconds of emulated time.
# The phases view lists, per second, the calls that dominate it; whenever
# the set changes a new line is printed, which is enough to find the start
# and end of each test of a benchmark.  The window view lists the call
# templates in order of frequency.
#
import re
import sys
import collections

# calls the AES makes on its own all the time
IDLE = ('31.0 h1', '33.0 h1', '128.0 h1', '78 ', '25 ')


def load(path):
    rows = []
    pat = re.compile(r'(\d+) x(\d+) ([VA]) (.*)')
    with open(path, errors='replace') as f:
        for line in f:
            m = pat.match(line)
            if m:
                rows.append((int(m.group(1)) / 200.0, int(m.group(2)),
                             m.group(3), m.group(4)))
    return rows


def busy(rows):
    return [r for r in rows if not r[3].startswith(IDLE)]


def phases(rows):
    per_second = collections.defaultdict(collections.Counter)
    for t, n, kind, text in busy(rows):
        op = text.split()
        per_second[int(t)][kind + op[0] + (' ' + op[1] if kind == 'V' else '')] += n
    last = None
    for second in sorted(per_second):
        total = sum(per_second[second].values())
        top = tuple(sorted(o for o, c in per_second[second].items()
                           if c >= max(5, 0.08 * total)))
        if top != last:
            print('%6d s  %s' % (second, ', '.join(top)))
            last = top


def window(rows, start, end):
    count = collections.Counter()
    for t, n, kind, text in busy(rows):
        if start <= t < end:
            count[kind + ' ' + text] += n
    for text, n in count.most_common(40):
        print('%6d  %s' % (n, text))


def main():
    if len(sys.argv) not in (2, 4):
        sys.exit(__doc__)
    rows = load(sys.argv[1])
    if len(sys.argv) == 2:
        phases(rows)
    else:
        window(rows, float(sys.argv[2]), float(sys.argv[3]))


if __name__ == '__main__':
    main()
