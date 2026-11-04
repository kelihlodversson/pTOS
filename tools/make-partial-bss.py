#!/usr/bin/env python3
"""Make the runtime test DSO contain a read-only partial-page BSS segment."""
from pathlib import Path
import struct
import sys

path = Path(sys.argv[1])
image = bytearray(path.read_bytes())
if not (image[:4] == b'\x7fELF' and image[4] == 1 and image[5] in (1, 2)):
    raise AssertionError('not an ELF32 object of a known byte order')    # (not an assert statement: python -O would skip it)
end = '<' if image[5] == 1 else '>'      # the byte order of the object (EI_DATA)
phoff = struct.unpack_from(end + 'I', image, 28)[0]
phnum = struct.unpack_from(end + 'H', image, 44)[0]
for i in range(phnum):
    at = phoff + 32 * i
    kind, offset, va, _, filesz, memsz, flags, _ = struct.unpack_from(end + '8I', image, at)
    if kind == 1 and va == 0x20000:
        assert filesz == memsz == 32 and flags == 4
        assert image[offset:offset + 4] == bytes([0xa5, 0xff, 0xff, 0xff])
        struct.pack_into(end + 'I', image, at + 16, 1)
        break
else:
    raise AssertionError('missing read-only partial-BSS test segment')
path.write_bytes(image)
