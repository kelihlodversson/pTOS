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
subprocess.run([sys.executable, "tools/check-libaes-compatibility.py"], check=True)
subprocess.run([sys.executable, "tests/cuis/compatibility.py"], check=True)
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

# The external GEM desktop/resources must survive removal of both kernel
# AES implementations. Shared options retain their normal desktop defaults.
config = kconfiglib.Kconfig(warn_to_stderr=False)
config.load_config("configs/pc-x86_64-desktop-tests_defconfig")
config.syms["CONF_WITH_ELF_SHARED"].set_value(2)
config.syms["CONF_WITH_LIBAES"].set_value(2)
config.syms["CONF_WITH_AES"].set_value(0)
for name in ("CONF_WITH_LIBAES_DESK", "CONF_WITH_DYNAMIC_DESK", "CONF_WITH_READ_INF",
             "CONF_WITH_WINDOW_ICONS", "CONF_WITH_BACKGROUNDS", "CONF_WITH_COLOUR_ICONS",
             "CONF_WITH_3D_OBJECTS", "CONF_WITH_PORTABLE_RSC_LOAD", "CONF_WITH_X32_DESKTOP_TEST"):
    assert config.syms[name].str_value == "y", name
assert config.syms["AES_STACK_SIZE"].str_value == ""
assert config.syms["CONF_WITH_LOADABLE_CURSORS"].str_value == "n"
assert config.syms["CONF_WITH_LEGACY_RSC_LOAD"].str_value == "n"
config.syms["CONF_EXTERNAL_DESK"].set_value(0)
assert config.syms["CONF_WITH_LIBAES_DESK"].str_value == "n"
assert config.syms["CONF_WITH_DYNAMIC_DESK"].str_value == "n"
config.syms["CONF_EXTERNAL_DESK"].set_value(2)
config.syms["CONF_WITH_X32_VDI"].set_value(0)
assert config.syms["CONF_EXTERNAL_DESK"].str_value == "n"
config.syms["CONF_WITH_X32_VDI"].set_value(2)
assert config.syms["CONF_WITH_LIBAES_DESK"].str_value == "y"
config.syms["CONF_WITH_LIBAES"].set_value(0)
assert config.syms["CONF_EXTERNAL_DESK"].str_value == "n"
assert config.syms["CONF_WITH_READ_INF"].str_value == "n"
assert config.syms["CONF_WITH_LIBAES_DESK"].str_value == "n"

config = kconfiglib.Kconfig(warn_to_stderr=False)
config.load_config("configs/pc-x86_64-libaes-desktop_defconfig")
for name in ("CONF_WITH_CUIS", "CONF_EXTERNAL_DESK", "CONF_WITH_LIBAES_DESK",
             "CONF_WITH_DYNAMIC_DESK", "CONF_WITH_X32_VDI"):
    assert config.syms[name].str_value == "y", name
for name in ("CONF_WITH_AES", "CONF_WITH_X86_64_MEMTEST"):
    assert config.syms[name].str_value == "n", name

# The existing legacy GUI keeps its resource/desktop defaults on ARM/m68k.
for preset in ("virt-arm", "virt-m68k"):
    config = kconfiglib.Kconfig(warn_to_stderr=False)
    config.load_config("configs/" + preset + "_defconfig")
    for name in ("CONF_WITH_AES", "CONF_EXTERNAL_DESK", "CONF_WITH_READ_INF",
                 "CONF_WITH_COLOUR_ICONS", "CONF_WITH_PORTABLE_RSC_LOAD"):
        assert config.syms[name].str_value == "y", (preset, name)
    assert config.syms["CONF_WITH_LIBAES_DESK"].str_value == "n"

