#!/usr/bin/env python3
"""Check release selection and fail-closed package verification without root."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PACKAGES = ("ptos-sdk-m68k", "ptos-sdk-arm", "ptos-sdk-x86-64")
MOCK = r'''#!/usr/bin/env python3
import json, os
from pathlib import Path
import subprocess, sys
name = Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ["INSTALL_TEST_LOG"], "a") as log:
    log.write(json.dumps([name, args]) + "\n")
if name == "uname": print("Linux")
elif name == "id": print("0")
elif name == "dpkg":
    if args == ["--print-architecture"]: print(os.environ.get("INSTALL_TEST_ARCH", "amd64"))
    else: sys.exit(subprocess.call(["/usr/bin/dpkg", *args]))
elif name == "curl":
    output = Path(args[args.index("-o") + 1])
    output.write_bytes(b"corrupt" if os.environ.get("INSTALL_TEST_CORRUPT") else output.name.encode())
elif name == "apt-get" and os.environ.get("INSTALL_TEST_NEWER"):
    if any(arg.endswith(".deb") for arg in args) and "--allow-downgrades" not in args:
        sys.exit(100)
elif name == "dpkg-query":
    version = os.environ.get("INSTALL_TEST_VERSION", "1.0.0")
    if any(arg.startswith("-f=") for arg in args): print(version)
    else:
        for arg in args:
            if not arg.startswith("-"): print(arg + "\t" + version)
'''


class RepositoryPin(unittest.TestCase):
    def test_complete_release_pin(self):
        lines = (ROOT / "tools/ptos-toolchain.sha256").read_text().splitlines()
        version = next(line[len("# release: "):] for line in lines if line.startswith("# release: "))
        tap = next(line[len("# homebrew-tap: "):] for line in lines if line.startswith("# homebrew-tap: "))
        self.assertRegex(version, r"^[0-9]+\.[0-9]+\.[0-9]+$")
        self.assertRegex(tap, r"^[0-9a-f]{40}$")
        entries = [line.split() for line in lines if line and not line.startswith("#")]
        self.assertEqual(len(entries), len(PACKAGES))
        self.assertEqual({entry[1] for entry in entries},
                         {p + "_" + version + "_amd64.deb" for p in PACKAGES})
        for checksum, _ in entries:
            self.assertRegex(checksum, r"^[0-9a-f]{64}$")


@unittest.skipUnless(sys.platform.startswith("linux"), "Debian package installer")
class ReleaseInstaller(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ptos-sdk-install-")
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        shutil.copy(ROOT / "tools/install-ptos-toolchain.sh", self.work)
        self.manifest = self.work / "ptos-toolchain.sha256"
        self.manifest.write_text("# release: 1.0.0\n" + "".join(
            hashlib.sha256((p + "_1.0.0_amd64.deb").encode()).hexdigest() +
            "  " + p + "_1.0.0_amd64.deb\n" for p in PACKAGES))
        binaries = self.work / "bin"
        binaries.mkdir()
        for name in ("uname", "id", "dpkg", "dpkg-query", "curl", "apt-get"):
            path = binaries / name
            path.write_text(MOCK)
            path.chmod(0o755)
        self.log = self.work / "calls.jsonl"
        self.env = dict(os.environ, PATH=str(binaries) + os.pathsep + os.environ["PATH"],
                        INSTALL_TEST_LOG=str(self.log))

    def run_installer(self, arch="all", **env):
        self.log.write_text("")
        return subprocess.run(["bash", str(self.work / "install-ptos-toolchain.sh"), arch, "--release"],
                              env=dict(self.env, **env), capture_output=True, text=True)

    def installed(self):
        calls = [json.loads(line) for line in self.log.read_text().splitlines()]
        return [Path(arg).name for name, args in calls if name == "apt-get"
                for arg in args if arg.endswith(".deb")]

    def test_selected_packages(self):
        for arch, packages in (("m68k", PACKAGES[:1]), ("arm", PACKAGES[1:2]),
                               ("x86_64", PACKAGES[2:]), ("all", PACKAGES)):
            with self.subTest(arch=arch):
                result = self.run_installer(arch)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(self.installed(), [p + "_1.0.0_amd64.deb" for p in packages])

    def test_corrupt_download_is_not_installed(self):
        result = self.run_installer(INSTALL_TEST_CORRUPT="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.installed(), [])

    def test_missing_checksum_does_not_install_partial_selection(self):
        self.manifest.write_text("\n".join(line for line in self.manifest.read_text().splitlines()
                                           if "ptos-sdk-arm" not in line) + "\n")
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Missing or invalid checksum", result.stderr)
        self.assertEqual(self.installed(), [])

    def test_installed_version_must_match_pin(self):
        result = self.run_installer(INSTALL_TEST_VERSION="2.0.0")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("expected pinned version 1.0.0", result.stderr)

    def test_newer_installation_can_restore_pin(self):
        result = self.run_installer(INSTALL_TEST_NEWER="1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.installed()), 3)
        calls = [json.loads(line) for line in self.log.read_text().splitlines()]
        for name, args in calls:
            if name == "apt-get" and "install" in args:
                self.assertEqual("--allow-downgrades" in args, any(arg.endswith(".deb") for arg in args))

    def test_invalid_release(self):
        self.manifest.write_text(self.manifest.read_text().replace("# release: 1.0.0", "# release: ../bad"))
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.installed(), [])

    def test_unsupported_host_architecture(self):
        result = self.run_installer(INSTALL_TEST_ARCH="arm64")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.installed(), [])


if __name__ == "__main__":
    unittest.main()
