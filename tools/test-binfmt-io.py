#!/usr/bin/env python3
"""Inject disk errors into the real format loader, close wrapper and resolver."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
os.chdir(root)
os.environ["CONFIG_"] = ""
with tempfile.TemporaryDirectory(prefix="binfmt-io-") as temp:
    work = Path(temp)
    subprocess.run([sys.executable, "tools/genconfig.py", "--kconfig", "Kconfig",
                    "--config", "configs/pc-x86_64_defconfig", "--header",
                    str(work / "autoconf.h"), "--makefile", str(work / "auto.conf")], check=True)
    (work / "setjmp.h").write_text("#include <setjmp.h>\n")
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin", "-UNDEBUG",
                "-Wno-clobbered", "-Wno-unused-parameter",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-iquote", temp, "-iquote", "include", "-iquote", "include/arch/x86_64",
                "-iquote", "bdos", "bdos/binfmt.c", "bdos/binfmt_rules.c",
                "bdos/binfmt_args.c", "bdos/kpgmld.c", "bdos/kfile.c", "tests/binfmt_io.c",
                "-o", str(work / "io-test")]
    if os.environ.get("BINFMT_SANITIZE") == "1":
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "io-test")], check=True)
