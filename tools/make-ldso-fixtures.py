#!/usr/bin/env python3
"""Build runtime rejection fixtures from the linked x32 ELF files."""
from pathlib import Path
import struct
import sys

root = Path(sys.argv[1])
app = (root / 'test/soapp.elf').read_bytes()
assert b'libdyn.so\0' in app and b'startup_log\0' in app
(root / 'test/SOMISS.TOS').write_bytes(app.replace(b'libdyn.so\0', b'absent.so\0'))
(root / 'test/SOSYM.TOS').write_bytes(app.replace(b'startup_log\0', b'missing_log\0'))
(root / 'test/SOBOOT.TOS').write_bytes(app.replace(b'/LD.SO\0', b'/BD.SO\0'))
image = bytearray((root / 'ld.so').read_bytes())
phoff = struct.unpack_from('<I', image, 28)[0]
phnum = struct.unpack_from('<H', image, 44)[0]
loads = []
dynamic = None
for i in range(phnum):
    kind, offset, va, _, size, _, _, _ = struct.unpack_from('<8I', image, phoff + i * 32)
    if kind == 1:
        loads.append((va, offset, size))
    if kind == 2:
        dynamic = (offset, size)
assert dynamic
rela = None
for at in range(dynamic[0], sum(dynamic), 8):
    tag, value = struct.unpack_from('<2I', image, at)
    if tag == 7:
        rela = value
assert rela is not None
for va, offset, size in loads:
    if va <= rela < va + size:
        # Symbol-bearing absolute relocation is unsupported during bootstrap.
        struct.pack_into('<I', image, offset + rela - va + 4, 0x101)
        break
else:
    raise AssertionError('relocation table is not file backed')
(root / 'test/BD.SO').write_bytes(image)
