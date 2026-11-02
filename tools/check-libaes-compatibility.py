#!/usr/bin/env python3
"""Keep the external libaes API matrix distinct from the legacy AES inventory."""
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def check(root=ROOT):
    matrix = (root / "doc/libaes-compatibility.md").read_text()
    export_map = (root / "lib/aes/exports.map").read_text()
    exports = set(re.findall(r"\b\w+\b", export_map.split("global:")[1].split("local:")[0]))
    # Standard binding globals and the explicitly transitional CUIS interface
    # are documented separately from GEM function bindings.
    state = {"aes_global", "gl_apid", "gl_ap_version"}
    assert state <= exports, "missing standard binding globals: " + ", ".join(sorted(state - exports))
    functions = {name for name in exports if name not in state and not name.startswith("aes_")}
    declarations = (root / "include/gem.h").read_text()
    helpers = (root / "include/rectfunc.h").read_text()
    rows = {}
    for line in matrix.splitlines():
        if not line.startswith("`") or " | " not in line:
            continue
        cells = line.split(" | ")
        assert len(cells) == 4, "API row must have four columns: " + line
        names, status, semantics, evidence = cells
        assert status in {"available", "partial", "unavailable", "helper"}, status
        assert semantics.strip() and evidence.strip(), "missing semantics/evidence"
        paths = re.findall(r"`([^`]+)`", evidence)
        assert paths, "missing evidence paths: " + names
        for path in paths:
            assert (root / path).is_file(), "missing evidence: " + path
        if status != "unavailable":
            assert any(path.startswith("tests/") for path in paths), "missing test evidence: " + names
        for name in re.findall(r"`(\w+)`", names):
            assert name not in rows, "duplicate API: " + name
            rows[name] = status
    assert rows, "empty API matrix"
    expected = set()
    for name, status in rows.items():
        if status == "unavailable":
            assert name not in exports and "mt_" + name not in exports, "update support status: " + name
            continue
        bindings = [name] if status == "helper" else [name, "mt_" + name]
        for binding in bindings:
            expected.add(binding)
            header = helpers if status == "helper" else declarations
            assert re.search(r"\b" + re.escape(binding) + r"\s*\(", header), "missing public declaration: " + binding
    assert functions == expected, {
        "undocumented_exports": sorted(functions - expected),
        "missing_exports": sorted(expected - functions),
    }
    operations = set(re.findall(r"case ([A-Z][A-Z0-9_]+):", (root / "aes/gemsuper.c").read_text()))
    aliases = {"GRAF_RUBBOX": "graf_rubberbox"}
    missing = {opcode for opcode in operations if aliases.get(opcode, opcode.lower()) not in rows}
    assert not missing, "unclassified legacy operations: " + ", ".join(sorted(missing))
    print("libaes compatibility: %d GEM/helper exports and %d legacy operations classified" %
          (len(functions), len(operations)))


if __name__ == "__main__":
    check()
