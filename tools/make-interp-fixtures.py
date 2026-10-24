#!/usr/bin/env python3
"""Derive PT_INTERP rejection fixtures from real x32 linker output."""
import pathlib
import struct
import sys

root = pathlib.Path(sys.argv[1])
program = (root / "dyntest.elf").read_bytes()
probe = (root / "probe.so").read_bytes()
phoff = struct.unpack_from("<I", program, 28)[0]
phnum = struct.unpack_from("<H", program, 44)[0]
headers = [(phoff + i * 32, struct.unpack_from("<8I", program, phoff + i * 32))
           for i in range(phnum)]
interp_at, interp = next((at, ph) for at, ph in headers if ph[0] == 3)
for output, library in (("DYNMISS.TOS", b"NOPE1.SO"), ("DYNBAD.TOS", b"BADPR.SO"),
                        ("DYNNEST.TOS", b"NESTD.SO")):
    data = bytearray(program)
    name = b"/GEMSYS/LIB/X32/" + library + b"\0"
    assert len(name) == interp[4]
    data[interp[1]:interp[1] + interp[4]] = name
    (root / output).write_bytes(data)
data = bytearray(program)
note_at = next(at for at, ph in headers if ph[0] == 4)
data[note_at:note_at + 32] = program[interp_at:interp_at + 32]
(root / "DYNDUPE.TOS").write_bytes(data)
data = bytearray(program)
writable = next(ph for at, ph in headers if ph[0] == 1 and ph[6] & 2)
struct.pack_into("<I", data, 24, writable[2])
(root / "DYNNX.TOS").write_bytes(data)
(root / "DYNTRNC.TOS").write_bytes(program[:2000])
data = bytearray(probe)
struct.pack_into("<H", data, 18, 40)  # Wrong machine in the interpreter.
(root / "BADPR.SO").write_bytes(data)
data = bytearray(program)
struct.pack_into("<H", data, 16, 3)   # ET_DYN with a nested PT_INTERP.
(root / "NESTD.SO").write_bytes(data)

# Static ELF dispatch must retain the static loader's header-count policy.
data = bytearray(pathlib.Path('x32hello.elf').read_bytes())
oldoff = struct.unpack_from('<I', data, 28)[0]
oldnum = struct.unpack_from('<H', data, 44)[0]
newph = data[oldoff:oldoff + oldnum * 32] + bytes((129 - oldnum) * 32)
struct.pack_into('<I', data, 28, len(data))
struct.pack_into('<H', data, 44, 129)
data.extend(newph)
(root / 'STATICPH.TOS').write_bytes(data)
