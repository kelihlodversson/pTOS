#!/usr/bin/env python3
"""Drive opt-in GEM modal tests through QEMU's actual keyboard device."""
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


def marker(text):
    while True:
        if Path(serial).exists() and text.encode() in Path(serial).read_bytes():
            return
        if time.monotonic() >= deadline:
            raise SystemExit("Missing modal test marker: " + text)
        time.sleep(0.03)


def key(name):
    command("send-key", {"keys": [{"type": "qcode", "data": name}], "hold-time": 40})
    time.sleep(0.12)


command("qmp_capabilities")
marker("x32 GEM form input: READY")
time.sleep(0.2)
for name in ("a", "2", "ret"):
    key(name)
marker("x32 GEM alert input: READY")
time.sleep(0.2)
if output:
    destination = Path(output).resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    command("screendump", {"filename": str(destination), "format": "png"})
key("ret")
marker("x32 GEM modal forms: PASS")
stream.close()
connection.close()
print("Real keyboard/modal GEM form tests passed.")
