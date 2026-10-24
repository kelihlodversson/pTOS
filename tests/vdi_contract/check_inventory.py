#!/usr/bin/env python3
"""Compare the x32 contract's tags against the actual VDI dispatch tables."""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[2]
source = (root / "vdi/vdi_main.c").read_text()
tables = re.findall(r"jmptb[12]\[\] = \{(.*?)\n\};", source, re.S)
assert len(tables) == 2, "VDI dispatch tables changed; update the inventory check"
operations = {}
for table in tables:
    for handler, number in re.findall(
        r"\{\s*\d+,\s*\d+,\s*(\w+)\s*\}[^\n]*?/\*\s*(\d+)", table
    ):
        operations[int(number)] = handler
assert len(operations) == 74, "VDI operation inventory changed; audit the x32 contract"

# These handlers use control union members. All other implemented handlers
# use the parameter arrays; no-op entries are deliberately rejected.
tags = {
    "vdi_vro_cpyfm": 1, "vdi_vr_trnfm": 1, "vdi_vrt_cpyfm": 1,
    "vdi_vst_load_fonts": 2, "vdi_vex_timv": 3, "vdi_vex_butv": 4,
    "vdi_vex_motv": 5, "vdi_vex_curv": 6, "vdi_vex_wheelv": 7,
}
defs = (root / "vdi/vdi_defs.h").read_text()
assert re.search(r"#define\s+HAVE_BEZIER\s+0\b", defs), "Bezier contract needs an audit"
result = subprocess.run(
    [str(root / "obj/vdi-contract-test"), "inventory"],
    check=True, capture_output=True, text=True,
)
for line in result.stdout.splitlines():
    code, subcode, actual = map(int, line.split())
    handler = operations.get(code)
    if code == 5:
        expected = 0 if 1 <= subcode <= 19 else -1
    elif code == 11:
        expected = 0 if 1 <= subcode <= 10 else -1
    elif code == 102 and subcode == 1:
        expected = 0
    elif not handler or handler == "vdi_v_nop" or subcode:
        expected = -1
    else:
        expected = tags.get(handler, 0)
    assert actual == expected, (code, subcode, handler, actual, expected)
print("VDI contract: layouts, bitmap bounds and complete opcode inventory passed")