with tempfile.TemporaryDirectory(prefix="ptos-cuis-") as temp:
    work = Path(temp)
    subprocess.run([sys.executable, "tools/genconfig.py", "--kconfig", "Kconfig",
                    "--config", "configs/pc-x86_64_defconfig", "--header",
                    str(work / "autoconf.h"), "--makefile", str(work / "auto.conf")],
                   check=True)
    for preset in ("pc-x86_64", "virt-arm", "virt-m68k"):
        rejected = kconfiglib.Kconfig(warn_to_stderr=False)
        rejected.load_config("configs/" + preset + "_defconfig")
        rejected.syms["CONF_WITH_USER_ASPACE"].set_value(2)
        rejected.syms["CONF_WITH_AES"].set_value(2)
        if rejected.syms["CONF_WITH_USER_ASPACE"].str_value == "y":
            assert rejected.syms["CONF_WITH_AES"].str_value == "n", preset
    geometry = shlex.split(os.environ.get("HOSTCC", "cc"))
    geometry += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-I" + temp,
                 "-Iinclude", "-Iaes", "-Iaes/shared", "tests/cuis/graph_geometry.c",
                 "aes/shared/gemgraphutil.c", "-o", str(work / "graph-geometry")]
    subprocess.run(geometry, check=True)
    subprocess.run([str(work / "graph-geometry")], check=True)
    for order in (0, 1):
        command = shlex.split(os.environ.get("HOSTCC", "cc"))
        command += ["-std=gnu90", "-Wall", "-Wextra", "-Werror", "-I" + temp, "-Iinclude",
                    "-DVDI_HEADER_FIRST=" + str(order), "tests/cuis/header_compat.c", "-o", str(work / "headers")]
        subprocess.run(command, check=True)
        subprocess.run([str(work / "headers")], check=True)
    subprocess.run(shlex.split(os.environ.get("HOSTCC", "cc")) + ["-DMFORM_RSC", "tools/erd.c", "-o", str(work / "mrd")], check=True)
    subprocess.run([str(work / "mrd"), "-pmform", "aes/mform", str(work / "mforms")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-I" + temp, "-Iinclude", "-Ilib/aes", "-Iaes", "-Iaes/shared",
                "-include", "gem.h", "tests/cuis/graphics.c", "lib/aes/graphics.c", "lib/aes/layout.c", str(work / "mforms.c"),
                "-o", str(work / "graphics")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "graphics")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror",
                "-ffunction-sections", "-fdata-sections", "-fno-builtin",
                "-I" + temp, "-Iinclude", "-Iinclude/arch/x86_64", "-Ibdos",
                "-Iaes", "-Iaes/shared", "tests/cuis/session.c", "aes/cuis_region.c", "aes/shared/gemframegeom.c", "-Wl,--gc-sections",
                "-o", str(work / "sessions")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "sessions")], check=True)
    # Compile the real loader, termination, scheduler and close wrapper. Use the
    # hosted setjmp ABI so an injected disk longjmp also works under ASan.
    rollback = work / "deferred-rollback"
    rollback.mkdir()
    native = kconfiglib.Kconfig(warn_to_stderr=False)
    native.load_config("configs/pc-x86_64_defconfig")
    native.syms["CONF_WITH_ELF_SHARED"].set_value(2)
    native.syms["CONF_WITH_LIBAES"].set_value(2)
    native.write_config(str(rollback / "config"))
    subprocess.run([sys.executable, "tools/genconfig.py", "--kconfig", "Kconfig",
                    "--config", str(rollback / "config"), "--header",
                    str(rollback / "autoconf.h"), "--makefile", str(rollback / "auto.conf")],
                   check=True)
    (rollback / "setjmp.h").write_text("#include <setjmp.h>\n")
    # country.h includes this generated header; the keymap fixture installs
    # its own active maps and must also build before the first kernel make.
    (rollback / "i18nconf.h").write_text("/* Host fixture supplies its own keyboard maps. */\n")
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-Wno-unused-parameter", "-Wno-unused-variable", "-Wno-cast-function-type", "-Wno-clobbered",
                "-fno-pie", "-no-pie", "-ffunction-sections", "-fdata-sections",
                "-iquote", str(rollback), "-iquote", "include", "-iquote", "include/arch/x86_64", "-iquote", "bdos",
                "bdos/proc.c", "bdos/kfile.c", "-Wl,--gc-sections"]
    for test in ("deferred_rollback", "deferred_exit"):
        subprocess.run(command + ["tests/cuis/" + test + ".c", "-o", str(rollback / test)], check=True)
        subprocess.run([str(rollback / test)], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-iquote", str(rollback), "-iquote", "include", "-iquote", "bdos", "-iquote", "fs",
                "tests/cuis/process_directory.c", "-o", str(rollback / "directory")]
    subprocess.run(command, check=True)
    subprocess.run([str(rollback / "directory")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Werror", "-fno-builtin",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-iquote", str(rollback), "-iquote", "include", "-iquote", "include/arch/x86_64",
                "-iquote", "include/machine/pc-x86_64", "-iquote", "bios", "-iquote", "bdos",
                "tests/cuis/keymap.c", "-o", str(rollback / "keymap")]
    subprocess.run(command, check=True)
    subprocess.run([str(rollback / "keymap")], check=True)
    # Generate the desktop headers privately as well: these tests run before
    # a kernel build, and must not pick up stale generated resource headers.
    for kind, source, prefix in (("DESK_RSC", "desk/desktop", "desk_rsc"),
                                 ("ICON_RSC", "desk/icon", "icons")):
        generator = work / (prefix + "-generator")
        subprocess.run(shlex.split(os.environ.get("HOSTCC", "cc")) +
                       ["-D" + kind, "tools/erd.c", "-o", str(generator)], check=True)
        subprocess.run([str(generator), "-pdesk" if kind == "DESK_RSC" else "-picon",
                        source, str(work / prefix)], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-iquote", str(rollback), "-iquote", temp, "-iquote", "include",
                "-iquote", "desk", "-iquote", "aes", "-iquote", "aes/shared",
                "-iquote", "include/arch/x86_64", "tests/x32_desktop/refresh.c", "desk/deskrefresh.c",
                "-o", str(work / "desktop-refresh")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "desktop-refresh")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror",
                "-fno-builtin", "-I" + temp, "-Iinclude", "-Iaes", "-Iaes/shared",
                "-DLIBAES_OBJECT_HOST_TEST", "tests/cuis/objects.c",
                "lib/aes/objects.c", "aes/shared/gemobjtree.c", "aes/shared/gemobjbounds.c", "-o", str(work / "objects")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "objects")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror",
                # Scalar GEM pointer halves require 32-bit fixture addresses.
                "-fno-pie", "-no-pie", "-fno-builtin", "-I" + temp, "-Iinclude", "-Ilib/aes", "-Iaes/shared",
                "-DAES_FRAME_TEMPLATE", "aes/shared/gemframe.c", "aes/shared/gemslidegeom.c", "tests/cuis/application.c", "lib/aes/application.c", "lib/aes/lifecycle.c", "lib/aes/children.c", "lib/aes/events.c", "lib/aes/session.c", "lib/aes/gemwindows.c", "lib/aes/desktop.c", "aes/shared/gemframegeom.c", "aes/shared/gemmovegeom.c", "aes/shared/gemsizegeom.c",
                "-o", str(work / "application")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "application")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-I" + temp, "-Iinclude", "-Ilib/aes", "tests/cuis/lifecycle.c",
                "lib/aes/lifecycle.c", "-o", str(work / "lifecycle")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "lifecycle")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-I" + temp, "-Iinclude", "-Iaes/shared", "tests/cuis/popup_track.c",
                "aes/shared/gemmenupopup.c", "lib/aes/objects.c", "aes/shared/gemobjtree.c",
                "aes/shared/gemobjbounds.c", "-DLIBAES_OBJECT_HOST_TEST", "-o", str(work / "popup-track")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "popup-track")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-I" + temp, "-Iinclude", "-Iaes/shared", "tests/cuis/menu_bar.c",
                "aes/shared/gemmenubar.c", "-o", str(work / "menu-bar")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "menu-bar")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-I" + temp, "-Iinclude", "-Iaes/shared", "tests/cuis/frame_geometry.c",
                "aes/shared/gemframegeom.c", "-o", str(work / "frame-geometry")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "frame-geometry")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-I" + temp, "-Iinclude", "-Iaes/shared", "tests/cuis/move_geometry.c",
                "aes/shared/gemmovegeom.c", "aes/shared/gemsizegeom.c", "aes/shared/gemframegeom.c", "util/rectfunc.c", "-o", str(work / "move-geometry")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "move-geometry")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-I" + temp, "-Iinclude", "-Iaes/shared", "tests/cuis/size_geometry.c",
                "aes/shared/gemsizegeom.c", "aes/shared/gemframegeom.c", "-o", str(work / "size-geometry")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "size-geometry")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-I" + temp, "-Iinclude", "-Iaes/shared", "-DAES_FRAME_TEMPLATE", "tests/cuis/frame_tree.c",
                "aes/shared/gemframe.c", "-o", str(work / "frame-tree")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "frame-tree")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-I" + temp, "-Iinclude", "-Iaes/shared", "-DAES_FRAME_TEMPLATE", "tests/cuis/slide_geometry.c",
                "aes/shared/gemframe.c", "aes/shared/gemslidegeom.c", "util/rectfunc.c", "-o", str(work / "slide-geometry")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "slide-geometry")], check=True)
    fixture = work / "GEMTEST.RSC"
    subprocess.run([sys.executable, "tools/make-cuis-resource.py", str(fixture)], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-ffunction-sections", "-fdata-sections", "-I" + temp, "-Iinclude", "-Iaes", "-Iaes/shared", "-Ilib/aes", "-Ilib/ldso/client", "-Ilib/ldso",
                '-DAES_RSC_BINDINGS="resource_bindings.h"', "-D__ptos_getauxval=aes_test_getauxval", "-Dgetenv=aes_test_getenv", "-Dsprintf=aes_resource_test_sprintf",
                "tests/cuis/resources.c", "tests/cuis/gem_resources.c", "lib/aes/resources.c", "aes/shared/rsload_portable.c",
                "aes/shared/gemrsaddr.c", "aes/shared/gemrcfix.c", "aes/shared/gemlayout.c", "util/string.c", "-Wl,--gc-sections", "-o", str(work / "resources")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "resources"), str(fixture), "aes/gem.rsc"], check=True)
    # These resource IDs/data are generated, and a clean checkout has neither
    # gem_rsc.h nor gem_rsc.c. Generate private host fixtures from the source RSC.
    subprocess.run(shlex.split(os.environ.get("HOSTCC", "cc")) +
                   ["-DGEM_RSC", "tools/erd.c", "-o", str(work / "grd")], check=True)
    subprocess.run([str(work / "grd"), "aes/gem", str(work / "gem_rsc")], check=True)
    (work / "i18nconf.h").write_text("/* Host selector uses untranslated resources. */\n")
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-fno-pie", "-no-pie", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-I" + temp, "-Iinclude", "-Iinclude/arch/x86_64", "-Iaes", "-Iaes/shared", "-Ilib/aes", "-Ilib/ldso/client", "-Ilib/ldso",
                '-DAES_SHELL_BINDINGS="shell_bindings.h"', "tests/cuis/shell.c", "lib/aes/shell.c", "lib/aes/lifecycle.c", "lib/aes/shell_arguments.c", "lib/aes/children.c", "lib/aes/resources.c", "lib/ldso/launch.c",
                "aes/shared/gemshquery.c", "util/optimize.c", "util/miscutil.c", "util/string.c", "util/doprintf.c",
                "-o", str(work / "shell")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "shell")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-I" + temp, "-Iinclude", "-Iaes", "-Iaes/shared", "-Ilib/aes",
                "tests/cuis/shell_buffer.c", "lib/aes/shell_buffer.c", "-o", str(work / "shell-buffer")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "shell-buffer")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-I" + temp, "-Iinclude", "-Ilib/aes",
                "tests/cuis/shell_metadata.c", "lib/aes/shell_metadata.c", "-o", str(work / "shell-metadata")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "shell-metadata")], check=True)
    command = shlex.split(os.environ.get("HOSTCC", "cc"))
    command += ["-std=gnu90", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                "-I" + temp, "-Iinclude", "-Iinclude/arch/x86_64", "-Iaes", "-Iaes/shared", "-Ilib/aes",
                "-DAES_RESOURCE_FSEL", "-DAES_RESOURCE_STRINGS_LOCAL", '-DAES_FSEL_BINDINGS="fsel_bindings.h"',
                '-DAES_EDIT_BINDINGS="edit_bindings.h"', "tests/cuis/file_selector.c", "lib/aes/fsel.c",
                "aes/shared/gemfslib.c", str(work / "gem_rsc.c"), "aes/shared/gemlayout.c",
                "aes/shared/gemobjtree.c", "aes/shared/gemobjbounds.c", "aes/shared/gemobjwalk.c", "aes/shared/gemedit.c",
                "util/rectfunc.c", "util/doprintf.c", "util/optimize.c", "util/miscutil.c", "util/string.c", "-ffunction-sections", "-fdata-sections",
                "-Wl,--gc-sections", "-o", str(work / "file-selector")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "file-selector")], check=True)
    shared_draw = []
    for name in ("gemrender", "gemrender_graphics", "gemgraphutil", "gemicon", "gemcicon", "gemobjbounds"):
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
                "-I" + temp, "-Iinclude", "-Iaes", "-Iaes/shared", "-Ilib/aes", "-Ilib/ldso/client", "-Ilib/ldso", "-DAES_FRAME_TEMPLATE", "tests/cuis/object_draw.c", "tests/cuis/box_animations.c",
                "tests/cuis/mono_objects.c", "tests/cuis/colour_objects.c", "tests/cuis/three_d.c", "tests/cuis/menu_items.c", "tests/cuis/popup_menu.c", "tests/cuis/gem_menubar.c", "lib/aes/menus.c", "lib/aes/popup.c", "lib/aes/menubar.c", "aes/shared/gemmenubar.c", "aes/shared/gemmenupopup.c", "aes/shared/gemmenuchange.c", "tests/cuis/gem_layout.c", "tests/cuis/form_keys.c", "tests/cuis/gem_buttons.c", "tests/cuis/gem_forms.c", "lib/aes/resources.c", "lib/aes/forms.c", "lib/aes/alerts.c", "aes/shared/gemformdo.c", "aes/shared/gemalert.c", str(work / "gem_rsc.c"), "aes/shared/gemformerror.c", "lib/aes/formbutton.c", "aes/shared/gemformbutton.c", "aes/shared/gemwatch.c", "lib/aes/formkeys.c", "aes/shared/gemformkey.c", "lib/aes/frames.c", "lib/aes/desktop.c", "aes/shared/gemslidegeom.c", "aes/shared/gemframe.c", "aes/shared/gemframegeom.c", "lib/aes/layout.c", "aes/shared/gemlayout.c", "lib/aes/draw.c", "lib/aes/animations.c", "lib/aes/tracking.c", "aes/shared/gemgrtrack.c", "aes/shared/gemmovegeom.c", "aes/shared/gemsizegeom.c", "aes/shared/gemedit.c", "aes/shared/gemformanim.c", "lib/aes/objects.c", "aes/shared/gemobjtree.c", "aes/shared/gemobjwalk.c", *shared_draw, "util/string.c", "util/rectfunc.c",
                '-DOB_WALK_COORD=LONG', '-DAES_RESOURCE_FORMS_ONLY', '-DAES_RESOURCE_STRINGS_LOCAL', '-DAES_MENU_BINDINGS="menu_bindings.h"', '-DAES_MODAL_BINDINGS="modal_bindings.h"', '-DAES_ALERT_BINDINGS="alert_bindings.h"', '-DAES_ERROR_BINDINGS="error_bindings.h"', '-DAES_EDIT_BINDINGS="edit_bindings.h"', '-DAES_ANIM_BINDINGS="anim_bindings.h"', '-DAES_GRTRACK_BINDINGS="track_bindings.h"', '-DAES_FORM_BINDINGS="form_bindings.h"', '-DAES_FORMKEY_BINDINGS="formkey_bindings.h"', "-Dsprintf=aes_draw_test_sprintf", "-ffunction-sections", "-fdata-sections",
                "-Wl,--gc-sections,--wrap=ob_offset", "-o", str(work / "draw")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "draw")], check=True)
print("CUIS sessions/streams/events, GEM API/object contracts and Kconfig tests passed.")
