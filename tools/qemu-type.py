#!/usr/bin/env python3
#
# qemu-type.py - type a line of text into a QEMU guest through the monitor
#
# Usage: qemu-type.py <monitor-socket> <text>
#
# Sends the characters as key presses (sendkey) followed by Return.  Only
# the characters a command line needs are known: letters, digits, space,
# '.', '\' and ':'.
#

import socket
import sys
import time

KEYS = {'.': 'dot', ' ': 'spc', '\\': 'backslash', ':': 'shift-semicolon'}


def key(c):
    if c in KEYS:
        return KEYS[c]
    if c.islower() or c.isdigit():
        return c
    if c.isupper():
        return 'shift-' + c.lower()
    raise SystemExit("qemu-type.py: no key for %r" % c)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    sock = socket.socket(socket.AF_UNIX)
    sock.connect(sys.argv[1])
    time.sleep(0.5)
    sock.recv(4096)                     # the monitor banner
    for c in sys.argv[2]:
        sock.send(('sendkey %s\n' % key(c)).encode())
        time.sleep(0.15)
    sock.send(b'sendkey ret\n')
    time.sleep(0.5)


main()
