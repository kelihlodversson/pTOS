#!/usr/bin/env python3
#
# check-baseline.py - compare pBench results with pTOS's own baselines and
#                     say whether performance got significantly worse or better
#
# Copyright (C) 2026 The pTOS development team.
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.
#
# Usage: check-baseline.py [--report FILE] BASELINE_DIR RESULT_DIR KEY
#
# Every BASELINE_DIR/KEY-*.txt is compared with the file of the same name in
# RESULT_DIR (both as written by run-hatari.py / run-baselines.py).  For each
# test the change is baseline time / new time, so more than 100% is faster;
# the geometric mean over the tests is the Display score of GEM Bench.
#
# The change is significant when the mean is off by more than MEAN_LIMIT, or
# any single test by more than TEST_LIMIT.  These are far above the noise of
# Hatari (below 0.25% between runs of the same image), and small enough that a
# slowdown of one drawing primitive shows.  A Markdown report goes to --report
# (and is what the workflow posts on the pull request).
#
# Exit status: 1 if anything got significantly slower (or a result is
# missing), 2 if nothing did but something got significantly faster, else 0.
#
import argparse
import glob
import math
import os
import re
import sys

LINE = re.compile(r'^(\D.*?)\s+(\d+)\.(\d{3}) s\s+(.*?)\s*$')
MEAN_LIMIT = 3.0        # percent
TEST_LIMIT = 8.0        # percent


def read(path):
    # name -> time in ms, and name -> the work done ("65 windows"): a result
    # that did less work (a test stopping early) must not pass as faster
    times = {}
    work = {}
    for line in open(path):
        m = LINE.match(line)
        if m:
            times[m.group(1)] = int(m.group(2)) * 1000 + int(m.group(3))
            work[m.group(1)] = m.group(4)
    return times, work


def emulator(path):
    for line in open(path):
        m = re.match(r'# emulator: (Hatari [^,]*)', line)
        if m:
            return m.group(1)
    return None


def describe(name):
    # atari512-med-noblitter -> "ST-MEDIUM, no blitter"
    m = re.match(r'.*-(low|med|high)-(blitter|noblitter)$', name)
    if not m:
        return name
    res = {'low': 'ST-LOW', 'med': 'ST-MEDIUM', 'high': 'ST-HIGH'}[m.group(1)]
    return '%s, %s' % (res, 'blitter' if m.group(2) == 'blitter'
                       else 'no blitter')


def verdict(change):
    if change < -TEST_LIMIT:
        return 'slower'
    if change > TEST_LIMIT:
        return 'faster'
    return ''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--report')
    ap.add_argument('baselines')
    ap.add_argument('results')
    ap.add_argument('key')
    a = ap.parse_args()

    files = sorted(glob.glob(os.path.join(a.baselines, a.key + '-*.txt')))
    if not files:
        text = ('### pBench: no baselines\n\nThere are no `%s-*.txt` files '
                'in `%s`; see "Performance baselines for CI" in '
                '`doc/pbench.txt`.\n' % (a.key, a.baselines))
        print(text)
        if a.report:
            with open(a.report, 'w') as f:
                f.write(text)
        sys.exit(1)
    out = []
    worse = better = missing = 0
    for path in files:
        name = os.path.basename(path)[:-4]
        res_path = os.path.join(a.results, name + '.txt')
        out.append('#### %s' % describe(name))
        out.append('')
        if not os.path.exists(res_path):
            out.append('No result: pBench did not finish under Hatari.')
            out.append('')
            missing += 1
            continue
        (base, base_work), (res, res_work) = read(path), read(res_path)
        if emulator(path) != emulator(res_path):
            out.append('Note: the baseline was made with %s, this run used %s;'
                       ' a different Hatari can move the numbers.'
                       % (emulator(path), emulator(res_path)))
            out.append('')
        if set(base) != set(res):
            out.append('The tests differ from the baseline: %s.'
                       % ', '.join(sorted(set(base) ^ set(res))))
            out.append('')
            missing += 1
            continue
        bad = [n for n in base if n in res and base_work[n] != res_work[n]]
        if bad:
            out.append('The work done differs from the baseline, so the times '
                       'cannot be compared: %s.'
                       % ', '.join('%s (%s, was %s)' % (n, res_work[n],
                                                        base_work[n])
                                   for n in bad))
            out.append('')
            missing += 1
            continue
        out.append('| Test | Baseline | This change | Change | |')
        out.append('| --- | ---: | ---: | ---: | --- |')
        logsum = 0.0
        for n in base:
            change = (base[n] * 1.0 / res[n] - 1) * 100
            logsum += math.log(base[n] * 1.0 / res[n])
            v = verdict(change)
            worse += v == 'slower'
            better += v == 'faster'
            out.append('| %s | %.3f s | %.3f s | %+.1f%% | %s |'
                       % (n, base[n] / 1000.0, res[n] / 1000.0, change, v))
        mean = (math.exp(logsum / len(base)) - 1) * 100
        v = 'slower' if mean < -MEAN_LIMIT else \
            'faster' if mean > MEAN_LIMIT else ''
        worse += v == 'slower'
        better += v == 'faster'
        out.append('| **Display (geometric mean)** | | | **%+.1f%%** | %s |'
                   % (mean, v))
        out.append('')

    if worse or missing:
        head = ('pBench: performance is significantly **worse** than the '
                'baseline' if worse else 'pBench: no result')
    elif better:
        head = ('pBench: performance is significantly **better** than the '
                'baseline')
    else:
        head = 'pBench: performance is as the baseline'
    text = ['### ' + head, '',
            'Hatari, `%s`. Significant: a test more than %g%% or the mean more '
            'than %g%% from the baseline (`tools/pBench/ci-baselines`).'
            % (a.key, TEST_LIMIT, MEAN_LIMIT), ''] + out
    if better and not worse:
        text += ['If the speedup is intended, move the baseline up: see '
                 '"Performance baselines for CI" in `doc/pbench.txt`.', '']
    text = '\n'.join(text)
    print(text)
    if a.report:
        with open(a.report, 'w') as f:
            f.write(text + '\n')
    sys.exit(1 if worse or missing else 2 if better else 0)


if __name__ == '__main__':
    main()
