#!/usr/bin/env python3
"""Check LRA defaults, architecture isolation and build flag propagation."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import kconfiglib

ROOT = Path(__file__).resolve().parents[1]
MAKE = shutil.which("gmake") or shutil.which("make")
HOST_PATH = os.environ["PATH"]


class LraConfiguration(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ptos-lra-config-")
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.bin = self.work / "bin"
        self.bin.mkdir()
        (self.bin / "sh").symlink_to("/bin/sh")
        # Exclude the host PATH so an installed SDK cannot affect missing-GCC tests.
        self.environment = patch.dict(os.environ, CONFIG_="", PATH=str(self.bin))
        self.environment.start()
        self.addCleanup(self.environment.stop)

    def compiler(self, version="16.2.0", target="m68k-unknown-ptos-prgelf", lra=True):
        cc = self.bin / "m68k-unknown-ptos-prgelf-gcc"
        cc.write_text(f'''#!/bin/sh
for arg in "$@"; do
    if [ "$arg" = -print-libgcc-file-name ]; then echo /mock/libgcc.a; exit 0; fi
done
case "$1" in
    -dumpversion) echo {version} ;;
    -dumpmachine) echo {target} ;;
    -print-multi-lib) echo '.;' ;;
    *) exit {0 if lra else 1} ;;
esac
''')
        cc.chmod(0o755)

    def config(self, preset="atari192"):
        config = kconfiglib.Kconfig(str(ROOT / "Kconfig"), warn_to_stderr=False)
        config.load_config(str(ROOT / "configs" / (preset + "_defconfig")))
        return config

    def test_missing_old_new_and_unsupported_compilers(self):
        self.assertEqual(self.config().syms["BUILD_M68K_LRA"].str_value, "n")
        for version, target, supported, expected in (
            ("15.3.0", "m68k-unknown-ptos-prgelf", True, "n"),
            ("16.2.0", "m68k-unknown-ptos-prgelf", True, "y"),
            ("17.1.0", "m68k-unknown-ptos-prgelf", True, "y"),
            ("unknown", "m68k-unknown-ptos-prgelf", True, "n"),
            ("16.2.0", "m68k-atari-mintelf", True, "n"),
            ("16.2.0", "m68k-unknown-ptos-prgelf", False, "n"),
        ):
            with self.subTest(version=version, target=target, supported=supported):
                self.compiler(version, target, supported)
                self.assertEqual(self.config().syms["BUILD_M68K_LRA"].str_value, expected)

    def test_manual_selection_and_prefix_override(self):
        config = self.config()
        config.syms["BUILD_M68K_LRA"].set_value("y")
        self.assertEqual(config.syms["BUILD_M68K_LRA"].str_value, "y")
        self.compiler()
        config = self.config()
        config.syms["BUILD_M68K_LRA"].set_value("n")
        self.assertEqual(config.syms["BUILD_M68K_LRA"].str_value, "n")
        config = self.config()
        config.syms["CROSS_COMPILE"].set_value("m68k-unknown-ptos-prgelf-")
        self.assertEqual(config.syms["BUILD_M68K_LRA"].str_value, "y")
        config.syms["CROSS_COMPILE"].set_value("/custom/m68k-unknown-ptos-prgelf-")
        self.assertEqual(config.syms["BUILD_M68K_LRA"].str_value, "n")
        config.syms["BUILD_M68K_LRA"].set_value("y")
        self.assertEqual(config.syms["BUILD_M68K_LRA"].str_value, "y")
        config = self.config()
        config.syms["BUILD_TOOLCHAIN_ELF"].set_value("y")
        self.assertEqual(config.syms["BUILD_M68K_LRA"].str_value, "n")

    def test_all_presets_keep_lra_on_m68k_only(self):
        self.compiler()
        for preset in sorted((ROOT / "configs").glob("*_defconfig")):
            with self.subTest(preset=preset.name):
                config = self.config(preset.name.removesuffix("_defconfig"))
                eligible = (config.syms["ARCH_M68K"].str_value == "y" and
                            config.syms["BUILD_TOOLCHAIN_PTOS"].str_value == "y" and
                            config.syms["CROSS_COMPILE"].str_value in
                            ("", "m68k-unknown-ptos-prgelf-"))
                self.assertEqual(config.syms["BUILD_M68K_LRA"].str_value,
                                 "y" if eligible else "n")
                if config.syms["ARCH_M68K"].str_value != "y":
                    config.syms["BUILD_M68K_LRA"].set_value("y")
                    self.assertEqual(config.syms["BUILD_M68K_LRA"].str_value, "n")

    def test_make_flags_and_command_line_cpu_override(self):
        self.compiler()
        config = self.config()
        config_file = self.work / ".config"
        auto = self.work / "auto.conf"
        header = self.work / "autoconf.h"
        probe = self.work / "probe.mk"
        probe.write_text("lra-flags-probe:\n\t@printf '%s\\n' '$(CPUFLAGS)' "
                         "'$(MULTILIBFLAGS)' '$(TEST_CPUFLAGS)' '$(CFLAGS)' '$(LD)'\n")
        env = dict(os.environ, PATH=str(self.bin) + os.pathsep + HOST_PATH)
        for enabled in ("y", "n"):
            config.syms["BUILD_M68K_LRA"].set_value(enabled)
            config.write_config(str(config_file))
            subprocess.run([sys.executable, "tools/genconfig.py", "--config", str(config_file),
                            "--header", str(header), "--makefile", str(auto)], env=env, check=True)
            for override in ([], ["CPUFLAGS=-m68020"]):
                output = subprocess.check_output([
                    MAKE, "-s", "-f", "Makefile", "-f", str(probe), "lra-flags-probe",
                    "KCONFIG_CONFIG=" + str(config_file), "AUTOCONF_MK=" + str(auto),
                    "AUTOCONF_H=" + str(header), "PYTHON=" + sys.executable, *override,
                ], env=env, text=True)
                for line in output.splitlines():
                    self.assertEqual(line.split().count("-mlra"), int(enabled == "y"), line)
                    if override:
                        self.assertIn("-m68020", line)
            self.assertNotIn("#define BUILD_M68K_LRA", header.read_text())

    def test_runtime_linker_compile_and_link_commands(self):
        self.compiler()
        config_file = self.work / ".config"
        auto = self.work / "auto.conf"
        header = self.work / "autoconf.h"
        targets = ("obj/ldso/ld.so", "obj/ldso/test/librt.so",
                   "obj/ldso/test/libdyn.so", "obj/ldso/test/soapp.elf",
                   "obj/ldso/test/soapp-pie.elf")
        env = dict(os.environ, PATH=str(self.bin) + os.pathsep + HOST_PATH)
        for preset, arch_flag in (("virt-m68k-aspace", "-m68020"),
                                  ("virt-arm-aspace", "-march=armv6k"),
                                  ("pc-x86_64", "-mx32")):
            config = self.config(preset)
            config.syms["CONF_WITH_VDI_SCALABLE_FONTS"].set_value("n")
            config.syms["CONF_WITH_ELF_SHARED"].set_value("y")
            for enabled in ("y", "n"):
                config.syms["BUILD_M68K_LRA"].set_value(enabled)
                config.write_config(str(config_file))
                subprocess.run([sys.executable, "tools/genconfig.py", "--config", str(config_file),
                                "--header", str(header), "--makefile", str(auto)],
                               env=env, check=True, stdout=subprocess.DEVNULL)
                for override in ([], ["CPUFLAGS=-m68060 -mshort"]):
                    with self.subTest(preset=preset, enabled=enabled, override=override):
                        output = subprocess.check_output([
                            MAKE, "-s", "-n", "-B", "-o", str(auto), "-o", str(header),
                            "KCONFIG_CONFIG=" + str(config_file), "AUTOCONF_MK=" + str(auto),
                            "AUTOCONF_H=" + str(header), "PYTHON=" + sys.executable,
                            *override, *targets,
                        ], env=env, text=True)
                        commands = [shlex.split(line) for line in output.replace("\\\n", "").splitlines()
                                    if line.split() and line.split()[0].endswith("gcc")]
                        self.assertTrue(commands, output)
                        outputs = {command[command.index("-o") + 1] for command in commands}
                        self.assertTrue(set(targets).issubset(outputs), outputs)
                        self.assertIn("obj/ldso/dynamic.o", outputs)
                        expected = int(preset == "virt-m68k-aspace" and enabled == "y")
                        for command in commands:
                            self.assertEqual(command.count("-mlra"), expected, command)
                            self.assertIn(arch_flag, command)
                            self.assertIn("-fPIC", command)
                            self.assertNotIn("-mshort", command)
                            self.assertNotIn("-m68060", command)

    def test_subbuild_compile_commands_and_allocator_identity(self):
        self.compiler()
        (self.bin / "arm-unknown-ptos-elf-gcc").symlink_to("m68k-unknown-ptos-prgelf-gcc")
        # Run real sub-make rules against isolated source copies. libcmini's
        # Makefile creates directories even on a dry run; leave checkouts alone.
        mini = self.work / "lib" / "libcmini"
        cli = self.work / "cli"
        shutil.copytree(ROOT / "lib/libcmini", mini,
                        ignore=shutil.ignore_patterns(".git", ".config", "obj", "build"))
        shutil.copytree(ROOT / "cli", cli,
                        ignore=shutil.ignore_patterns("*.o", ".arch-*", "libcmini-build-*"))
        shutil.copy(ROOT / "version.mk", self.work)
        config_file = self.work / ".config"
        auto = self.work / "auto.conf"
        header = self.work / "autoconf.h"
        probe = self.work / "subbuild.mk"
        probe.write_text(
            "lra-subbuild-probe:\n"
            "\t@printf 'libcmini-id=%s\\n' '$(LIBCMINI_BUILD_ID)'\n"
            "\t@$(LIBCMINI_MAKE) -n $(LIBCMINI_OBJDIR)/strlen.o\n"
            "\t@$(EMUCON_MAKE) -C '" + str(cli) + "' -n -B NLS=0 cmdutil.o\n")
        env = dict(os.environ, PATH=str(self.bin) + os.pathsep + HOST_PATH)
        def configure_libcmini(preset):
            for goal in (preset, "obj/auto.conf"):
                subprocess.run([MAKE, "-s", "-C", str(mini), goal,
                                "PYTHON=" + sys.executable, "KCONFIG_CONFIG=.config",
                                "CROSSPREFIX=m68k-unknown-ptos-prgelf-"],
                               env=env, check=True, stdout=subprocess.DEVNULL)

        for preset in ("atari512", "rpi2"):
            config = self.config(preset)
            config.syms["CONF_WITH_VDI_SCALABLE_FONTS"].set_value("n")
            config.syms["CONF_EXTERNAL_CLI"].set_value("y")
            mini_preset = "mintelf_defconfig" if preset == "atari512" else "ptos_arm_defconfig"
            configure_libcmini(mini_preset)
            identities = []
            for enabled in ("y", "n"):
                config.syms["BUILD_M68K_LRA"].set_value(enabled)
                config.write_config(str(config_file))
                subprocess.run([sys.executable, "tools/genconfig.py", "--config", str(config_file),
                                "--header", str(header), "--makefile", str(auto)],
                               env=env, check=True, stdout=subprocess.DEVNULL)
                for override in ([], ["CPUFLAGS=-m68020"]):
                    with self.subTest(preset=preset, enabled=enabled, override=override):
                        output = subprocess.check_output([
                            MAKE, "-s", "-f", "Makefile", "-f", str(probe), "lra-subbuild-probe",
                            "KCONFIG_CONFIG=" + str(config_file), "AUTOCONF_MK=" + str(auto),
                            "AUTOCONF_H=" + str(header), "PYTHON=" + sys.executable,
                            "LIBCMINI_DIR=" + str(mini),
                            "LIBCMINI_BUILDDIR=" + str(self.work / "mini-build"), *override,
                        ], env=env, text=True)
                        commands = [shlex.split(line) for line in output.splitlines()
                                    if " -c " in line]
                        self.assertEqual(len(commands), 2, output)
                        expected = int(preset == "atari512" and enabled == "y")
                        for command in commands:
                            self.assertEqual(command.count("-mlra"), expected, command)
                            # Do not leak kernel CPU/ABI flags into userland.
                            self.assertNotIn("-mshort", command)
                            self.assertNotIn("-m68020", command)
                        if preset == "atari512":
                            self.assertIn("-Wa,--register-prefix-optional", commands[1])
                        else:
                            for command in commands:
                                self.assertIn("-mfloat-abi=hard", command)
                                self.assertIn("-mfpu=vfp", command)
                        if not override:
                            identities.append(next(line for line in output.splitlines()
                                                   if line.startswith("libcmini-id=")))
            self.assertEqual(len(identities), 2)
            if preset == "atari512":
                self.assertNotEqual(*identities)
            else:
                self.assertEqual(*identities)

        # A direct standalone build also gets the option, and changes both
        # object identity and its private libcmini directory when toggled.
        configure_libcmini("mintelf_defconfig")
        cli_probe = self.work / "cli-probe.mk"
        cli_probe.write_text("lra-cli-probe:\n"
                             "\t@printf '%s\\n' '$(STAMP)' '$(LIBCMINI_BUILDDIR)'\n"
                             "\t@$(LIBCMINI_MAKE) -n $(LIBCMINI_OBJDIR)/strlen.o\n")
        identities = []
        for enabled in ("y", "n"):
            output = subprocess.check_output([
                MAKE, "-s", "-C", str(cli), "-f", "Makefile", "-f", str(cli_probe),
                "lra-cli-probe", "BUILD_M68K_LRA=" + enabled,
            ], env=env, text=True)
            lines = output.splitlines()
            identities.append(lines[:2])
            command = next(shlex.split(line) for line in lines if " -c " in line)
            self.assertEqual(command.count("-mlra"), int(enabled == "y"), command)
        for enabled_id, disabled_id in zip(*identities):
            self.assertNotEqual(enabled_id, disabled_id)


if __name__ == "__main__":
    os.chdir(ROOT)
    unittest.main()
