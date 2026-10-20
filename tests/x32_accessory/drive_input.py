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
}
seen = 0
connection = None
try:
    while time.monotonic() < deadline:
        if args.log.exists():
            output = args.log.read_text(errors="replace")
            assert not any(x in output for x in ("panic:", "accessory input FAIL")), output[-4000:]
            ready = re.findall(r"x32 input (keyboard|mouse|button|release): READY", output)
            for stage in ready[seen:]:
                if connection is None:
                    connection = socket.socket(socket.AF_UNIX)
                    connection.settimeout(2)
                    connection.connect(args.monitor)
                connection.settimeout(2)
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
                assert seen == 16 and output.count("x32 input events: PASS") == 4
                print("PS/2 keyboard, movement and button input in four sessions: PASS")
                break
        time.sleep(0.01)
    else:
        raise AssertionError("timed out driving accessory input")
finally:
    if connection is not None:
        connection.close()
