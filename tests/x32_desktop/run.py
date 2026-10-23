"""Boot the real external x32 desktop and drive its public UI through PS/2."""
import argparse
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
import time


def firmware(name):
    candidates = {
        "code": ("/usr/share/OVMF/OVMF_CODE_4M.fd", "/opt/homebrew/share/qemu/edk2-x86_64-code.fd"),
        "vars": ("/usr/share/OVMF/OVMF_VARS_4M.fd", "/opt/homebrew/share/qemu/edk2-i386-vars.fd"),
    }
    return next((path for path in candidates[name] if Path(path).is_file()), None)


class Guest:
    def __init__(self, output, args, missing=False):
        self.output = output.resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="ptos-desktop-", dir="/tmp")
        self.root = Path(self.temporary.name)
        esp = self.root / "esp"
        (esp / "EFI" / "BOOT").mkdir(parents=True)
        shutil.copy("pc-x86_64.efi", esp / "EFI" / "BOOT" / "BOOTX64.EFI")
        shutil.copy("cli/emucon.tos", esp / "EMUCON.TOS")
        if not missing:
            for source, target in (("emudesk.prg", "EMUDESK.PRG"),
                                   ("obj/x32/GEMTEST.PRG", "GEMTEST.PRG"),
                                   ("obj/x32/DESKACC.ACC", "DESKACC.ACC"),
                                   ("desk/emudesk-raspi.inf", "TYPED.INF")):
                shutil.copy(source, esp / target)
            shutil.copy("desk/emudesk-raspi.inf", esp / "EDITED.INF")
            files = esp / "INFS"
            (files / "SUB").mkdir(parents=True)
            for number in range(14):
                shutil.copy("desk/emudesk-raspi.inf", files / ("FILE%02d.INF" % number))
            (files / "NOISE.TXT").write_text("This must not appear in an INF listing.\n")
        variables = self.root / "vars.fd"
        shutil.copy(args.ovmf_vars, variables)
        self.log = self.output / "boot.log"
        self.monitor = self.root / "monitor.sock"
        self.log.write_text("")
        self.process = subprocess.Popen([
            args.qemu, "-machine", "pc", "-m", "256", "-cpu", "qemu64,+pdpe1gb",
            "-drive", "if=pflash,format=raw,readonly=on,file=" + args.ovmf_code,
            "-drive", "if=pflash,format=raw,file=" + str(variables),
            "-drive", "format=raw,file=fat:rw:" + str(esp),
            "-serial", "file:" + str(self.log), "-display", "none",
            "-monitor", "unix:" + str(self.monitor) + ",server=on,wait=off",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.deadline = time.monotonic() + args.timeout
        self.connection = None
        self.x = self.y = 0

    def text(self):
        text = self.log.read_text(errors="replace")
        assert not any(marker in text for marker in ("panic:", "process fault:", " FAIL")), text[-5000:]
        assert text.count("EFI entry reached") <= 1, "guest reset during integration test"
        assert self.process.poll() is None, "QEMU exited before the desktop test completed"
        return text

    def wait(self, marker, count=1, timeout=15, after=0, regex=False):
        deadline = min(self.deadline, time.monotonic() + timeout)
        while time.monotonic() < deadline:
            text = self.text()
            found = len(re.findall(marker, text[after:])) if regex else text[after:].count(marker)
            if found >= count:
                return text
            time.sleep(0.05)
        raise AssertionError("timeout waiting for %r #%d\n%s" % (marker, count, self.text()[-5000:]))

    def command(self, command, delay=0.15):
        if self.connection is None:
            self.connection = socket.socket(socket.AF_UNIX)
            self.connection.settimeout(0.1)
            self.connection.connect(str(self.monitor))
        self.connection.sendall((command + "\n").encode())
        time.sleep(delay)
        try:
            while self.connection.recv(65536):
                pass
        except socket.timeout:
            pass

    def move(self, x, y):
        # Keep every PS/2 packet inside its signed-byte movement range.
        # Large monitor deltas can lose motion before a direction reversal.
        while (self.x, self.y) != (x, y):
            dx = max(-100, min(100, x - self.x))
            dy = max(-100, min(100, y - self.y))
            self.command("mouse_move %d %d" % (dx, dy), delay=0.05)
            self.x += dx
            self.y += dy

    def click(self):
        self.command("mouse_button 1")
        self.command("mouse_button 0")

    def double_click(self):
        for state in (1, 0, 1, 0):
            self.command("mouse_button %d" % state, delay=0.01)

    def key(self, key):
        self.command("sendkey " + key)

    def screenshot(self, name):
        self.command("screendump " + str(self.output / (name + ".ppm")))

    def ready(self, count):
        text = self.wait(r"x32 desktop READY \d+ \d+ \d+ \d+\r?\n", count, regex=True)
        records = re.findall(r"x32 desktop READY (\d+) (\d+) (\d+) (\d+)\r?\n", text)
        assert len(records) >= count, "partial READY record"
        self.x, self.y, width, height = map(int, records[count - 1])
        assert width >= 640 and height >= 400
        return text

    def selector(self, count):
        text = self.wait("x32 desktop selector WAIT", count)
        frame = text.split("x32 desktop selector READY ")[count].split("x32 desktop selector WAIT")[0]
        controls = {int(n): (int(x), int(y)) for n, x, y in
                    re.findall(r"x32 desktop selector CONTROL (\d+) (\d+) (\d+)\r?\n", frame)}
        rows = {name: (int(x), int(y)) for name, x, y in
                re.findall(r"x32 desktop selector ROW (\S+) (\d+) (\d+)\r?\n", frame)}
        path = re.search(r"x32 desktop selector PATH (.*?)\r?\n", frame).group(1)
        name = re.search(r"x32 desktop selector NAME (.*?)\r?\n", frame).group(1)
        return {"controls": controls, "rows": rows, "path": path, "name": name}

    def selector_click(self, point):
        count = self.text().count("x32 desktop selector WAIT") + 1
        self.move(*point)
        self.click()
        return self.selector(count)

    def cancel_selector(self, frame):
        count = self.text().count("x32 desktop form PASS") + 1
        self.move(*frame["controls"][8])
        self.click()
        self.wait("x32 desktop form PASS", count)
        time.sleep(0.3)

    def select_inf(self):
        count = self.text().count("x32 desktop selector WAIT") + 1
        text = self.text()
        records = re.findall(r"x32 desktop readinf TARGET (\d+) (\d+) (\d+) (\d+)\r?\n", text)
        item_x, item_y, title_x, title_y = map(int, records[-1])
        self.move(title_x, title_y)
        time.sleep(0.25)
        self.move(item_x, item_y)
        self.click()
        return self.selector(count)

    def close(self):
        if self.process.poll() is None:
            try:
                self.screenshot("final")
            except (OSError, AssertionError):
                pass
            self.process.terminate()
            self.process.wait(timeout=5)
        if self.connection:
            self.connection.close()
        stderr = self.process.stderr.read().decode(errors="replace")
        (self.output / "qemu.log").write_text(stderr)
        self.process.stderr.close()
        self.temporary.cleanup()


def desktop(guest):
    guest.ready(1)
    guest.wait("x32 desktop CPL3 PASS")
    guest.wait("x32 desktop accessory READY")
    guest.screenshot("desktop")
    guest.move(38, 8)
    time.sleep(0.25)
    guest.screenshot("desk-menu")
    guest.move(70, 30)
    guest.click()
    guest.wait("x32 desktop form READY")
    guest.screenshot("about")
    guest.key("ret")
    guest.wait("x32 desktop form PASS")
    time.sleep(0.3)
    guest.move(38, 8)
    time.sleep(0.25)
    guest.move(70, 59)
    guest.click()
    guest.wait("x32 desktop accessory OPEN")
    guest.screenshot("accessory-menu-open")
    guest.move(858, 508)
    guest.click()
    guest.wait("x32 desktop accessory window CLOSED")
    guest.key("alt-c")
    text = guest.wait("x32 desktop window OPEN")
    text = guest.wait(r"x32 desktop IDLE \d+ \d+\r?\n", after=text.rfind("x32 desktop window OPEN"), regex=True)
    coordinates = re.findall(r"x32 desktop IDLE (\d+) (\d+)\r?\n", text)
    guest.x, guest.y = map(int, coordinates[-1])
    time.sleep(0.3)
    guest.screenshot("icons")
    guest.move(135, 8)
    time.sleep(0.25)
    guest.screenshot("view-menu")
    guest.move(135, 44)
    guest.click()
    text = guest.wait(r"x32 desktop program TARGET \d+ \d+\r?\n", regex=True)
    guest.wait("x32 desktop USERDEF PASS")
    guest.screenshot("text")
    target = re.search(r"x32 desktop program TARGET (\d+) (\d+)\r?\n", text)
    time.sleep(0.4)
    idle = guest.text().count("x32 desktop IDLE")
    guest.move(*map(int, target.groups()))
    guest.click()
    guest.wait("x32 desktop IDLE", idle + 1)
    guest.key("ctrl-o")
    guest.wait("x32 desktop application and accessory PASS")
    guest.wait("x32 desktop rejected blocks and shell copies PASS")
    guest.wait("x32 desktop nested ownership PASS")
    guest.ready(2)
    guest.screenshot("application-return")
    # Exercise the user-side file selector, including cancellation and the
    # editable filename copied back from the owned kernel snapshot.
    frame = guest.select_inf()
    guest.screenshot("selector")
    guest.cancel_selector(frame)
    frame = guest.select_inf()
    assert frame["path"] == "C:\\*.INF" and "INFS" in frame["rows"]
    frame = guest.selector_click(frame["rows"]["INFS"])
    assert frame["path"] == "C:\\INFS\\*.INF"
    assert "SUB" in frame["rows"] and "NOISE.TXT" not in frame["rows"]
    first_page = set(frame["rows"])
    selected_name = sorted(name for name in first_page if name.endswith(".INF"))[0]
    frame = guest.selector_click(frame["controls"][6])  # Next page
    second_page = set(frame["rows"])
    assert not first_page.intersection(second_page)
    assert first_page.union(second_page) == {"SUB"}.union("FILE%02d.INF" % number for number in range(14))
    guest.screenshot("selector-next-page")
    frame = guest.selector_click(frame["controls"][5])  # Previous page
    assert set(frame["rows"]) == first_page
    frame = guest.selector_click(frame["rows"]["SUB"])
    assert frame["path"] == "C:\\INFS\\SUB\\*.INF" and not frame["rows"]
    frame = guest.selector_click(frame["controls"][4])  # Parent
    assert frame["path"] == "C:\\INFS\\*.INF"
    frame = guest.selector_click(frame["rows"][selected_name])
    assert frame["name"] == selected_name
    guest.screenshot("selector-selected-row")
    guest.cancel_selector(frame)
    frame = guest.select_inf()
    assert frame["path"] == "C:\\*.INF"
    frame = guest.selector_click(frame["rows"]["TYPED.INF"])
    assert frame["name"] == "TYPED.INF"
    guest.key("esc")
    for key in ("e", "d", "i", "t", "e", "d", "dot", "i", "n", "f"):
        guest.key(key)
    guest.screenshot("selector-edited-filename")
    guest.key("ret")
    guest.wait(r"x32 desktop selector ACCEPT C:\\(?i:edited\.inf)\r?\n", regex=True)
    guest.ready(3)
    guest.screenshot("read-inf-return")
    frame = guest.select_inf()
    guest.move(*frame["rows"]["TYPED.INF"])
    guest.double_click()
    guest.wait("x32 desktop selector ACCEPT C:\\TYPED.INF")
    guest.ready(4)
    guest.screenshot("selector-double-click-return")
    # The console transition drops the graphical association. Returning to
    # AES loads a fresh ring-3 desktop with the saved shell context.
    guest.key("ctrl-z")
    guest.wait("C:\\>")
    for key in ("e", "x", "i", "t", "ret"):
        guest.key(key)
    guest.ready(5)
    guest.screenshot("console-return")
    text = guest.text()
    assert text.count("x32 desktop CPL3 PASS") == 3, text[-3000:]
    assert text.count("x32 desktop accessory READY") == 1, text[-3000:]
    assert text.count("x32 desktop accessory CLOSE") >= 3, text[-3000:]
    print("External x32 desktop, dialogs, edited file selector, USERDEF, nested ownership, application/accessory and console return: PASS", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--ovmf-code", default=firmware("code"))
    parser.add_argument("--ovmf-vars", default=firmware("vars"))
    parser.add_argument("--output", type=Path, default=Path("obj/x32-desktop-runtime"))
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--mode", choices=("desktop", "missing", "both"), default="both")
    args = parser.parse_args()
    if not args.ovmf_code or not args.ovmf_vars:
        parser.error("supply --ovmf-code and --ovmf-vars for this host")
    modes = ("desktop", "missing") if args.mode == "both" else (args.mode,)
    for mode in modes:
        guest = Guest(args.output / mode, args, missing=mode == "missing")
        try:
            if mode == "desktop":
                desktop(guest)
            else:
                guest.wait("C:\\>", timeout=25)
                assert "x32 desktop CPL3 PASS" not in guest.text()
                print("Missing external desktop falls back to EmuCON: PASS", flush=True)
        finally:
            guest.close()


if __name__ == "__main__":
    main()
