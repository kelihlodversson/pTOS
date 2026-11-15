#!/usr/bin/env python3
"""Production interpreted KPROC staging with hosted MMU providers."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
os.environ["CONFIG_"] = ""
parser = argparse.ArgumentParser()
parser.add_argument("presets", nargs="*", choices=["pc-x86_64", "virt-arm-aspace", "virt-m68k-aspace"])
parser.add_argument("--mode", choices=["normal", "asan", "fortified"], default="normal")
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="ptos-interp-") as directory:
    work = Path(directory)
    cc = shlex.split(os.environ.get("HOSTCC", "cc"))
    for preset, arch, machine in [("pc-x86_64", "x86_64", "pc-x86_64"), ("virt-arm-aspace", "arm", "virt-arm"), ("virt-m68k-aspace", "m68k", "virt-m68k")]:
        if args.presets and preset not in args.presets:
            continue
        import kconfiglib
        k = kconfiglib.Kconfig(warn_to_stderr=False)
        k.load_config(f"configs/{preset}_defconfig")
        k.syms["CONF_WITH_NATIVE_SCHED"].set_value(2)
        k.syms["CONF_WITH_SHARED_VM_PEXEC_TEST"].set_value(2)
        k.syms["CONF_WITH_ELF_SHARED"].set_value(2)
        k.syms["CONF_WITH_PRIVATE_CLONE_TEST"].set_value(0)
        k.syms["CONF_WITH_MMAP"].set_value(2)
        k.syms["CONF_WITH_PRIVATE_CLONE"].set_value(0)
        assert k.syms["CONF_WITH_PRIVATE_CLONE"].str_value == "n"
        for name in ["CONF_WITH_NATIVE_SCHED", "CONF_WITH_SHARED_VM_PEXEC_TEST", "CONF_WITH_ELF_SHARED"]:
            assert k.syms[name].str_value == "y", name
        k.write_config(str(work / "config"))
        subprocess.run([sys.executable, "tools/genconfig.py", "--kconfig", "Kconfig", "--config", str(work / "config"), "--header", str(work / "autoconf.h"), "--makefile", str(work / "auto.conf")], env={**os.environ, "CONFIG_": ""}, check=True)
        flags = [] if arch == "x86_64" else ["-DBORROW_GENERIC"]
        if arch == "m68k":
            flags += ["-D__m68k__"]
        if args.mode == "asan":
            flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        elif args.mode == "fortified":
            flags += ["-D_FORTIFY_SOURCE=3", "-fstack-protector-strong"]
        subprocess.run(cc + ["-std=gnu90", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-fno-builtin", "-fno-pie", "-no-pie", "-ffunction-sections", "-fdata-sections", "-iquote", "include", "-I" + str(work), "-Iinclude/machine/" + machine, "-Iinclude/arch/" + arch, "-Ibios/machine/" + machine, "-Ibios/arch/" + arch, "-iquote", "bdos", "-iquote", "bios", "-iquote", "fs", "-iquote", "aes", "-idirafter", "include"] + flags + ["tests/kproc_shared_interp.c", "bdos/kaspace.c", "lib/ldso/startup.c", "-Wl,--gc-sections", "-o", str(work / "borrow")], check=True)
        subprocess.run([str(work / "borrow")], check=True)
        print(f"Production interpreted staging ({preset}): PASS", flush=True)
