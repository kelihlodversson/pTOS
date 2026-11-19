#!/usr/bin/env python3
"""Check LRA defaults, architecture isolation and build flag propagation."""
import os
from pathlib import Path
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
case "$1" in
    -dumpversion) echo {version} ;;
    -dumpmachine) echo {target} ;;
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


if __name__ == "__main__":
    os.chdir(ROOT)
    unittest.main()
