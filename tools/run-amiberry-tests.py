#!/usr/bin/env python3
"""
run-amiberry-tests.py - run the regression tests of tests/ on an Amiga ROM of
pTOS under Amiberry (#737)

  tools/run-amiberry-tests.py --rom ptos-amiga.rom --disk test-hd.img --cpu 68040

The Amiga ROM boots to the desktop from the test disk on the IDE, which runs
runtests.tos by the autorun line of emudesk.inf (tests/readme.md).  What the
program prints goes to a screen that nobody reads, so a build for the Amiga
(TEST_LOG_FILE, tests/testlib.c) writes it to RUNTESTS.LOG on the disk as well,
a test at a time, and this script reads the file from a copy of the image
(mtools) until the summary is there.  It fails on a failed test, on a missing
summary within the timeout, and when the emulator stops.  The log is printed.

Needs mtools (mtype, mdel) on the host.  The emulator is the pinned Amiberry
(doc/debugging.txt), $AMIBERRY or --amiberry.

Copyright (C) 2026 The pTOS development team.

This file is distributed under the GPL, version 2 or at your
option any later version.  See doc/license.txt for details.
"""

import argparse
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

PARTITION_OFFSET = 2048 * 512           # tools/mkhdisk.sh: the partition starts at sector 2048
SUMMARY = re.compile(r'--- Summary: (\d+) passed, (\d+) failed, (\d+) total ---')


def load_runner():
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location('run_amiberry', os.path.join(here, 'run-amiberry.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def read_log(image):
    """The log file on the image, as the emulator has written it so far."""
    try:
        r = subprocess.run(['mtype', '-i', '%s@@%d' % (image, PARTITION_OFFSET), '::RUNTESTS.LOG'],
                           capture_output=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        return ''
    return r.stdout.decode(errors='replace').replace('\r', '') if r.returncode == 0 else ''


def clear_log(image):
    """Remove a RUNTESTS.LOG that the disk already has (a disk that was run
    before), so that only this run can make the one that is read."""
    mt = ['-i', '%s@@%d' % (image, PARTITION_OFFSET)]
    if subprocess.run(['mtype'] + mt + ['::RUNTESTS.LOG'], stdout=subprocess.DEVNULL,
                      stderr=subprocess.DEVNULL, timeout=30).returncode != 0:
        return                          # not there
    subprocess.run(['mdel'] + mt + ['::RUNTESTS.LOG'], check=True, timeout=30)


def halted(ra, path, addr):
    """The CPU is in halt() (the loop of a kernel that has nothing more to run)."""
    try:
        regs = ra.ipc(path, 'GET_CPU_REGS', 5)
    except (OSError, ValueError):
        return False
    for line in regs.split('\n'):
        if line.startswith('PC='):
            return addr <= int(line[3:], 16) < addr + 16
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--rom', required=True)
    ap.add_argument('--disk', required=True, help='the test disk (make test-hd)')
    ap.add_argument('--cpu', default='68030', choices=['68030', '68040', '68060'])
    ap.add_argument('--amiberry', default=os.environ.get('AMIBERRY') or
                    os.path.expanduser('~/.local/src/amiberry/build/amiberry'))
    ap.add_argument('--timeout', type=float, default=1200, help='seconds for the whole suite')
    ap.add_argument('--caches', action='store_true', help='CPU caches on')
    ap.add_argument('--halt', help='the address of halt() in the kernel (emutos.map): an image whose shell '
                    'is the harness halts the machine when it is done, and the emulator does not write the '
                    'log to the disk image until it is stopped, so the end is seen by the CPU')
    a = ap.parse_args()

    if not os.path.exists(a.amiberry):
        sys.exit('run-amiberry-tests: no emulator at %s (doc/debugging.txt)' % a.amiberry)
    if shutil.which('mtype') is None or shutil.which('mdel') is None:
        sys.exit('run-amiberry-tests: mtools is needed (mtype, mdel)')

    ra = load_runner()
    work = tempfile.mkdtemp(prefix='amiberry-disk-')
    image = os.path.join(work, 'test-hd.img')           # (the emulator writes to it)
    shutil.copy(a.disk, image)
    try:
        clear_log(image)
    except (OSError, subprocess.SubprocessError) as e:
        shutil.rmtree(work, ignore_errors=True)
        sys.exit('run-amiberry-tests: cannot clear the old RUNTESTS.LOG of the disk: %s' % e)

    proc = None
    path = None
    text = ''
    try:
        proc, path, emu = ra.start(a.amiberry, a.rom, a.cpu, image, a.caches)
        deadline = time.time() + a.timeout
        while time.time() < deadline:
            time.sleep(5)
            text = read_log(image)
            if SUMMARY.search(text) and ('All tests passed.' in text or 'Some tests FAILED.' in text):
                break
            if proc.poll() is not None:
                print('run-amiberry-tests: the emulator stopped (log: %s/amiberry.log)' % emu,
                      file=sys.stderr)
                break
            if a.halt and halted(ra, path, int(a.halt, 16)):
                break
    finally:
        if proc:
            ra.stop(proc, path)
        text = read_log(image) or text
        shutil.rmtree(work, ignore_errors=True)

    print(text)
    m = SUMMARY.search(text)
    if not m:
        print('run-amiberry-tests: no summary from the test harness (timeout or crash)',
              file=sys.stderr)
        return 1
    if int(m.group(2)) or 'All tests passed.' not in text:
        print('run-amiberry-tests: %s tests failed' % m.group(2), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
