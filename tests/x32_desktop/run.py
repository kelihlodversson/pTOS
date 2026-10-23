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

    def wait(self, marker, count=1, timeout=15):
        deadline = min(self.deadline, time.monotonic() + timeout)
        while time.monotonic() < deadline:
            text = self.text()
            if text.count(marker) >= count:
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
        self.command("mouse_move %d %d" % (x - self.x, y - self.y))
        self.x, self.y = x, y

    def click(self):
        self.command("mouse_button 1")
        self.command("mouse_button 0")

    def key(self, key):
        self.command("sendkey " + key)

    def screenshot(self, name):
        self.command("screendump " + str(self.output / (name + ".ppm")))

    def ready(self, count):
        text = self.wait("x32 desktop READY", count)
        records = re.findall(r"x32 desktop READY (\d+) (\d+) (\d+) (\d+)\r?\n", text)
        assert len(records) >= count, "partial READY record"
        self.x, self.y, width, height = map(int, records[count - 1])
        assert width >= 640 and height >= 400
        return text

    def select_inf(self, count):
        text = self.text()
        records = re.findall(r"x32 desktop readinf TARGET (\d+) (\d+) (\d+) (\d+)\r?\n", text)
        item_x, item_y, title_x, title_y = map(int, records[-1])
        self.move(title_x, title_y)
        time.sleep(0.25)
        self.move(item_x, item_y)
        self.click()
        text = self.wait("x32 desktop selector READY", count)
        records = re.findall(r"x32 desktop selector READY (\d+) (\d+)\r?\n", text)
        assert len(records) >= count
        return tuple(map(int, records[count - 1]))

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
    guest.key("alt-c")
    guest.wait("x32 desktop window OPEN")
    time.sleep(0.3)
    guest.screenshot("icons")
    guest.move(135, 8)
    time.sleep(0.25)
    guest.move(135, 44)
    guest.click()
    text = guest.wait("x32 desktop program TARGET")
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
    cancel = guest.select_inf(1)
    guest.screenshot("selector")
    guest.move(*cancel)
    guest.click()
    guest.wait("x32 desktop form PASS", 2)
    time.sleep(0.3)
    guest.select_inf(2)
    guest.key("esc")
    for key in ("t", "y", "p", "e", "d", "dot", "i", "n", "f"):
        guest.key(key)
    guest.key("ret")
    guest.wait("x32 desktop form PASS", 3)
    guest.ready(3)
    guest.screenshot("read-inf-return")
    # The console transition drops the graphical association. Returning to
    # AES loads a fresh ring-3 desktop with the saved shell context.
    guest.key("ctrl-z")
    guest.wait("C:\\>")
    for key in ("e", "x", "i", "t", "ret"):
        guest.key(key)
    guest.ready(4)
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
    parser.add_argument("--timeout", type=float, default=90)
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
