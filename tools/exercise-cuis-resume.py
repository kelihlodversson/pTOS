#!/usr/bin/env python3
"""Drive the ring-3 SAME_PROCESS resume test with actual PS/2 input."""
from pathlib import Path
import socket
import sys
import time

endpoint, serial, limit = sys.argv[1:]
deadline = time.monotonic() + float(limit)

def marker(value):
    while time.monotonic() < deadline:
        data = Path(serial).read_bytes() if Path(serial).exists() else b""
        if value.encode() in data:
            return
        if b"Some tests FAILED." in data or b"\nFAIL\r" in data:
            raise SystemExit("CUIS resume worker failed before " + value)
        time.sleep(0.01)
    raise SystemExit("CUIS resume marker missing: " + value)

marker("CUIS resume first: READY")
with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
    connection.settimeout(5)
    connection.connect(endpoint)
    buffer = b""
    def prompt():
        global buffer
        while b"(qemu) " not in buffer:
            part = connection.recv(4096)
            if not part:
                raise RuntimeError("QEMU monitor closed")
            buffer += part
        buffer = b""
    def command(text):
        connection.sendall((text + "\n").encode())
        prompt()
    prompt()
    for _ in range(16):
        command("mouse_move -127 -127")
        time.sleep(0.01)
    command("mouse_move 100 150")
    time.sleep(0.03)
    command("mouse_button 1")
    started = time.monotonic()
    marker("CUIS resume checkpoint: READY")
    print("First checkpoint after %.3fs" % (time.monotonic()-started),flush=True)
    command("mouse_button 0")
    marker("CUIS resume adopt: READY")
    print("Adoption after %.3fs" % (time.monotonic()-started),flush=True)
    command("mouse_button 1")
    marker("CUIS resume second: READY")
    command("mouse_button 0")
    marker("native CUIS resume/live commit: PASS")
    print("Native SAME_PROCESS resume/live count=2, no redelivery: PASS; %.3fs from first press" %
          (time.monotonic() - started))

    # Cancellation owns a separate PROCESS; the outer worker shares ASPACE
    # and must survive both group exits. Physical presses enter admitted waits.
    marker("CUIS initial button: READY")
    command("mouse_button 1")
    marker("native CUIS initial button before admission: PASS")
    command("mouse_button 0")

    marker("CUIS cancel button: READY")
    command("mouse_button 1")
    marker("native CUIS tracked button cancellation: PASS")
    command("mouse_button 0")
    marker("CUIS cancel resume first: READY")
    command("mouse_button 1")
    marker("CUIS cancel resume checkpoint: READY")
    command("mouse_button 0")
    # The cancellation peer emits this only after the leader has blocked in
    # the resumed wait; a leader-side pre-wait marker admits input too early.
    marker("CUIS cancel resume admitted: READY")
    command("mouse_button 1")
    marker("native CUIS resumed button cancellation: PASS")
    command("mouse_button 0")
    marker("native CUIS history cancellation: PASS")
    print("Native admitted tracking/resume cancellation and independent PROCESS survival: PASS")
