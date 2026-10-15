#!/usr/bin/env python3
#
# run-qemu.py - run pBench under QEMU on a pTOS target and save its results
#
# Copyright (C) 2026 The pTOS development team.
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.
#
# Usage: run-qemu.py --target virt-m68k --kernel virt-m68k.elf \
#                    --pbench pbench.prg --name TEXT OUTPUT.txt
#        run-qemu.py --target rpi2 --kernel kernel7.img \
#                    --image pbench-hd.img --name TEXT OUTPUT.txt
#
#   virt-m68k   pbench.prg must be an m68k build; it is put, with an
#               EMUDESK.INF that starts it, in a directory that QEMU shows
#               to the machine as a virtio disk
#   rpi2        --image is the disk made by "make pbench-hd.img" (an ARM
#               configuration), used as the SD card of a raspi2b machine
#
# The results are read from the serial port, where pBench prints them, and
# the file written is in the format of the Hatari baselines.
#
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

EMUDESK_INF = '''#R 01
#E 1A E1
#Q 41 40 43 40 43 40
#Z 00 C:\\PBENCH.PRG@
#M 00 00 00 FF C Drive C@ @
#T 00 01 03 FF   Trash@ @
#F FF 07 @ *.*@
#N FF 07 @ *.*@
#D FF 02 @ *.*@
#G 06 FF *.APP@ @ 000 @
#G 06 FF *.PRG@ @ 000 @
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--target', required=True, choices=['virt-m68k', 'rpi2'])
    ap.add_argument('--kernel', required=True)
    ap.add_argument('--pbench')
    ap.add_argument('--image')
    ap.add_argument('--name', required=True)
    ap.add_argument('--timeout', type=int, default=600)
    ap.add_argument('output')
    a = ap.parse_args()
    if a.target == 'virt-m68k' and not a.pbench:
        ap.error('--target virt-m68k needs --pbench')
    if a.target == 'rpi2' and not a.image:
        ap.error('--target rpi2 needs --image')

    work = tempfile.mkdtemp(prefix='pbench-qemu-')
    serial = os.path.join(work, 'serial.txt')
    if a.target == 'virt-m68k':
        disk = os.path.join(work, 'disk')
        os.mkdir(disk)
        shutil.copy(a.pbench, os.path.join(disk, 'PBENCH.PRG'))
        with open(os.path.join(disk, 'EMUDESK.INF'), 'w') as f:
            f.write(EMUDESK_INF)
        qemu = 'qemu-system-m68k'
        cmd = [qemu, '-M', 'virt', '-m', '128', '-cpu', 'm68020',
               '-kernel', a.kernel,
               '-global', 'virtio-mmio.force-legacy=false',
               '-drive', 'file=fat:rw:%s,format=raw,if=none,id=hd0' % disk,
               '-device', 'virtio-blk-device,drive=hd0',
               '-device', 'virtio-gpu-device',
               '-device', 'virtio-keyboard-device',
               '-device', 'virtio-tablet-device']
        machine = 'virt (m68020)'
    else:
        qemu = 'qemu-system-arm'
        cmd = [qemu, '-M', 'raspi2b', '-bios', a.kernel,
               '-drive', 'file=%s,format=raw,if=sd' % a.image,
               '-device', 'usb-mouse', '-device', 'usb-kbd']
        machine = 'raspi2b'
    cmd += ['-display', 'none', '-serial', 'file:' + serial]
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    lines = []
    screen = None
    deadline = time.time() + a.timeout
    while time.time() < deadline and proc.poll() is None:
        time.sleep(2)
        if not os.path.exists(serial):
            continue
        text = open(serial, errors='replace').read().replace('\r', '')
        # the serial port also carries the kernel's own messages
        lines = [l for l in text.splitlines()
                 if re.search(r' \d+\.\d{3} s ', l)]
        m = re.search(r'^Screen .*$', text, re.M)
        screen = m.group(0) if m else None
        if len(lines) >= 10:
            break
    proc.kill()
    proc.wait()
    shutil.rmtree(work, ignore_errors=True)
    lines = lines[-10:]
    if len(lines) < 10:
        sys.exit('no results from QEMU (timeout or crash)')

    version = subprocess.run([qemu, '--version'], capture_output=True,
                             text=True).stdout.splitlines()[0]
    with open(a.output, 'w') as f:
        f.write('# pBench baseline\n')
        f.write('# system: %s\n' % a.name)
        f.write('# emulator: %s, machine %s\n' % (version, machine))
        if screen:
            f.write('# %s\n' % screen)
        for l in lines:
            f.write(re.sub(r'\s+\d+%\s*$', '', l) + '\n')


if __name__ == '__main__':
    main()
