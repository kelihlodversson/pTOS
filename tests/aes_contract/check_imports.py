"""Check mixed AES/VDI imports and generate invalid loader fixtures."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

root = Path(sys.argv[1])


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def inspect(path):
    data = bytearray(path.read_bytes())
    assert data[:6] == b'\x7fELF\x01\x01' and u32(data, 32) == 0
    phoff = u32(data, 28)
    phsize, phnum = struct.unpack_from('<HH', data, 42)
    headers = [struct.unpack_from('<8I', data, phoff + i * phsize) for i in range(phnum)]
    imports = [p for p in headers if p[0] == 0x60000002]
    assert len(imports) == 1
    offset = imports[0][1]
    magic, version, reserved, count, binds, itab, btab, strings, size = struct.unpack_from('<IHH6I', data, offset)
    assert (magic, version, reserved) == (0x50544c32, 1, 0)
    assert count == 15 and binds >= count, (count, binds)
    names = set()
    for i in range(count):
        name, major, minor, kind = struct.unpack_from('<IHHB', data, offset + itab + 12 * i)
        assert (major, minor, kind) == (1, 0, 0)
        names.add(bytes(data[offset + strings + name:offset + strings + size]).split(b'\0')[0])
    assert b'vdi:vdi' in names
    assert len([n for n in names if n.startswith(b'aes:')]) == 14
    return data, offset, itab, btab, strings


for name in ('AESIMP.TOS', 'AESREL.TOS'):
    inspect(root / name)
data, offset, itab, btab, strings = inspect(root / 'AESIMP.TOS')
for i, (at, value, fmt) in enumerate((
    (offset + itab + 4, 2, '<H'),
    (offset + itab + 6, 1, '<H'),
    (offset + itab + 8, 1, '<B'),
    (offset + btab + 8, 1, '<B'),
    (offset + btab + 4, 0xffffffff, '<I'),
    (offset + strings, ord('z'), '<B'),
    (offset + strings + 4, ord('z'), '<B'),
), 1):
    bad = data.copy()
    struct.pack_into(fmt, bad, at, value)
    (root / ('AESBAD%d.TOS' % i)).write_bytes(bad)

source = (root / 'aes-import.elf').read_bytes()
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'bad.elf'
    output = Path(directory) / 'bad.tos'
    for old, new, expected in (
        (b'libptos-aes.so.1', b'libptos-vdi.so.1', 'duplicate'),
        (b'\0aes_appl_exit\0', b'\0aes_appl_xxxx\0', 'unsupported'),
    ):
        assert old in source
        path.write_bytes(source.replace(old, new))
        result = subprocess.run(['./ptos-elf-pack', str(path), str(output)], capture_output=True, text=True)
        assert result.returncode != 0 and expected in result.stderr, result.stderr
        assert not output.exists()
print('x32 AES/VDI import packaging: PASS')
