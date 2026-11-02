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
        self.embedded_console = args.embedded_console
        self.output.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="ptos-desktop-", dir="/tmp")
        self.root = Path(self.temporary.name)
        esp = self.root / "esp"
        (esp / "EFI" / "BOOT").mkdir(parents=True)
        shutil.copy("pc-x86_64.efi", esp / "EFI" / "BOOT" / "BOOTX64.EFI")
        if not args.embedded_console:
            shutil.copy("cli/emucon.tos", esp / "EMUCON.TOS")
        if not missing:
            for source, target in (("emudesk.prg", "EMUDESK.PRG"),
                                   ("obj/x32/GEMTEST.PRG", "APPTEST.PRG"),
                                   ("obj/x32/DESKACC.ACC", "DESKACC.ACC"),
                                   ("desk/emudesk-raspi.inf", "TYPED.INF")):
                shutil.copy(source, esp / target)
            shutil.copy("desk/emudesk-raspi.inf", esp / "EDITED.INF")
            if "CONF_WITH_LIBAES = y" in Path("obj/auto.conf").read_text():
                shutil.copy("obj/libaes/DESKACC.ACC",esp / "DESKACC.ACC")
                shutil.copy("obj/libaes/DESKGEM.PRG",esp / "APPTEST.PRG")
                shutil.copy("obj/libaes/DESKGEM.PRG",esp / "APPTEST.GTP")
                shutil.copy("obj/libaes/DESKCON.TOS",esp / "APPTEST.TOS")
                shutil.copy("obj/libaes/DESKCON.TOS",esp / "APPTEST.TTP")
                shutil.copy("extras/emuicon0.rsc",esp / "EMUICON.RSC")
            deep = esp
            for level in range(1, 14):
                deep = deep / "LONGDIR0"
                deep.mkdir()
                shutil.copy("desk/emudesk-raspi.inf", deep / "LONGNAME.INF")
                shutil.copy("desk/emudesk-raspi.inf", deep / ("DEPTH%02d.INF" % level))
            files = esp / "INFS"
            (files / "SUB").mkdir(parents=True)
            for number in reversed(range(14)):
                shutil.copy("desk/emudesk-raspi.inf", files / ("FILE%02d.INF" % number))
            (files / "NOISE.TXT").write_text("This must not appear in an INF listing.\n")
        shared_runtime = "CONF_WITH_ELF_SHARED = y" in Path("obj/auto.conf").read_text()
        if shared_runtime:
            shutil.copytree("obj/ldso/runtime/GEMSYS", esp / "GEMSYS", dirs_exist_ok=True)
        second = self.root / "second"
        second.mkdir()
        shutil.copy("desk/emudesk-raspi.inf", second / "SECOND.INF")
        variables = self.root / "vars.fd"
        shutil.copy(args.ovmf_vars, variables)
        self.log = self.output / "boot.log"
        self.monitor = self.root / "monitor.sock"
        self.log.write_text("")
        disks = ["format=raw,file=fat:rw:" + str(esp), "format=raw,file=fat:rw:" + str(second)]
        if shared_runtime:
            # File-backed shared pages need a stable FAT identity and chain;
            # use real sectors for the shared-runtime integration run.
            disks = []
            for name, source in (("system", esp), ("second", second)):
                image = self.root / (name + ".img")
                subprocess.run(["tools/mkhdisk.sh", str(image), "33554432"] +
                               [str(path) for path in sorted(source.iterdir())],
                               check=True, stdout=subprocess.DEVNULL)
                disks.append("format=raw,file=" + str(image))
        self.process = subprocess.Popen([
            args.qemu, "-machine", "pc", "-m", "256", "-cpu", "qemu64,+pdpe1gb",
            "-drive", "if=pflash,format=raw,readonly=on,file=" + args.ovmf_code,
            "-drive", "if=pflash,format=raw,file=" + str(variables),
            "-drive", disks[0],
            "-drive", disks[1],
            "-serial", "file:" + str(self.log), "-display", "none",
            "-monitor", "unix:" + str(self.monitor) + ",server=on,wait=off",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.deadline = time.monotonic() + args.timeout
        self.connection = None
        self.x = self.y = 0

    def text(self):
        text = self.log.read_text(errors="replace")
        assert not any(marker in text for marker in ("panic:", " FAIL", "resource snapshot rejected")), text[-5000:]
        faults = re.findall(r"^process fault: (.*)\n", text, re.MULTILINE)
        expected = text.count("x32 desktop teardown fault READY") + text.count("x32 desktop client fault READY") + text.count("x32 console fault READY")
        # The early logger and console both report the same expected #UD.
        assert len(set(faults)) <= expected and len(faults) <= 2 * expected, text[-5000:]
        assert all(line.startswith("#UD Invalid Opcode") for line in faults), text[-5000:]
        assert not re.search(r"VDI \d+ error -?\d+", text), text[-5000:]
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

    def short_click(self):
        # Native TOUCHEXIT repeats while held. Keep a parent click brief,
        # independently of the monitor's response-draining timeout.
        self.connection.sendall(b"mouse_button 1\n")
        time.sleep(0.02)
        self.connection.sendall(b"mouse_button 0\n")
        self.command("", delay=0.2)

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
        # TOUCHEXIT arrows can repeat while held, as in native GEM. Inspect
        # the last complete frame after releasing the button.
        frame = text.rsplit("x32 desktop selector WAIT", 1)[0].rsplit("x32 desktop selector READY ", 1)[1]
        controls = {int(n): (int(x), int(y)) for n, x, y in
                    re.findall(r"x32 desktop selector CONTROL (\d+) (\d+) (\d+)\r?\n", frame)}
        rows = {name: (int(x), int(y)) for name, x, y in
                re.findall(r"x32 desktop selector ROW (\S+) (\d+) (\d+)\r?\n", frame)}
        path = re.search(r"x32 desktop selector PATH (.*?)\r?\n", frame).group(1)
        name = re.search(r"x32 desktop selector NAME (.*?)\r?\n", frame).group(1)
        return {"controls": controls, "rows": rows, "path": path, "name": name}

    def selector_click(self, point, short=False):
        count = self.text().count("x32 desktop selector WAIT") + 1
        self.move(*point)
        if short:
            self.short_click()
        else:
            self.click()
        return self.selector(count)

    def selector_path(self, frame, path, error=False):
        count = self.text().count("x32 desktop selector WAIT") + 1
        alerts = self.text().count("x32 desktop alert READY")
        self.move(*frame["controls"][3])
        self.click()
        self.key("esc")
        keys = {":": "shift-semicolon", "\\": "backslash", "*": "shift-8", ".": "dot"}
        for char in path.lower():
            self.key(keys.get(char, char))
        self.key("ret")
        if error:
            self.wait("x32 desktop alert READY", alerts + 1)
            self.screenshot("selector-path-error")
            self.key("ret")
        return self.selector(count)

    def selector_reject(self, point):
        count = self.text().count("x32 desktop selector WAIT") + 1
        alerts = self.text().count("x32 desktop alert READY") + 1
        self.move(*point)
        self.short_click()
        self.wait("x32 desktop alert READY", alerts)
        self.key("ret")
        return self.selector(count)

    def cancel_selector(self, frame):
        count = self.text().count("x32 desktop form PASS") + 1
        self.move(*frame["controls"][52])
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
    guest.wait("x32 desktop shared icon snapshot PASS")
    guest.wait("x32 desktop window getter retries PASS")
    guest.wait("x32 desktop window top retries PASS")
    guest.wait("x32 desktop accessory READY")
    # Exercise the classic binding's shared argument array through nested
    # cursor calls, in both drag directions on empty desktop background.
    for start, end in (((900, 600), (1000, 650)), ((1000, 650), (900, 600))):
        count = guest.text().count("x32 desktop rubber RESULT") + 1
        gesture = len(guest.text())
        guest.move(*start)
        guest.command("mouse_button 1")
        # The native double-click recognizer may delay the next press.
        # Hold it until the desktop has entered the rubber-band call.
        guest.wait(r"x32 desktop rubber READY %d %d 1\r?\n" % start, after=gesture, regex=True)
        tracking = len(guest.text())
        guest.move(*end)
        guest.wait(r"x32 desktop rubber TRACK %d %d 1\r?\n" % (end[0] - start[0], end[1] - start[1]), after=tracking, regex=True)
        guest.command("mouse_button 0")
        text = guest.wait(r"x32 desktop rubber RESULT -?\d+ -?\d+ -?\d+ -?\d+\r?\n", count, regex=True)
        result = re.findall(r"x32 desktop rubber RESULT (-?\d+) (-?\d+) (-?\d+) (-?\d+)\r?\n", text)[-1]
        assert tuple(map(int, result)) == (*start, end[0] - start[0], end[1] - start[1]), result
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
    if "CONF_WITH_NATIVE_SCHED = y" in Path("obj/auto.conf").read_text():
        guest.wait("x32 desktop unmanaged accessory scheduling PASS")
        guest.wait("x32 desktop managed scheduling restore PASS")
    guest.wait("x32 desktop rejected blocks and shell copies PASS")
    guest.wait("x32 desktop shell tail boundary PASS")
    guest.wait("x32 desktop nested ownership PASS")
    guest.wait("x32 desktop teardown normal READY")
    guest.wait("x32 desktop teardown fault READY")
    guest.wait("process fault:")
    guest.ready(2)
    guest.screenshot("application-return")
    # Exercise the user-side file selector, including cancellation and the
    # editable filename copied back from the owned kernel snapshot.
    frame = guest.select_inf()
    guest.screenshot("selector")
    guest.cancel_selector(frame)
    frame = guest.select_inf()
    assert frame["path"] == "C:\\*.INF" and "INFS" in frame["rows"]
    # The native drive grid retains enabled and disabled drive behavior.
    count = guest.text().count("x32 desktop selector WAIT")
    guest.move(*frame["controls"][7])  # A is absent and disabled.
    guest.click()
    assert guest.text().count("x32 desktop selector WAIT") == count
    frame = guest.selector_click(frame["controls"][10])  # D
    assert frame["path"] == "D:\\*.INF" and list(frame["rows"]) == ["SECOND.INF"]
    frame = guest.selector_click(frame["controls"][9])  # C
    assert frame["path"] == "C:\\*.INF"

    frame = guest.selector_click(frame["rows"]["INFS"])
    assert frame["path"] == "C:\\INFS\\*.INF"
    assert "SUB" in frame["rows"] and "NOISE.TXT" not in frame["rows"]
    first_page = list(frame["rows"])
    assert first_page == ["SUB"] + ["FILE%02d.INF" % n for n in range(8)]
    selected_name = "FILE00.INF"
    # Native scrollbar page down/up, including the overlapping last page.
    bar_x, _ = frame["controls"][39]
    down_y = frame["controls"][38][1] - 16
    frame = guest.selector_click((bar_x, down_y))
    assert list(frame["rows"]) == ["FILE%02d.INF" % n for n in range(5, 14)]
    guest.screenshot("selector-next-page")
    up_y = frame["controls"][37][1] + 16
    frame = guest.selector_click((bar_x, up_y))
    assert list(frame["rows"]) == first_page
    # Drag the original elevator to the bottom and page back to the top.
    count = guest.text().count("x32 desktop selector WAIT") + 1
    guest.move(*frame["controls"][40])
    guest.command("mouse_button 1")
    guest.wait("x32 desktop selector scroll READY")
    guest.move(bar_x, down_y)
    guest.command("mouse_button 0")
    frame = guest.selector(count)
    assert list(frame["rows"])[-1] == "FILE13.INF"
    guest.screenshot("selector-dragged-scrollbar")
    frame = guest.selector_click((bar_x, up_y))
    assert list(frame["rows"]) == first_page
    frame = guest.selector_path(frame, "C:\\INFS\\*.TXT")
    assert list(frame["rows"]) == ["SUB", "NOISE.TXT"]
    frame = guest.selector_path(frame, "C:\\INFS\\*.INF")
    assert list(frame["rows"]) == first_page
    frame = guest.selector_path(frame, "C:\\MISSING\\*.INF", error=True)
    assert frame["path"] == "C:\\INFS\\*.INF" and list(frame["rows"]) == first_page
    frame = guest.selector_click(frame["rows"]["SUB"])
    assert frame["path"] == "C:\\INFS\\SUB\\*.INF" and not frame["rows"]
    frame = guest.selector_click(frame["controls"][34], short=True)  # Native parent gadget
    assert frame["path"] == "C:\\INFS\\*.INF"
    frame = guest.selector_click(frame["rows"][selected_name])
    assert frame["name"] == selected_name
    guest.screenshot("selector-selected-row")
    guest.cancel_selector(frame)
    frame = guest.select_inf()
    for depth in range(12):
        frame = guest.selector_click(frame["rows"]["LONGDIR0"], short=True)
        expected_path = "C:\\" + "LONGDIR0\\" * (depth + 1) + "*.INF"
        # GEM displays only the leading 38 characters of the full path.
        assert frame["path"] == expected_path[:38]
        assert "DEPTH%02d.INF" % (depth + 1) in frame["rows"]
    old_path = frame["path"]
    frame = guest.selector_reject(frame["rows"]["LONGDIR0"])
    assert frame["path"] == old_path and "DEPTH12.INF" in frame["rows"]
    assert "DEPTH13.INF" not in frame["rows"]
    frame = guest.selector_click(frame["rows"]["LONGNAME.INF"])
    assert frame["name"] == "LONGNAME.INF"
    guest.screenshot("selector-path-limit")
    guest.move(*frame["controls"][51])
    guest.click()
    guest.wait("x32 desktop selector ACCEPT C:\\" + "LONGDIR0\\" * 12 + "LONGNAME.INF")
    guest.ready(3)
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
    guest.ready(4)
    guest.screenshot("read-inf-return")
    frame = guest.select_inf()
    guest.move(*frame["rows"]["TYPED.INF"])
    guest.double_click()
    guest.wait("x32 desktop selector ACCEPT C:\\TYPED.INF")
    guest.ready(5)
    guest.screenshot("selector-double-click-return")
    # The console transition drops the graphical association. Returning to
    # AES loads a fresh ring-3 desktop with the saved shell context.
    guest.key("ctrl-z")
    guest.wait("C:\\>")
    for key in ("e", "x", "i", "t", "ret"):
        guest.key(key)
    guest.ready(6)
    guest.screenshot("console-return")
    text = guest.text()
    assert text.count("x32 desktop CPL3 PASS") == 3, text[-3000:]
    assert text.count("x32 desktop accessory READY") == 1, text[-3000:]
    assert text.count("x32 desktop accessory CLOSE") >= 3, text[-3000:]
    print("External x32 desktop, rubber-band selection, bounded shell tails, dialogs, edited file selector, USERDEF, nested ownership, application/accessory and console return: PASS", flush=True)


def libaes_desktop(guest):
    text = guest.ready(1)
    assert "x32 desktop LIBAES PASS" in text
    assert "x32 desktop custom icons PASS" in text
    guest.wait("x32 libaes accessory READY")
    guest.wait("x32 libaes accessory FORGED TERM")

    def accessory_open(count):
        guest.move(38,8)
        time.sleep(0.25)
        guest.move(70,59)
        guest.click()
        guest.wait("x32 libaes accessory OPEN",count)

    accessory_open(1)
    guest.screenshot("resident-accessory")
    guest.move(588,498)
    guest.click()
    guest.wait("x32 libaes accessory CLOSED")
    guest.move(34,45)
    guest.double_click()
    guest.wait("x32 desktop window OPEN")
    guest.move(135,8)
    time.sleep(0.25)
    guest.move(135,44)
    guest.click()
    guest.wait("x32 desktop program TARGET")

    def launch(count):
        time.sleep(0.6)  # Each Open is an independent gesture, beyond dclick time.
        text = guest.text()
        points = re.findall(r"x32 desktop program TARGET (\d+) (\d+)",text)
        # Focus the existing directory window before selecting a file. GEM
        # consumes the activation click when another application's window tops it.
        guest.move(180,105)
        guest.click()
        guest.move(*map(int,points[-1]))
        guest.click()
        guest.key("ctrl-o")
        text = guest.wait(r"x32 desktop client READY \d+ \d+",count,regex=True)
        pairs = re.findall(r"x32 desktop client READY (\d+) (\d+)",text)
        assert text.count("x32 desktop LIBAES PASS") == 1
        return tuple(map(int,pairs[-1]))

    first = launch(1)
    # Launch another process while the first remains blocked for GEM input.
    second = launch(2)
    assert first[0] != second[0] and first[1] != second[1]
    assert "x32 desktop client EXIT" not in guest.text()
    guest.screenshot("two-applications")
    accessory_open(2)  # Same resident process survives other applications.
    for (pid,window),key,status in ((first,"q","0025"),(second,"f","0400")):
        guest.move(750,110+(pid&1)*280)
        guest.click()
        guest.key(key)
        text = guest.wait("x32 desktop CH_EXIT %d %s" % (pid,status))
        assert text.count("x32 desktop LIBAES PASS") == 1
        assert text.count("x32 desktop window OPEN") == 1
    # .GTP is graphical too, with a standard bounded parameter dialog.
    guest.move(180,105)
    guest.click()
    points = re.findall(r"x32 desktop GTP TARGET (\d+) (\d+)",guest.text())
    guest.move(*map(int,points[-1]))
    guest.click()
    forms = guest.text().count("x32 desktop form WAIT")
    guest.key("ctrl-o")
    guest.wait("x32 desktop form WAIT",forms+1)
    for letter in "param": guest.key(letter)
    guest.key("ret")
    text = guest.wait(r"x32 desktop client READY \d+ \d+",3,regex=True)
    third = tuple(map(int,re.findall(r"x32 desktop client READY (\d+) (\d+)",text)[-1]))
    assert "x32 desktop client PARAMETERS PASS %d " % third[0] in text
    guest.move(750,110+(third[0]&1)*280)
    guest.click()
    guest.key("q")
    guest.wait("x32 desktop CH_EXIT %d 0025" % third[0])
    background = launch(4)
    # Full-screen text children do not terminate or restart the desktop.
    for count,kind,key,status in ((1,"TOS","q","0025"),(2,"TOS","f","0400"),(3,"TTP","q","0025")):
        guest.move(180,105)
        guest.click()
        points = re.findall(r"x32 desktop %s TARGET (\d+) (\d+)" % kind,guest.text())
        if not points and kind == "TTP":
            before = len(guest.text())
            guest.move(308,107)  # Full the directory window to reveal TTP.
            guest.click()
            text = guest.wait(r"x32 desktop TTP TARGET \d+ \d+",after=before,regex=True)
            points = re.findall(r"x32 desktop TTP TARGET (\d+) (\d+)",text[before:])
        assert points, "missing console application target"
        guest.move(*map(int,points[-1]))
        guest.click()
        forms = guest.text().count("x32 desktop form WAIT")
        guest.key("ctrl-o")
        if kind == "TTP":
            guest.wait("x32 desktop form WAIT",forms+1)
            for letter in "param": guest.key(letter)
            guest.key("ret")
        text = guest.wait(r"x32 console READY \d+",count,regex=True)
        pid = int(re.findall(r"x32 console READY (\d+)",text)[-1])
        if kind == "TTP": assert "x32 console PARAMETERS PASS %d" % pid in text
        guest.screenshot("console-%d" % count)
        guest.key(key)
        guest.wait("x32 desktop CH_EXIT %d %s" % (pid,status))
        assert guest.text().count("x32 desktop CH_EXIT %d " % pid) == 1
        assert guest.text().count("x32 desktop LIBAES PASS") == 1
        if count == 2:
            # A second GUI client stays registered across both text exits,
            # handles the restored redraw, and still accepts its own input.
            guest.move(750,110+(background[0]&1)*280)
            guest.click()
            guest.key("q")
            guest.wait("x32 desktop CH_EXIT %d 0025" % background[0])
    if not guest.embedded_console:
        before = len(guest.text())
        guest.key("ctrl-z")
        text = guest.wait(r"x32 desktop LAUNCH \d+ (?:C:\\)?EMUCON.TOS",after=before,regex=True)
        pid = int(re.findall(r"x32 desktop LAUNCH (\d+) (?:C:\\)?EMUCON.TOS",text[before:])[-1])
        assert pid > 0
        guest.wait("C:\\>",after=before)
        guest.screenshot("console-shell")
        for key in ("e","x","i","t","ret"): guest.key(key)
        guest.wait("x32 desktop CH_EXIT %d 0000" % pid)
        assert guest.text().count("x32 desktop LIBAES PASS") == 1
    # The original directory window remains usable after all terminations.
    guest.move(180,105)
    guest.click()
    guest.key("alt-c")
    guest.wait("x32 desktop window OPEN",2)
    text = guest.text()
    assert text.count("x32 desktop LIBAES PASS") == 1
    for pid,window in (first,second):
        assert text.count("x32 desktop CH_EXIT %d " % pid) == 1
    assert text.count("x32 libaes accessory READY") == 1
    assert "x32 libaes accessory FAIL" not in text
    assert "x32 libaes accessory EXIT" not in text
    accessory_open(3)
    records = re.findall(r"x32 libaes accessory OPEN (\d+) (\d+)",guest.text())
    assert records[1] == records[2], records  # Window survived every child exit.
    guest.key("q")
    guest.wait("x32 libaes accessory EXIT")
    # A fresh Desk popup must no longer contain the terminated accessory.
    guest.move(38,8)
    time.sleep(0.25)
    guest.screenshot("accessory-removed")
    guest.move(400,80)
    guest.click()
    guest.screenshot("resident-desktop")
    print("Resident libaes desktop, registered accessory lifetime, GEM/console launches and normal/fault return: PASS",flush=True)


def shutdown_desktop(guest):
    text = guest.ready(1)
    assert "x32 desktop LIBAES PASS" in text
    guest.wait("x32 libaes accessory READY")
    guest.wait("x32 libaes accessory FORGED TERM")
    guest.move(38,8)
    time.sleep(0.25)
    guest.move(70,59)
    guest.click()
    guest.wait("x32 libaes accessory OPEN")
    guest.move(34,45)
    guest.click()  # Activate the background before the opening gesture.
    time.sleep(0.6)
    guest.double_click()
    guest.wait("x32 desktop window OPEN")
    guest.move(135,8)
    time.sleep(0.25)
    guest.move(135,44)
    guest.click()
    text = guest.wait(r"x32 desktop program TARGET \d+ \d+",regex=True)
    x,y = map(int,re.findall(r"x32 desktop program TARGET (\d+) (\d+)",text)[-1])
    guest.move(x,y)
    guest.click()
    guest.key("ctrl-o")
    guest.wait("x32 desktop client READY")
    guest.move(180,105)
    guest.click()
    guest.move(85,8)
    time.sleep(0.3)
    guest.screenshot("shutdown-menu")
    guest.move(100,252)
    guest.click()
    deadline = min(guest.deadline,time.monotonic()+20)
    while guest.process.poll() is None and time.monotonic() < deadline:
        time.sleep(0.05)
    assert guest.process.poll() == 0, "desktop did not power off cleanly: " + guest.log.read_text()[-3000:]
    text = guest.log.read_text()
    assert "x32 desktop client EXIT" in text, text[-3000:]
    assert "x32 libaes accessory CLOSE" in text, text[-3000:]
    assert "x32 libaes accessory EXIT" in text, text[-3000:]
    assert "x32 desktop shutdown completed" in text, text[-3000:]
    assert "x32 desktop shutdown poweroff" in text, text[-3000:]
    assert text.index("x32 libaes accessory EXIT") < text.index("x32 desktop shutdown completed")
    assert text.index("x32 desktop client EXIT") < text.index("x32 desktop shutdown completed")
    assert text.count("x32 desktop LIBAES PASS") == 1 and " FAIL" not in text
    assert "panic:" not in text and "process fault:" not in text
    print("Resident desktop waits for GEM application/accessory shutdown before poweroff: PASS",flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--ovmf-code", default=firmware("code"))
    parser.add_argument("--ovmf-vars", default=firmware("vars"))
    parser.add_argument("--output", type=Path, default=Path("obj/x32-desktop-runtime"))
    parser.add_argument("--timeout", type=float, default=300)
    parser.add_argument("--embedded-console", action="store_true",
                        help="omit EMUCON.TOS and require the embedded x32 console")
    parser.add_argument("--mode", choices=("desktop", "missing", "both", "libaes", "shutdown"), default="both")
    args = parser.parse_args()
    if not args.ovmf_code or not args.ovmf_vars:
        parser.error("supply --ovmf-code and --ovmf-vars for this host")
    modes = ("desktop", "missing") if args.mode == "both" else (args.mode,)
    for mode in modes:
        guest = Guest(args.output / mode, args, missing=mode == "missing")
        try:
            if mode == "desktop":
                if "CONF_WITH_LIBAES = y" in Path("obj/auto.conf").read_text():
                    libaes_desktop(guest)
                else:
                    desktop(guest)
            elif mode == "shutdown":
                shutdown_desktop(guest)
            elif mode == "libaes":
                libaes_desktop(guest)
            else:
                text = guest.wait("C:\\>", timeout=25)
                if args.embedded_console:
                    assert "EmuCON: starting the x32 image in ring 3" in text
                assert "x32 desktop CPL3 PASS" not in guest.text()
                print("Missing external desktop falls back to EmuCON: PASS", flush=True)
        finally:
            guest.close()


if __name__ == "__main__":
    main()
