#!/usr/bin/env python3
#
# run-hatari.py - run pBench under Hatari and save its results
#
# Copyright (C) 2026 The pTOS development team.
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.
#
# Usage: run-hatari.py [options] PBENCH.PRG OUTPUT.txt
#
#   --tos FILE       the TOS/EmuTOS image to run (required)
#   --name TEXT      what to call it in the result file (default: the image)
#   --res low|med|high   the ST screen mode (default med)
#   --blitter on|off     the STE (which has a blitter) or the plain ST (which
#                        does not) is emulated; default on
#   --falcon MODE    emulate a Falcon with TOS 4.04 (--tos) instead, in the
#                    Videl mode MODE (four hex digits, e.g. 001C for
#                    640x480 true colour on a VGA monitor); --res/--blitter
#                    do not apply
#   --timeout SEC    give up after this long (default 1800)
#
# pBench is run unattended: Hatari starts it with --auto, and once it has
# written PBENCH.TXT into the emulated hard disk (a host directory) the
# emulator is stopped.  The result file holds a header saying how it was made
# and the lines pBench printed, without their percentage column (that is
# relative to whatever reference the pBench build had, which is not part of
# the measurement).  Hatari must be able to run without a display
# (SDL_VIDEODRIVER=dummy is set).
#
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

NTESTS = 10


def hatari_version():
    r = subprocess.run(['hatari', '--version'], capture_output=True, text=True)
    m = re.search(r'Hatari v[\w.]+', r.stdout + r.stderr)
    return m.group(0) if m else 'Hatari (unknown version)'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tos', required=True)
    ap.add_argument('--name')
    ap.add_argument('--res', default='med', choices=['low', 'med', 'high'])
    ap.add_argument('--blitter', default='on', choices=['on', 'off'])
    ap.add_argument('--falcon')
    ap.add_argument('--timeout', type=int, default=1800)
    ap.add_argument('pbench')
    ap.add_argument('output')
    a = ap.parse_args()

    work = tempfile.mkdtemp(prefix='pbench-')
    shutil.copy(a.pbench, os.path.join(work, 'PBENCH.PRG'))
    shots = os.path.join(work, 'shots')
    os.mkdir(shots)
    if a.falcon:
        # TOS 4.04 gets its video mode from bytes 5 and 6 of the #E line of
        # NEWDESK.INF, and runs the program of the #Z line, which must come
        # first; Hatari's --auto would replace the file, so it is not used
        machine = 'falcon'
        with open(os.path.join(work, 'NEWDESK.INF'), 'w', newline='') as f:
            f.write('#R 01\r\n#Z 01 C:\\PBENCH.PRG@\r\n'
                    '#a000000\r\n#b000000\r\n'
                    '#c7770007000600070055200505552220770557075055507703111103'
                    '\r\n#d\r\n#E 18 03 00 06 %s %s 00 00 00 00\r\n'
                    '#M 00 00 00 FF C Drive C@ @\r\n'
                    % (a.falcon[:2], a.falcon[2:]))
        cmd = ['hatari', '--tos', a.tos, '--machine', 'falcon',
               '--monitor', 'vga', '--memsize', '14', '--dsp', 'none',
               '--sound', 'off', '--fast-forward', 'on', '--fastfdc', 'on',
               '--screenshot-dir', shots, work]
    else:
        machine = 'ste' if a.blitter == 'on' else 'st'
        # the directory goes last: Hatari boots from the drive given last,
        # and needs to boot from C: for --auto to work with the original TOS
        cmd = ['hatari', '--tos', a.tos, '--machine', machine,
               '--memsize', '4', '--sound', 'off', '--tos-res', a.res,
               '--monitor', 'mono' if a.res == 'high' else 'rgb',
               '--fast-forward', 'on', '--fastfdc', 'on',
               '--auto', 'C:\\PBENCH.PRG', '--screenshot-dir', shots, work]
        if machine == 'st':
            cmd[1:1] = ['--blitter', 'off']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    proc = subprocess.Popen(cmd, env=env, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    result = os.path.join(work, 'PBENCH.TXT')
    lines = None
    screen = None
    deadline = time.time() + a.timeout
    while time.time() < deadline and proc.poll() is None:
        time.sleep(2)
        if os.path.exists(result):
            time.sleep(1)       # let it finish writing
            with open(result, errors='replace') as f:
                text = f.read()
            lines = [l.rstrip('\r') for l in text.splitlines()
                     if re.search(r' \d+\.\d{3} s ', l)]
            m = re.search(r'^Screen .*$', text.replace('\r', ''), re.M)
            screen = m.group(0) if m else None
            if len(lines) >= NTESTS:
                break
            lines = None
    proc.kill()
    proc.wait()
    shutil.rmtree(work, ignore_errors=True)
    if not lines:
        sys.exit('no results from Hatari (timeout or crash)')

    name = a.name or os.path.basename(a.tos)
    with open(a.output, 'w') as f:
        f.write('# pBench baseline\n')
        f.write('# system: %s\n' % name)
        if a.falcon:
            f.write('# emulator: %s, machine FALCON, Videl mode %s\n'
                    % (hatari_version(), a.falcon.upper()))
        else:
            f.write('# emulator: %s, machine %s, screen ST-%s, blitter %s\n'
                    % (hatari_version(), machine.upper(), a.res.upper(),
                       a.blitter))
        if screen:
            f.write('# %s\n' % screen)
        for l in lines:
            f.write(re.sub(r'\s+\d+%\s*$', '', l) + '\n')


if __name__ == '__main__':
    main()
