#!/usr/bin/env python3
"""Run native CUIS ownership/failure tests and Kconfig backend constraints."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
os.environ["CONFIG_"] = ""
import kconfiglib

for preset, available in (("pc-x86_64", True), ("virt-arm", False),
                          ("virt-m68k", False)):
    config = kconfiglib.Kconfig(warn_to_stderr=False)
    config.load_config("configs/" + preset + "_defconfig")
    config.syms["CONF_WITH_ELF_SHARED"].set_value(2)
    config.syms["CONF_WITH_LIBAES"].set_value(2)
    assert (config.syms["CONF_WITH_CUIS"].str_value == "y") == available
    assert (config.syms["CONF_WITH_NATIVE_SCHED"].str_value == "y") == available
    config.syms["CONF_WITH_ELF_SHARED"].set_value(0)
    assert config.syms["CONF_WITH_LIBAES"].str_value == "n"
    assert config.syms["CONF_WITH_CUIS"].str_value == "n"
    assert config.syms["CONF_WITH_NATIVE_SCHED"].str_value == "n"

with tempfile.TemporaryDirectory(prefix="ptos-cuis-") as temp:
    work = Path(temp)
    subprocess.run([sys.executable, "tools/genconfig.py", "--kconfig", "Kconfig",
                    "--config", "configs/pc-x86_64_defconfig", "--header",
                    str(work / "autoconf.h"), "--makefile", str(work / "auto.conf")],
                   check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror",
                "-ffunction-sections", "-fdata-sections", "-fno-builtin",
                "-I" + temp, "-Iinclude", "-Iinclude/arch/x86_64", "-Ibdos",
                "-Iaes", "tests/cuis/session.c", "aes/cuis_region.c", "-Wl,--gc-sections",
                "-o", str(work / "sessions")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "sessions")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror",
                "-fno-builtin", "-I" + temp, "-Iinclude", "-Iaes",
                "-DLIBAES_OBJECT_HOST_TEST", "tests/cuis/objects.c",
                "lib/aes/objects.c", "aes/gemobjtree.c", "-o", str(work / "objects")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "objects")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror",
                "-fno-builtin", "-I" + temp, "-Iinclude", "-Ilib/aes",
                "tests/cuis/application.c", "lib/aes/application.c", "lib/aes/events.c", "lib/aes/session.c", "lib/aes/gemwindows.c",
                "-o", str(work / "application")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "application")], check=True)
print("CUIS sessions/streams/events, GEM API/object contracts and Kconfig tests passed.")
