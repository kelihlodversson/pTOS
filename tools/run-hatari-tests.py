#!/usr/bin/env python3
#
# run-hatari-tests.py - run the regression tests (runtests.tos on test-hd.img)
#                       under Hatari and say whether they passed
#
# Copyright (C) 2026 The pTOS development team.
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.
#
# Usage: run-hatari-tests.py --tos IMAGE --disk test-hd.img --mode MODE
#                            [--timeout SEC]
#
#   --tos IMAGE   the pTOS ROM image (ptos512k.img, ...)
#   --disk FILE   the test disk made by "make test-hd"
#   --mode MODE   low, med or high: an STE with 4 MB in that ST screen mode;
#                 f030a: the Falcon of f030 for the image with an address
#                 space for every process (falcon-aspace, #727), which has no
#                 desktop and no VDI: the disk is the one of "make
#                 test-hd-shell", whose shell is the test harness, so the
#                 tests start by themselves, and the two blitter suites are
#                 not expected;
#                 f040a and tt040a: the same for the image with the 68040 MMU
#                 (falcon-aspace040, #728), on a Falcon and on a TT (without
#                 TT-RAM, which the 68040 tree does not map);
#                 tta: a TT with a 68030, 10 MB of ST-RAM and 32 MB of TT-RAM,
#                 32-bit addressing, for the same image and disk as f030a
#                 (#728), on ACSI, as the TT has no IDE;
#                 f030 or f040: a Falcon with a 68030 or a 68040 and 14 MB,
#                 in the screen mode pTOS starts it in (640x480, 16 colours),
#                 with the MMU emulated (--mmu on), so that the page tables
#                 pTOS builds for the caches (CONF_WITH_68030_PMMU,
#                 CONF_WITH_68040_PMMU) are walked and not just written
#   --timeout SEC give up after this long (default 600; CI passes 900)
#
# The harness prints what each test did on the console, which Hatari's
# --conout 2 sends to its standard output; the run is over when the summary
# line has been printed (the harness leaves the machine at the desktop, so
# the emulator is stopped then).  The run also stops at once, with the text, when
# the kernel panics: its handler waits for a key, and the text is on the emulated
# screen only, unless Hatari gets --natfeats on, when kprintf() (which the panic
# uses too) sends it to the log; the TT modes read it from the screen, where
# natfeats upsets the capture.  Exit status: 0 if every test passed, 1 if one
# failed, the kernel panicked, the run timed out or
# did not finish, 2 if text_blit or
# raster_copy skipped itself, which is a failure here: that they run on the
# machine and the screen mode is the point of this.
#
import argparse
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

PNG_SIG = b'\x89PNG\r\n\x1a\n'
PNG_END = b'IEND\xaeB`\x82'


def load_font():
    """The 8x16 console font of the ROM: 16 rows of 256 bytes, one per character."""
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       '..', 'bios', 'fnt_st_8x16.c')
    with open(src) as f:
        body = f.read().split('dat_table[] =', 1)[1].split('};', 1)[0]
    data = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', body))
    glyphs = {}
    for c in range(32, 127):
        glyphs[bytes(data[r * 256 + c] for r in range(16))] = chr(c)
    return glyphs


