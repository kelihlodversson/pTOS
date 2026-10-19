"""Validate the loaded-accessory boot, including the live desktop session."""
from pathlib import Path
import sys

output = Path(sys.argv[1]).read_text(errors="replace")
assert output.count("EFI entry reached") == 1, "reset/triple-fault loop"
for forbidden in ("panic:", "accessory FAIL", "memtest: FAIL", "System halted!"):
    assert forbidden not in output, forbidden
for marker in (
    "x86-64 memtest: PASS",
    "x32 accessory preparation failures: PASS",
    "x32 accessory refused load: PASS",
    "x32 accessory restart 1: PASS",
    "x32 accessory restart 2: PASS",
    "x32 accessory restart 3: PASS",
    "x32 accessory lifecycle: PASS",
):
    assert marker in output, marker
live = output.rsplit("x32 accessory lifecycle: PASS", 1)[1]
for name in ("ACCONE", "ACCTWO", "ACCFAULT", "ACCWAIT"):
    assert "accessory " + name + ": PASS" in live, name + " in final session"
assert "x32 accessory message/exit/fault: PASS" in live
assert "x32 accessory desktop: PASS" in live
for marker in ("native queue backpressure", "native window pointers", "idle wake"):
    assert "x32 accessory " + marker + ": PASS" in live, marker
# Only the deliberate #UD fault is expected after the main process tests.
for line in output.rsplit("x86-64 memtest: PASS", 1)[1].splitlines():
    if "process fault:" in line:
        assert "#UD Invalid Opcode" in line, line
print("Loaded x32 accessory preparation, startup, message/resume, exit/fault and restart: PASS")
