#!/usr/bin/env python3
#
# run-baselines.py - run pBench under Hatari for several systems and screen
#                    modes, and save the results as baselines
#
# Copyright (C) 2026 The pTOS development team.
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.
#
# Usage: run-baselines.py --pbench PBENCH.PRG --out DIR [--jobs N]
#                         [--res low,med,high] [--blitter on,off]
#                         --system KEY=IMAGE[=NAME] ...
#
# For every system, runs run-hatari.py in the ST screen modes given by --res
# (default all three: low, medium, high) with and without the blitter (--blitter
# restricts that), and writes DIR/KEY-MODE-BLIT.txt,
# for example tos206-med-blitter.txt or emutos256-high-noblitter.txt.
# Several Hatari instances are run at a time; their results do not depend on
# how busy the host is, since Hatari's clock is the emulated one.
#
import argparse
import concurrent.futures
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def run(job):
    key, image, name, res, blit, pbench, out = job
    dest = os.path.join(out, '%s-%s-%s.txt'
                        % (key, res, 'blitter' if blit == 'on' else 'noblitter'))
    cmd = [sys.executable, os.path.join(HERE, 'run-hatari.py'),
           '--tos', image, '--name', name, '--res', res, '--blitter', blit,
           pbench, dest]
    # Hatari now and then dies at start-up on a busy host; the result of a
    # run does not depend on that, so try again
    for attempt in range(3):
        r = subprocess.run(cmd, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True)
        if r.returncode == 0:
            break
    return dest, r.returncode, r.stdout.strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pbench', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--jobs', type=int, default=6)
    ap.add_argument('--res', default='low,med,high')
    ap.add_argument('--blitter', default='on,off')
    ap.add_argument('--system', action='append', required=True,
                    help='KEY=IMAGE[=NAME]')
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    jobs = []
    for spec in a.system:
        parts = spec.split('=', 2)
        key, image = parts[0], parts[1]
        name = parts[2] if len(parts) > 2 else key
        for res in a.res.split(','):
            for blit in a.blitter.split(','):
                jobs.append((key, image, name, res, blit,
                             os.path.abspath(a.pbench), a.out))
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as pool:
        for dest, rc, text in pool.map(run, jobs):
            print('%s: %s' % (os.path.basename(dest), 'ok' if rc == 0 else text),
                  flush=True)
            failed += rc != 0
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
