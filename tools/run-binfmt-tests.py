#!/usr/bin/env python3
"""Boot public Pexec fixtures; preserve logs and the raw FAT disk in obj/binfmt."""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import time


def elf_header(arch, foreign=False):
    machine, endian = {"arm": (40, "<"), "x32": (62, "<"), "m68k": (4, ">")}[arch]
    if foreign:
        machine = 3  # i386 is foreign on all three native targets
    ident = b"\x7fELF\x01" + bytes([1 if endian == "<" else 2, 1]) + bytes(9)
    # ET_REL with native class/machine is malformed native input, never a
    # foreign format eligible for the catch-all configured ELF handler.
    return ident + struct.pack(endian + "HHIIIIIHHHHHH", 1, machine, 1, 0, 0, 0,
                               0, 52, 32, 0, 40, 0, 0)


def stage_disk(arch, output, root):
    for name in ("BIN", "SUB"):
        (root / name).mkdir()
    shutil.copyfile(output / "driver.tos", root / "EMUCON.TOS")
    shutil.copyfile(output / "interpreter.tos", root / "BIN/INTERP.TOS")
    (root / "PTOS.INI").write_text("""; Registry ordering and invalid/disabled definitions
[future]
reserved = ignored
[binfmt.disabled]
enabled=false
offset=2
magic=4d46
interpreter=/MISSING.TOS
[binfmt.invalid]
magic=4d46
MAGIC=4d46
interpreter=/MISSING.TOS
[BINFMT.fixture]
offset=0x2
magic=4D46
mask=FFFF
interpreter=/BIN\\INTERP.TOS
[binfmt.elf]
magic=7f454c46
interpreter=\\BIN/INTERP.TOS
[binfmt.prg]
magic=601a
interpreter=/BIN/INTERP.TOS
[binfmt.missing]
magic=4e4f
interpreter=/NOFILE.TOS
[binfmt.later]
magic=4e4f
interpreter=/BIN/INTERP.TOS
[binfmt.hashbang]
magic=2321
interpreter=/BIN/INTERP.TOS
""", encoding="ascii")
    contents = {
        "DEMO.FMT": b"xxMFpayload", "FOREIGN.TOS": b"xxMFpayload",
        "SIMPLE.TOS": b"#!/BIN/INTERP.TOS -s  --literal 'quotes'\nbody",
        "CHAIN.TOS": b"#!/SUB/FOREIGN.TOS -s  --literal 'quotes'\r\nbody",
        "L0.TOS": b"#!/SUB/D0.TOS\n", "LOOPA.TOS": b"#!/SUB/LOOPB.TOS\n",
        "LOOPB.TOS": b"#!\\sub\\loopa.tos\n", "MISSING.TOS": b"NO missing interpreter",
        "BAD.TOS": b"#! \n", "RELATIVE.TOS": b"#!relative\n",
        "LONG.TOS": b"#!/" + b"a" * 254 + b"\n", "UNKNOWN.TOS": b"unknown",
        "SHORT.TOS": b"xxM", "FOREIGN.ELF": elf_header(arch, True),
        "NATBAD.ELF": elf_header(arch),
        "RAW.TOS": struct.pack(">H6IH", 0x601a, 10, 0, 0, 0, 0, 0, 1) +
                   bytes.fromhex("3f3c00253f3c004c4e41"),
    }
    for i in range(4):
        target = "/BIN/INTERP.TOS" if i == 3 else f"/SUB/D{i + 1}.TOS"
        contents[f"D{i}.TOS"] = ("#!" + target + "\n").encode("ascii")
    for name, data in contents.items():
        (root / "SUB" / name).write_bytes(data)


def run(arch, output, timeout):
    output = output.resolve()
    root = output / "stage"
    if root.exists():
        shutil.rmtree(root)
    root.mkdir()
    stage_disk(arch, output, root)
    qemu = ["qemu-system-" + {"arm": "arm", "x32": "x86_64", "m68k": "m68k"}[arch]]
    if os.environ.get("QEMU_DATA_DIR"):
        qemu += ["-L", os.environ["QEMU_DATA_DIR"]]
    disk = output / "disk.img"
    if arch == "x32":
        boot = root / "EFI/BOOT"
        boot.mkdir(parents=True)
        shutil.copyfile("pc-x86_64.efi", boot / "BOOTX64.EFI")
        ovmf = Path(os.environ.get("OVMF_DIR", "/usr/share/OVMF"))
        shutil.copyfile(ovmf / "OVMF_VARS_4M.fd", output / "vars.fd")
        qemu += ["-machine", "pc", "-cpu", "qemu64,+pdpe1gb", "-m", "256",
                 "-drive", "if=pflash,format=raw,readonly=on,file=" + str(ovmf / "OVMF_CODE_4M.fd"),
                 "-drive", "if=pflash,format=raw,file=" + str(output / "vars.fd"),
                 "-drive", "format=raw,file=" + str(disk)]
    else:
        qemu += ["-M", "virt,highmem=off" if arch == "arm" else "virt",
                 "-cpu", "cortex-a7" if arch == "arm" else "m68040", "-m", "128",
                 "-global", "virtio-mmio.force-legacy=false",
                 "-kernel", f"virt-{arch}.elf",
                 "-drive", "if=none,id=disk,format=raw,file=" + str(disk),
                 "-device", "virtio-blk-device,drive=disk"]
    with (output / "disk.log").open("w") as stream:
        subprocess.run(["sh", "tools/mkhdisk.sh", str(disk), "33554432",
                        *map(str, root.iterdir())], check=True, stdout=stream, stderr=subprocess.STDOUT)
    log = output / "serial.log"
    log.write_text("")
    qemu += ["-nic", "none", "-display", "none", "-monitor", "none", "-serial", "file:" + str(log)]
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    (output / "test.json").write_text(json.dumps({"arch": arch, "revision": revision,
                                                 "command": qemu}, indent=2) + "\n")
    with (output / "qemu.log").open("w") as stream:
        process = subprocess.Popen(qemu, stdout=stream, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                text = log.read_text(errors="replace")
                if "binfmt boot: FAIL" in text or "process fault:" in text or "panic:" in text:
                    raise RuntimeError("native binfmt fixture failed:\n" + text[-6000:])
                if "binfmt boot: BEGIN" in text and "binfmt boot: PASS" in text:
                    if arch == "x32" and text.count("EFI entry reached") != 1:
                        raise RuntimeError("EFI reset/re-entry detected")
                    print(f"{arch}: binfmt boot: PASS ({log})")
                    return
                if process.poll() is not None:
                    raise RuntimeError(f"QEMU exited ({process.returncode}); see {output}")
                time.sleep(0.1)
            raise RuntimeError(f"binfmt boot timed out; see {output}")
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("arch", choices=("arm", "x32", "m68k"))
    parser.add_argument("output", type=Path)
    parser.add_argument("--timeout", type=float, default=90)
    args = parser.parse_args()
    run(args.arch, args.output, args.timeout)
