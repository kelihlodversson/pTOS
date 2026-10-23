"""Exercise the real input driver while serial READY records arrive in pieces."""
from pathlib import Path
import select
import socket
import subprocess
import sys
import tempfile


with tempfile.TemporaryDirectory(prefix="ptos-input-") as temporary:
    directory = Path(temporary)
    log = directory / "boot.log"
    monitor = directory / "monitor.sock"
    log.write_bytes(b"")
    with socket.socket(socket.AF_UNIX) as listener:
        listener.bind(str(monitor))
        listener.listen(1)
        listener.settimeout(2)
        process = subprocess.Popen(
            [sys.executable, str(Path(__file__).with_name("drive_input.py")),
             str(log), str(monitor), "--timeout", "20"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        connection = None
        try:
            stages = (
                (b"x32 input keyboard: READY", b"sendkey a\n"),
                (b"x32 input mouse: READY", b"mouse_move 30 10\n"),
                (b"x32 input button: READY", b"mouse_button 1\n"),
                (b"x32 input release: READY", b"mouse_button 0\n"),
                (b"x32 window key: READY", b"sendkey b\n"),
                (b"x32 window close: READY 120 80 100 -123",
                 b"mouse_move 20 203\nmouse_button 1\nmouse_button 0\n"),
                (b"x32 object dialog: READY", b"sendkey a\nsendkey ret\n"),
                (b"x32 object menu: READY 20 8 100 -123",
                 b"mouse_move -80 131\nmouse_move 0 20\nmouse_button 1\nmouse_button 0\n"),
                (b"x32 object cancel: READY 20 8 100 -123",
                 b"mouse_move -80 131\nmouse_move 0 20\nmouse_button 1\nmouse_button 0\nmouse_move 480 472\n"),
                (b"x32 object modal: READY", b"mouse_button 1\nmouse_button 0\n"),
            )
            with log.open("ab", buffering=0) as serial:
                for session in range(4):
                    for record, expected in stages:
                        # Close records pause before the coordinates and in
                        # the final number, reproducing both reported races.
                        split = record.index(b"READY") + len(b"READY")
                        suffix = record[split:]
                        chunks = (record[:split], suffix[:-1], suffix[-1:], b"\r")
                        for chunk in chunks:
                            if not chunk:
                                continue
                            serial.write(chunk)
                            watched = connection if connection is not None else listener
                            assert not select.select([watched], [], [], 0.06)[0], \
                                "input sent before a complete READY line"
                            assert process.poll() is None, "input driver exited on a partial record"
                        serial.write(b"\n")
                        if connection is None:
                            connection, _ = listener.accept()
                            connection.settimeout(2)
                        received = b""
                        while len(received) < len(expected):
                            chunk = connection.recv(4096)
                            assert chunk, "input driver closed the monitor"
                            received += chunk
                        assert received == expected, (received, expected)
                    serial.write(b"x32 input events: PASS\r\n"
                                 b"x32 object active menu cancellation: PASS\r\n"
                                 b"x32 object cancelled modal preserves new locks: PASS\r\n"
                                 b"x32 object USERDEF fault: ARMED\r\n"
                                 b"x32 window events/ownership/lifecycle: PASS\r\n"
                                 b"x32 objects snapshot/menu/dialog/USERDEF/revocation: PASS\r\n")
                serial.write(b"x32 window reopened on desktop: PASS\r\n" * 2)
                serial.write(b"x32 accessory desktop: PASS\r\n")
            output, error = process.communicate(timeout=2)
            assert process.returncode == 0, output + error
            assert "four sessions: PASS" in output, output
            print("Input driver fragmented READY records and exact coordinates: PASS")
        finally:
            if process.poll() is None:
                process.terminate()
                process.communicate(timeout=2)
            if connection is not None:
                connection.close()
