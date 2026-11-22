#!/usr/bin/env python3
"""Real KPROC/KTHREAD/KASPACE image-policy contracts, counted hosted MMU."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import kconfiglib

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
os.environ["CONFIG_"] = ""
parser = argparse.ArgumentParser()
parser.add_argument("--mode", choices=["normal", "asan", "fortified"], default="normal")
parser.add_argument("--shared", action="store_true", help="also capture interpreter/standard-entry policy")
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="ptos-image-policy-") as directory:
    work = Path(directory)
    config = kconfiglib.Kconfig(warn_to_stderr=False)
    config.load_config("configs/pc-x86_64_defconfig")
    config.load_config("tools/hosted-legacy-profile.config", replace=False)
    assert config.syms["CONF_WITH_IMAGE_POLICY_TEST"].str_value == "n"
    for name in ("CONF_WITH_ELF_SHARED", "CONF_WITH_CUIS", "CONF_WITH_X32_VDI",
                 "CONF_WITH_USER_CALLBACKS", "CONF_WITH_MULTITHREADING"):
        config.syms[name].set_value(0)
    if args.shared:
        config.syms["CONF_WITH_ELF_SHARED"].set_value(2)
        assert config.syms["CONF_WITH_ELF_SHARED"].str_value == "y"
    for name in ("CONF_WITH_MMAP", "CONF_WITH_X86_64_MEMTEST", "CONF_WITH_MULTITHREADING", "CONF_WITH_PRIVATE_CLONE_TEST", "CONF_WITH_IMAGE_POLICY_TEST"):
        config.syms[name].set_value(2)
        assert config.syms[name].str_value == "y", name
    config.write_config(str(work / "config"))
    subprocess.run([sys.executable, "tools/genconfig.py", "--kconfig", "Kconfig", "--config", str(work / "config"),
                    "--header", str(work / "autoconf.h"), "--makefile", str(work / "auto.conf")], check=True)
    flags = []
    if args.mode == "asan":
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    elif args.mode == "fortified":
        flags += ["-D_FORTIFY_SOURCE=3", "-fstack-protector-strong"]
    cc = shlex.split(os.environ.get("HOSTCC", "cc"))
    subprocess.run(cc + ["-fno-builtin", "-std=gnu90", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
        "-fno-pie", "-no-pie", "-ffunction-sections", "-fdata-sections", "-iquote", "include", "-I" + str(work),
        "-Iinclude/arch/x86_64", "-idirafter", "include", "-iquote", "bdos", "-iquote", "fs"] + flags +
        ["tests/image_policy.c", "bdos/kthread.c", "bdos/kowner.c", "tests/kowner_model.c", "bdos/proc_clone.c", "bdos/process_observer.c",
         "-Wl,--gc-sections", "-o", str(work / "policy")], check=True)
    subprocess.run([str(work / "policy")], check=True)
