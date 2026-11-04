#!/usr/bin/env python3
"""Capture QEMU's framebuffer after the userspace GEM demo is painted."""
import json
from pathlib import Path
import socket
import sys
import time

endpoint, serial, output = sys.argv[1:4]
deadline = time.monotonic() + float(sys.argv[4])
connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
while True:
    try:
        connection.connect(endpoint)
        break
    except (FileNotFoundError, ConnectionRefusedError):
        if time.monotonic() >= deadline:
            raise SystemExit("QMP socket did not become available")
        time.sleep(0.03)
connection.settimeout(5)
stream = connection.makefile("rwb")
json.loads(stream.readline())

def command(name, arguments=None):
    stream.write((json.dumps({"execute": name, "arguments": arguments or {}}) + "\n").encode())
    stream.flush()
    while True:
        message = json.loads(stream.readline())
        if "error" in message:
            raise RuntimeError(message["error"])
        if "return" in message:
            return message["return"]

command("qmp_capabilities")
while True:
    try:
        ready = b"x32 GEM object screenshot: READY" in Path(serial).read_bytes()
    except FileNotFoundError:
        ready = False
    if ready:
        break
    if time.monotonic() >= deadline:
        raise SystemExit("GEM object demo did not reach screenshot marker")
    time.sleep(0.03)
# Let the display surface reflect the completed framebuffer writes.
time.sleep(0.1)
destination = Path(output).resolve()
destination.parent.mkdir(parents=True, exist_ok=True)
command("screendump", {"filename": str(destination), "format": "png"})
print("Saved QEMU screenshot:", destination)
stream.close()
connection.close()
