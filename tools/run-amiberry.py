#!/usr/bin/env python3
"""
run-amiberry.py - run a pTOS Amiga ROM under Amiberry (the WinUAE core on
Linux), headless, and ask the emulator questions through its IPC socket

  tools/run-amiberry.py --rom ptos-amiga.rom --cpu 68040 \\
      --wait 15 --cmd GET_CPU_REGS --screenshot desktop.png

Starts Amiberry with the SDL dummy video driver, a 32-bit 68030/68040/68060
with its MMU emulated, JIT and cycle-exact off, no sound and no floppy, waits,
sends each --cmd (tab separated fields: the IPC protocol of Amiberry; "SLEEP <seconds>"
waits, for a command that has to come while the guest is still running,
docs/ipc.md in its source: READ_MEM <addr> <width>, GET_CPU_REGS, ...), takes
the screenshot and stops it.  The reply of each command is printed.

The emulator is the pinned build of doc/debugging.txt; --amiberry names its
binary (default: $AMIBERRY, then ~/.local/src/amiberry/build/amiberry).

Copyright (C) 2026 The pTOS development team.

This file is distributed under the GPL, version 2 or at your
option any later version.  See doc/license.txt for details.
"""

import argparse
import os
import socket
import subprocess
import sys
import tempfile
import time

CONFIG = """config_version=5.0.0
use_gui=no
kickstart_rom_file={rom}
cpu_model={cpu}
mmu_model={cpu}
cpu_compatible=false
cpu_24bit_addressing=false
cpu_speed=max
cpu_cycle_exact={cycle_exact}
cpu_data_cache={caches}
cpu_instruction_cache={caches}
chipmem_size=4
fastmem_size=8
chipset=aga
floppy0type=-1
sound_output=none
"""

# a hard disk on the IDE of the A600 and the A1200 (Gayle), where pTOS looks for
# one: the image is a raw disk with a partition table
DISK_CONFIG = """chipset_compatible=A1200
ide=a600/a1200
uaehf0=hdf,rw,DH0:{disk},32,1,2,512,0,,ide0_mainboard
"""


def socket_path():
    runtime = os.environ.get('XDG_RUNTIME_DIR')
    return os.path.join(runtime or '/tmp', 'amiberry.sock')


def ipc(path, line, timeout=10.0):
    """One request, one reply (fields separated by tabs); the connection is
    closed afterwards."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(path)
    s.sendall(line.encode() + b'\n')
    data = b''
    while not data.endswith(b'\n'):
        chunk = s.recv(65536)
        if not chunk:
            break
        data += chunk
    s.close()
    return data.decode(errors='replace').rstrip('\n').replace('\t', '\n')


def start(amiberry, rom, cpu, disk=None, caches=False, cycle_exact=False):
    """Start the emulator, headless, on a ROM (and an image of a hard disk),
    and wait for its IPC socket: returns (the process, the socket path, the
    directory of its configuration and log)."""
    work = tempfile.mkdtemp(prefix='amiberry-tests-')
    cfg = os.path.join(work, 'amiga.uae')
    with open(cfg, 'w') as f:
        f.write(CONFIG.format(rom=os.path.abspath(rom), cpu=cpu,
                              caches='true' if caches else 'false',
                              cycle_exact='true' if cycle_exact else 'false'))
        if disk:
            f.write(DISK_CONFIG.format(disk=os.path.abspath(disk)))
    path = socket_path()
    try:
        os.unlink(path)                 # (a killed emulator leaves its socket behind)
    except OSError:
        pass
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    with open(os.path.join(work, 'amiberry.log'), 'w') as out:
        proc = subprocess.Popen([amiberry, '--log', '-G', '-f', cfg], env=env,
                                stdout=out, stderr=subprocess.STDOUT,
                                start_new_session=True)
    deadline = time.time() + 30
    while not os.path.exists(path) and time.time() < deadline:
        if proc.poll() is not None:
            raise OSError('the emulator exited (log: %s/amiberry.log)' % work)
        time.sleep(0.5)
    return proc, path, work


def stop(proc):
    try:
        ipc(socket_path(), 'QUIT', 3)
    except (OSError, socket.timeout):
        pass
    time.sleep(1)
    proc.kill()
    proc.wait()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--rom', required=True, help='the pTOS Amiga ROM image')
    ap.add_argument('--cpu', default='68030', choices=['68030', '68040', '68060'])
    ap.add_argument('--amiberry',
                    default=os.environ.get('AMIBERRY') or
                    os.path.expanduser('~/.local/src/amiberry/build/amiberry'))
    ap.add_argument('--wait', type=float, default=15, help='seconds before the commands')
    ap.add_argument('--cmd', action='append', default=[],
                    help='an IPC command, arguments separated by tabs or spaces')
    ap.add_argument('--screenshot', help='write a PNG of the screen here')
    ap.add_argument('--caches', action='store_true',
                    help='CPU instruction and data caches on (off by default)')
    ap.add_argument('--cycle-exact', action='store_true')
    ap.add_argument('--disk', help='an image of a hard disk, on the IDE')
    a = ap.parse_args()

    if not os.path.exists(a.amiberry):
        sys.exit('run-amiberry: no emulator at %s (doc/debugging.txt)' % a.amiberry)
    status = 0
    proc = None
    try:
        proc, path, work = start(a.amiberry, a.rom, a.cpu, a.disk, a.caches, a.cycle_exact)
        time.sleep(a.wait)
        if a.screenshot:
            print(ipc(path, 'SCREENSHOT\t' + os.path.abspath(a.screenshot)))
        for c in a.cmd:
            print('> ' + c)
            if c.startswith('SLEEP '):          # (not a command of the emulator: wait)
                time.sleep(float(c.split()[1]))
                continue
            print(ipc(path, c.replace(' ', '\t')))
    except (OSError, socket.timeout) as e:
        print('run-amiberry: %s' % e, file=sys.stderr)
        status = 1
    finally:
        if proc:
            stop(proc)
    return status


if __name__ == '__main__':
    sys.exit(main())
