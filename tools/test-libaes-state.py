#!/usr/bin/env python3
"""Exercise the AES ELF mutable-state guard using actual ELF sections."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("aes_elf", ROOT / "tools/check-libaes-elf.py")
aes_elf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(aes_elf)
with tempfile.TemporaryDirectory(prefix="libaes-state-") as temp:
    work = Path(temp)
    for name, source, reject in (
        ("readonly", 'static const char text[]="ok"; const char *const pointers[]={text}; int probe(void){return pointers[0][0];}', False),
        ("unsealed", 'const char *const pointers[]={"ok"}; int probe(void){return pointers[0][0];}', True),
        ("bss", 'static int state; int probe(void){return ++state;}', True),
        ("data", 'int state=7; int probe(void){return state++;}', True),
        ("tls", '__thread int state; int probe(void){return ++state;}', True),
    ):
        path = work / (name + ".c")
        path.write_text(source)
        library = work / (name + ".so")
        subprocess.run(shlex.split(os.environ.get("HOSTCC", "cc")) + ["-fPIC", "-shared", "-nostdlib", "-Wl,-z,relro,-z,now" if name != "unsealed" else "-Wl,-z,norelro,-z,now", str(path), "-o", str(library)], check=True)
        try:
            aes_elf.check_mutable_state(library)
        except AssertionError:
            assert reject, name
        else:
            assert not reject, name
print("AES helper state: read-only relocation tables accepted; data/BSS/TLS rejected: PASS")
