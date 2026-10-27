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
                "-Iaes", "-Iaes/shared", "tests/cuis/session.c", "aes/cuis_region.c", "-Wl,--gc-sections",
                "-o", str(work / "sessions")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "sessions")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror",
                "-fno-builtin", "-I" + temp, "-Iinclude", "-Iaes", "-Iaes/shared",
                "-DLIBAES_OBJECT_HOST_TEST", "tests/cuis/objects.c",
                "lib/aes/objects.c", "aes/shared/gemobjtree.c", "-o", str(work / "objects")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "objects")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror",
                "-fno-builtin", "-I" + temp, "-Iinclude", "-Ilib/aes",
                "tests/cuis/application.c", "lib/aes/application.c", "lib/aes/events.c", "lib/aes/session.c", "lib/aes/gemwindows.c",
                "-o", str(work / "application")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "application")], check=True)
    shared_draw = []
    for name in ("gemrender", "gemrender_graphics", "gemgraphutil", "gemicon"):
        target = str(work / (name + ".o"))
        command = shlex.split(os.environ.get("HOSTCC", "cc"))
        command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                    "-I" + temp, "-Iinclude", "-Ilib/aes", "-Iaes", "-Iaes/shared",
                    '-DAES_RENDER_BINDINGS="render_bindings.h"',
                    "-ffunction-sections", "-fdata-sections", "-c", "aes/shared/" + name + ".c", "-o", target]
        subprocess.run(command, check=True)
        shared_draw.append(target)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-I" + temp, "-Iinclude", "-Iaes", "-Iaes/shared", "-Ilib/aes", "tests/cuis/object_draw.c",
                "tests/cuis/mono_objects.c", "tests/cuis/gem_layout.c", "tests/cuis/form_keys.c", "tests/cuis/gem_buttons.c", "tests/cuis/gem_forms.c", "lib/aes/forms.c", "lib/aes/alerts.c", "aes/shared/gemformdo.c", "aes/shared/gemalert.c", "aes/shared/gemalertdata.c", "aes/shared/gemalertstrings.c", "aes/shared/gemformerror.c", "lib/aes/formbutton.c", "aes/shared/gemformbutton.c", "aes/shared/gemwatch.c", "lib/aes/formkeys.c", "aes/shared/gemformkey.c", "lib/aes/layout.c", "aes/shared/gemlayout.c", "lib/aes/draw.c", "aes/shared/gemedit.c", "aes/shared/gemformanim.c", "lib/aes/objects.c", "aes/shared/gemobjtree.c", "aes/shared/gemobjwalk.c", *shared_draw, "util/string.c", "util/rectfunc.c",
                '-DAES_RESOURCE_STRINGS_LOCAL', '-DAES_MODAL_BINDINGS="modal_bindings.h"', '-DAES_ALERT_BINDINGS="alert_bindings.h"', '-DAES_ERROR_BINDINGS="error_bindings.h"', '-DAES_EDIT_BINDINGS="edit_bindings.h"', '-DAES_ANIM_BINDINGS="anim_bindings.h"', '-DAES_FORM_BINDINGS="form_bindings.h"', '-DAES_FORMKEY_BINDINGS="formkey_bindings.h"', "-Dsprintf=aes_draw_test_sprintf", "-ffunction-sections", "-fdata-sections",
                "-Wl,--gc-sections,--wrap=ob_offset", "-o", str(work / "draw")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "draw")], check=True)
print("CUIS sessions/streams/events, GEM API/object contracts and Kconfig tests passed.")
