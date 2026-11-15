#!/usr/bin/env python3
"""Keep the external libaes API matrix distinct from the legacy AES inventory."""
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def c_code(text):
    # C removes escaped newlines before recognizing comments. Tokenize quoted
    # literals too, so a comment marker in a string cannot end/start a comment
    # and a prototype quoted in a string cannot count as a declaration.
    text = re.sub(r"\\\r?\n", "", text)
    tokens = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*'
    return re.sub(tokens, lambda match: " " + "\n" * match[0].count("\n"), text, flags=re.S)


def check(root=ROOT):
    matrix = (root / "doc/libaes-compatibility.md").read_text()
    export_map = c_code((root / "lib/aes/exports.map").read_text())
    exports = set(re.findall(r"\b\w+\b", export_map.split("global:")[1].split("local:")[0]))
    state = {"aes_global", "gl_apid", "gl_ap_version"}
    private = {name for name in exports if not name.startswith("mt_")}
    assert not private, "non-mt libaes exports: " + ", ".join(sorted(private))
    functions = exports
    declarations = c_code((root / "include/libaes.h").read_text())
    binding_header = c_code((root / "lib/gemlib/ptos/gem.h").read_text())
    binding_code = c_code("\n".join(path.read_text() for path in sorted((root / "lib/gemlib/ptos").glob("*.c"))))
    helper_code = binding_code
    helpers = c_code((root / "include/rectfunc.h").read_text())
    for name in state:
        assert re.search(r"\b" + name + r"\s*(?:;|,)", binding_header), "missing GEMlib binding declaration: " + name
        assert re.search(r"\b" + name + r"\s*(?:=|;)", binding_code), "missing GEMlib binding definition: " + name
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
        if status != "helper":
            expected.add("mt_" + name)
            assert re.search(r"\bmt_" + re.escape(name) + r"\s*\(", declarations), "missing public declaration: mt_" + name
        header = helpers if status == "helper" else binding_header
        code = helper_code if status == "helper" else binding_code
        assert re.search(r"\b" + re.escape(name) + r"\s*\(", header), "missing public declaration: " + name
        assert re.search(r"\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{", code), "missing GEMlib definition: " + name
    assert functions == expected, {
        "undocumented_exports": sorted(functions - expected),
        "missing_exports": sorted(expected - functions),
    }
    dispatcher = c_code((root / "aes/gemsuper.c").read_text())
    operations = set(re.findall(r"\bcase\s+([A-Z][A-Z0-9_]*)\s*:", dispatcher))
    aliases = {"GRAF_RUBBOX": "graf_rubberbox"}
    missing = {opcode for opcode in operations if aliases.get(opcode, opcode.lower()) not in rows}
    assert not missing, "unclassified legacy operations: " + ", ".join(sorted(missing))
    print("libaes compatibility: %d mt_* exports and %d legacy operations classified" %
          (len(functions), len(operations)))


if __name__ == "__main__":
    check()
