"""Drive real PS/2 input when the loaded accessory announces each wait."""
import argparse
from pathlib import Path
import re
import socket
import time

parser = argparse.ArgumentParser()
parser.add_argument("log", type=Path)
parser.add_argument("monitor")
parser.add_argument("--timeout", type=float, default=60)
args = parser.parse_args()
deadline = time.monotonic() + args.timeout
commands = {
    "keyboard": "sendkey a",
    "mouse": "mouse_move 30 10",
    "button": "mouse_button 1",
    "release": "mouse_button 0",
    "key": "sendkey b",
}
seen = 0
connection = None
try:
    while time.monotonic() < deadline:
        if args.log.exists():
            # Preserve a trailing CR: only the LF completes a serial record.
            with args.log.open(errors="replace", newline="") as log:
                output = log.read()
            assert not any(x in output for x in ("panic:", "accessory input FAIL", "accessory window FAIL")), output[-4000:]
            ready = list(re.finditer(
                r"x32 (?:input (?P<input>keyboard|mouse|button|release)|window (?P<window>key|close)): READY"
                r"(?: (?P<x>-?\d+) (?P<y>-?\d+) (?P<old_x>-?\d+) (?P<old_y>-?\d+))?\r?\n", output))
            for match in ready[seen:]:
                stage = match['input'] or match['window']
                if connection is None:
                    connection = socket.socket(socket.AF_UNIX)
                    connection.settimeout(2)
                    connection.connect(args.monitor)
                connection.settimeout(2)
                if stage == 'close':
                    dx = int(match['x']) - int(match['old_x'])
                    dy = int(match['y']) - int(match['old_y'])
                    connection.sendall(("mouse_move %d %d\n" % (dx, dy)).encode())
                    time.sleep(0.1)
                    connection.sendall(b"mouse_button 1\n")
                    time.sleep(0.1)
                    connection.sendall(b"mouse_button 0\n")
                else:
                    connection.sendall((commands[stage] + "\n").encode())
                seen += 1
            # Drain the monitor's terminal echo so its output cannot block it.
            if connection is not None:
                connection.settimeout(0.001)
                try:
                    connection.recv(65536)
                except socket.timeout:
                    pass
            if "x32 accessory desktop: PASS" in output:
                assert seen == 24 and output.count("x32 input events: PASS") == 4
                assert output.count("x32 window events/ownership/lifecycle: PASS") == 4
                assert output.count("x32 window reopened on desktop: PASS") == 2
                print("PS/2 input, focused window keys and native close clicks in four sessions: PASS")
                break
        time.sleep(0.01)
    else:
        raise AssertionError("timed out driving accessory input")
finally:
    if connection is not None:
        connection.close()