def screen_text(avi, glyphs):
    """The text of the last frame of the video Hatari is recording, or ''.

    The console output of --conout is taken from the stack of the BIOS call,
    which Hatari reads as a physical address: with the kernel in the upper half
    of a TT (32-bit addressing, no mirror of the window) it finds nothing.  What
    the harness printed is on the screen, though, in the font of the ROM.
    """
    from PIL import Image
    try:
        with open(avi, 'rb') as f:
            f.seek(0, os.SEEK_END)
            f.seek(max(0, f.tell() - 400000))
            tail = f.read()
    except OSError:
        return ''
    end = tail.rfind(PNG_END)
    start = tail.rfind(PNG_SIG, 0, end) if end >= 0 else -1
    if start < 0:
        return ''
    try:
        img = Image.open(io.BytesIO(tail[start:end + len(PNG_END)])).convert('L')
    except Exception:
        return ''
    px = img.load()
    lines = []
    for row in range(min(30, img.height // 16)):
        line = ''
        for col in range(min(80, img.width // 8)):
            key = bytes(sum((px[col * 8 + b, row * 16 + r] < 128) << (7 - b)
                            for b in range(8)) for r in range(16))
            line += glyphs.get(key, ' ' if not any(key) else '?')
        lines.append(line.rstrip())
    return '\n'.join(lines)


SUMMARY = re.compile(r'--- Summary: (\d+) passed, (\d+) failed, (\d+) total ---')
# the first line of dopanic()'s output (bios/kprint.c), which kprintf() sends to the log
PANIC = re.compile(r'^Panic: ', re.M)
MUST_RUN = ('text_blit', 'raster_copy')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tos', required=True)
    ap.add_argument('--disk', required=True)
    ap.add_argument('--mode', required=True,
                    choices=['low', 'med', 'high', 'f030', 'f040', 'f030a', 'tta', 'f040a', 'tt040a'])
    ap.add_argument('--timeout', type=int, default=600)
    a = ap.parse_args()

    work = tempfile.mkdtemp(prefix='hatari-tests-')
    disk = os.path.join(work, 'test-hd.img')    # Hatari locks the file
    shutil.copy(a.disk, disk)
    log = os.path.join(work, 'console.txt')
    if a.mode in ('f030', 'f040', 'f030a', 'f040a'):
        cmd = ['hatari', '--tos', a.tos, '--machine', 'falcon',
               '--cpulevel', '4' if a.mode in ('f040', 'f040a') else '3',
               '--memsize', '14', '--dsp', 'none', '--sound', 'off',
               '--mmu', 'on', '--monitor', 'vga', '--ide-master', disk]
    elif a.mode in ('tta', 'tt040a'):
        cmd = ['hatari', '--tos', a.tos, '--machine', 'tt',
               '--cpulevel', '4' if a.mode == 'tt040a' else '3',
               '--addr24', 'off', '--memsize', '10',
               '--ttram', '32',
               '--sound', 'off', '--mmu', 'on',
               '--monitor', 'vga', '--acsi', '0=' + disk]
    else:
        cmd = ['hatari', '--tos', a.tos, '--machine', 'ste', '--memsize', '4',
               '--sound', 'off', '--tos-res', a.mode,
               '--monitor', 'mono' if a.mode == 'high' else 'rgb',
               '--acsi', disk]
    cmd += ['--conout', '2', '--fast-forward', 'on']
    avi = os.path.join(work, 'screen.avi')
    glyphs = None
    if a.mode not in ('tta', 'tt040a'):
        cmd += ['--natfeats', 'on']         # the panic text reaches the log (below)
    if a.mode in ('tta', 'tt040a'):         # --conout shows nothing here: read the screen
        glyphs = load_font()
        cmd += ['--avirecord', '--avi-vcodec', 'png', '--avi-file', avi]
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    with open(log, 'w') as out:
        proc = subprocess.Popen(cmd, env=env, stdout=out,
                                stderr=subprocess.STDOUT)
        text = ''
        summary = None
        panic = None
        deadline = time.time() + a.timeout
        while time.time() < deadline and proc.poll() is None:
            time.sleep(2)
            with open(log, errors='replace') as f:
                text = f.read()
            if glyphs:
                text = screen_text(avi, glyphs)
            panic = PANIC.search(text)
            if panic:
                panic = text[panic.start():]
                break
            summary = SUMMARY.search(text)
            if summary:
                break
        proc.kill()
        proc.wait()
    if not glyphs:
        with open(log, errors='replace') as f:
            text = f.read()
    shutil.rmtree(work, ignore_errors=True)

    text = text.replace('\r', '')
    print(text)
    if panic:
        print('run-hatari-tests: the kernel panicked:\n' +
              '\n'.join(panic.replace('\r', '').splitlines()[:12]),
              file=sys.stderr)
        return 1
    summary = SUMMARY.search(text)
    if not summary:
        print('run-hatari-tests: no summary from the test harness '
              '(timeout or crash)', file=sys.stderr)
        return 1
    if int(summary.group(2)):
        print('run-hatari-tests: %s tests failed' % summary.group(2),
              file=sys.stderr)
        return 1
    if a.mode in ('f030a', 'tta', 'f040a', 'tt040a'):       # no VDI, so no blitter suites
        return 0
    skipped = [l for l in text.splitlines()
               if 'SKIP' in l and any(t in l for t in MUST_RUN)]
    if skipped:
        print('run-hatari-tests: skipped, but they have to run on this '
              'machine:\n  ' + '\n  '.join(skipped), file=sys.stderr)
        return 2
    # the suites are found by the build, so one that vanished (or was
    # renamed) would otherwise leave this green with its coverage gone: each
    # has to have printed a group line that ends in PASS
    missing = [t for t in MUST_RUN
               if not any(l.strip().startswith(t + ':') and
                          l.rstrip().endswith('PASS')
                          for l in text.splitlines())]
    if missing:
        print('run-hatari-tests: no passing result from %s'
              % ', '.join(missing), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
