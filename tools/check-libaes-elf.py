#!/usr/bin/env python3
"""Verify that native AES implementations and source bindings have separate owners."""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def symbols(path, dynamic=False):
    command = ["readelf", "--wide", "--dyn-syms" if dynamic else "--syms", str(path)]
    result = {}
    for line in subprocess.check_output(command, text=True).splitlines():
        fields = line.split()
        if len(fields) >= 8 and fields[0].rstrip(":").isdigit():
            name = fields[7].split("@")[0]
            result[name] = {"size": int(fields[2]), "type": fields[3],
                            "bind": fields[4], "section": fields[6]}
    return result


def check(library, archive, clients):
    expected = set(re.findall(r"\bmt_\w+\b", (ROOT / "lib/aes/exports.map").read_text()))
    dynamic = symbols(library, True)
    public = {name for name, value in dynamic.items()
              if value["section"] != "UND" and value["bind"] in {"GLOBAL", "WEAK"}}
    assert public == expected, ("libaes export ownership", public - expected, expected - public)
    state = {"aes_global", "gl_apid", "gl_ap_version"}
    assert not state & dynamic.keys(), "libaes must neither define nor import binding data"
    definitions = set()
    for line in subprocess.check_output(["nm", "-g", "--defined-only", str(archive)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3:
            definitions.add(fields[2])
    bindings = {name[3:] for name in expected} | state | {"inside", "rc_constrain", "rc_equal", "rc_intersect", "rc_union"}
    assert definitions == bindings, ("GEMlib archive ownership", definitions - bindings, bindings - definitions)
    for client in clients:
        defined = symbols(client)
        for name in state:
            assert name in defined and defined[name]["section"] != "UND", (client, "missing caller-owned binding data", name)
        assert defined["aes_global"]["size"] == 32, (client, "x32 AESGLOBAL layout")
        imported = {name for name, value in symbols(client, True).items() if value["section"] == "UND"}
        assert not imported & bindings, (client, "classic binding import", imported & bindings)
    print("Native AES ELF ownership: mt_* in libaes; bindings/data/helpers in GEMlib and clients: PASS")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit("usage: check-libaes-elf.py LIBAES.SO LIBGEM.A [X32-GEM-CLIENT ...]")
    check(Path(sys.argv[1]), Path(sys.argv[2]), [Path(path) for path in sys.argv[3:]])
