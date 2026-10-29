#!/usr/bin/env python3
"""Drive opt-in GEM forms and popups through QEMU keyboard/mouse devices."""
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


def mouse_event(events):
    command("input-send-event", {"events": events})


def control_move(dx, dy):
    while dx or dy:
        x, y = max(-127, min(127, dx)), max(-127, min(127, dy))
        mouse_event([{"type": "rel", "data": {"axis": "x", "value": x}},
                     {"type": "rel", "data": {"axis": "y", "value": y}}])
        dx, dy = dx-x, dy-y
        time.sleep(0.03)


def button(down):
    mouse_event([{"type": "btn", "data": {"down": down, "button": "left"}}])
    time.sleep(0.15)


command("qmp_capabilities")
for stage in range(5):
    marker("x32 GEM frame control %d: READY" % stage)
    for _ in range(32):
        control_move(-127, -127)
    control_move(210 if stage == 1 else 80 if stage >= 3 else 30, 40)
    button(True)
    if stage == 0:
        control_move(50, 50)  # Close: release outside cancels.
    elif stage == 1:
        control_move(-130, 50)
        time.sleep(0.1)
        control_move(130, -50)  # Full: leaving and returning reselects.
    elif stage == 3:
        marker("x32 GEM mover poll: PASS")  # Polls must finish while held.
        control_move(60, 40)  # Move: propose a new position, preserving size.
    elif stage == 4:
        control_move(60, 40)  # Poll-only clients must also receive WM_MOVED.
    button(False)
    if stage == 0:
        key("ret")  # Barrier after the cancelled gesture.
marker("x32 GEM frame controls: PASS")
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

def move(dx, dy):
    mouse_event([{"type": "rel", "data": {"axis": "x", "value": dx}},
                 {"type": "rel", "data": {"axis": "y", "value": dy}}])


for stage, (x, y) in enumerate(((35, 55), (5, 5), (35, 35), (35, 55))):
    marker("x32 GEM popup %d: READY" % stage)
    time.sleep(0.2)
    # The PC has a relative PS/2 mouse. Clamp against the upper-left edge
    # with bounded packets before moving to the requested screen pixel.
    for _ in range(32):
        move(-127, -127)
        time.sleep(0.02)
    move(x, y)
    time.sleep(0.2)
    mouse_event([{"type": "btn", "data": {"down": True, "button": "left"}}])
    time.sleep(0.1)
    mouse_event([{"type": "btn", "data": {"down": False, "button": "left"}}])
marker("x32 GEM popup menus: PASS")
marker("x32 GEM menu bar: READY")
time.sleep(0.3)
# Home outside the titles so homing itself cannot start menu tracking.
for _ in range(32):
    move(-127, 0)
    time.sleep(0.02)
move(127, 0)
move(73, 0)
for _ in range(32):
    move(0, -127)
    time.sleep(0.02)
# Queue an application click at the future dropdown item before tracking.
move(-120, 33)
mouse_event([{"type": "btn", "data": {"down": True, "button": "left"}}])
time.sleep(0.1)
mouse_event([{"type": "btn", "data": {"down": False, "button": "left"}}])
time.sleep(0.1)
move(-50, -25)
time.sleep(0.25)
move(50, 0)
time.sleep(0.25)
move(0, 25)
time.sleep(0.2)
mouse_event([{"type": "btn", "data": {"down": True, "button": "left"}}])
time.sleep(0.1)
mouse_event([{"type": "btn", "data": {"down": False, "button": "left"}}])
marker("x32 GEM menu bar: PASS")
stream.close()
connection.close()
print("Real keyboard/mouse GEM form and popup tests passed.")
