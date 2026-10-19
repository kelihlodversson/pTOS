"""Inspect real packed imports, generate malformed fixtures and test refusals."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

root = Path(sys.argv[1])


def word(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def inspect(path):
    data = bytearray(path.read_bytes())
    assert data[:6] == b'\x7fELF\x01\x01'
    assert word(data, 32) == 0, 'fixture must have no section headers'
    phoff = word(data, 28)
    phsize, phnum = struct.unpack_from('<HH', data, 42)
    imports = []
    loads = []
    for i in range(phnum):
        ph = struct.unpack_from('<8I', data, phoff + phsize * i)
        if ph[0] == 0x60000002:
            imports.append(ph[1])
        if ph[0] == 1:
            loads.append(ph)
    assert len(imports) == 1
    offset = imports[0]
    magic, version, reserved, count, binds, itab, btab, strings, size = struct.unpack_from('<IHH6I', data, offset)
    assert (magic, version, reserved, count, binds) == (0x50544c32, 1, 0, 1, 1)
    assert data[offset + strings:offset + strings + size] == b'vdi:vdi\0'
    assert struct.unpack_from('<HHB', data, offset + itab + 4) == (1, 0, 0)
    slot = word(data, offset + btab + 4)
    segment = next(p for p in loads if p[2] <= slot and slot + 8 <= p[2] + p[4])
    assert segment[6] & 2, 'GOT must belong to application data'
    assert word(data, segment[1] + slot - segment[2] + 4) == 0, 'x32 GOT upper half'
    return data, offset, itab, btab, strings


for name in ('VDIIMP.TOS', 'VDIREL.TOS', 'VDIPLT.TOS'):
    inspect(root / name)
data, offset, itab, btab, strings = inspect(root / 'VDIIMP.TOS')
for i, (at, value, fmt) in enumerate((
    (offset + itab + 4, 2, '<H'),       # wrong ABI major
    (offset + itab + 6, 1, '<H'),       # unavailable minor
    (offset + itab + 8, 1, '<B'),       # data kind for a function
    (offset + btab + 8, 1, '<B'),       # data binding for a function
    (offset + btab + 4, 0xffffffff, '<I'),  # invalid slot
), 1):
    bad = data.copy()
    struct.pack_into(fmt, bad, at, value)
    (root / ('VDIBAD%d.TOS' % i)).write_bytes(bad)
for i, name in enumerate((b'aes:vdi\0', b'vdi:bad\0'), 6):
    bad = data.copy()
    bad[offset + strings:offset + strings + 8] = name
    (root / ('VDIBAD%d.TOS' % i)).write_bytes(bad)

# A real ELF dynamic reference must not be silently packaged under a
# misspelled symbol or another library's namespace.
source = (root / 'vdi-import.elf').read_bytes()
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'bad.elf'
    out = Path(directory) / 'bad.tos'
    for old, new, expected in ((b'\0vdi\0', b'\0bad\0', 'unsupported VDI symbol'),
                               (b'libptos-vdi.so.1', b'libptos-aes.so.1', 'needs')):
        assert old in source
        path.write_bytes(source.replace(old, new))
        result = subprocess.run(['./ptos-elf-pack', str(path), str(out)], capture_output=True, text=True)
        assert result.returncode != 0 and expected in result.stderr, result.stderr
        assert not out.exists(), 'refusal must precede writing output'
print('x32 VDI import packaging: PASS')
