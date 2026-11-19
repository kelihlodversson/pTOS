#!/usr/bin/env python3
"""Regression tests for stack budgets, incomplete telemetry and workloads."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import MagicMock, patch

from stack_usage import (COMPLETED, PBENCH_TESTS, analyze, pbench_complete, stack_result)

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("ci_stack", ROOT / "tools/ci-stack-reserve.py")
ci_stack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci_stack)


def aes(peak=1900, process=1, bottom=0x1000, size=2560, lto=False, maximum=None):
    # Already occupied prefix = 400 bytes, distinct from the marker's used.
    top = bottom + size
    current = top - 400
    return (f"AES exit  rlr={hex(process)} bottom={hex(bottom)} current={hex(current)} "
            f"top={hex(top)} free={current-bottom}, used={peak-400}, "
            f"{'_max_usage' if lto else 'max_usage'}={peak-400 if maximum is None else maximum}, "
            "recommended AES_STACK_SIZE=1")


def log(*samples, desktop_free=1800):
    return "\n".join([*samples, *([f"Desktop stack has {desktop_free} bytes available"] * 3), COMPLETED])


def pbench():
    return "Screen 320x200, 4 planes\n" + "\n".join(
        name + " 1.000 s 42 objects" for name in PBENCH_TESTS)


class StackReserve(unittest.TestCase):
    def setUp(self):
        self.metadata = dict(allocated_bytes=dict(aes=2560, desktop=2560))

    def check(self, text, output=None):
        return analyze(text, pbench() if output is None else output, self.metadata, "low")

    def test_exact_reserve_boundary_and_overflow(self):
        for remaining, expected in ((511, "fail"), (512, "pass"), (513, "pass"),
                                    (0, "fail"), (-1, "fail")):
            with self.subTest(remaining=remaining):
                result = stack_result(2560, 2560-remaining, 512)
                self.assertEqual(result["status"], expected)
                self.assertEqual(result["remaining_bytes"], remaining)

    def test_aes_entry_prefix_is_included(self):
        result = self.check(log(aes(2100)))
        self.assertEqual(result["status"], "fail")
        self.assertEqual(result["stacks"]["aes"]["peak_bytes"], 2100)
        self.assertEqual(result["stacks"]["aes"]["remaining_bytes"], 460)

    def test_multiple_processes_and_lto_spelling(self):
        result = self.check(log(aes(1800), aes(2048, process=2, bottom=0x4000, lto=True),
                                aes(1900, maximum=1648)))
        self.assertEqual(result["status"], "pass")
        self.assertEqual(result["aes_samples"], 3)
        self.assertEqual(result["aes_process_peaks"], {"0x1": 1900, "0x2": 2048})

    def test_missing_cumulative_peak_cannot_pass(self):
        for lto in (False, True):
            with self.subTest(lto=lto), self.assertRaisesRegex(ValueError, "cumulative"):
                self.check(log(aes(1900, maximum=2200, lto=lto)))
        # A later sample reaching the reported maximum does not recover the
        # missing earlier sample's prefix. Validate at each exit, not at EOF.
        with self.assertRaisesRegex(ValueError, "cumulative"):
            self.check(log(aes(1900, maximum=1700), aes(2100, maximum=1700)))

    def test_cumulative_peak_cannot_decrease_across_processes(self):
        with self.assertRaisesRegex(ValueError, "cumulative"):
            self.check(log(aes(2048), aes(1900, process=2, bottom=0x4000)))

    def test_desktop_reserve_independent_of_aes(self):
        for free, expected in ((511, "fail"), (512, "pass"), (513, "pass")):
            with self.subTest(free=free):
                result = self.check(log(aes(), desktop_free=free))
                self.assertEqual(result["status"], expected)
                self.assertEqual(result["stacks"]["desktop"]["remaining_bytes"], free)

    def test_uses_each_builds_production_allocation(self):
        self.metadata["allocated_bytes"]["aes"] = 3072
        result = self.check(log(aes(2560, size=3072)))
        self.assertEqual(result["status"], "pass")
        self.assertEqual(result["stacks"]["aes"]["remaining_bytes"], 512)
        with self.assertRaisesRegex(ValueError, "allocation"):
            self.check(log(aes(size=8192)))

    def test_missing_and_malformed_telemetry_fails(self):
        good = log(aes())
        for bad in ("", log(), good.replace("used=1500", "used=oops"),
                    good.replace("free=2160", "free=-1"),
                    good.replace("Desktop stack has 1800", "Desktop stack has invalid"),
                    log(aes(), desktop_free=3000), log(aes(), desktop_free=-1)):
            with self.subTest(log=bad), self.assertRaises(ValueError):
                self.check(bad)

    def test_incomplete_dialogs_and_controller_failure(self):
        good = log(aes())
        for bad in (good.replace(COMPLETED, "timeout"),
                    good.replace("Desktop stack has 1800 bytes available\n", "", 1),
                    "PANIC: corrupted stack\n" + good):
            with self.subTest(log=bad), self.assertRaises(ValueError):
                self.check(bad)

    def test_missing_duplicate_and_wrong_mode_pbench(self):
        good = pbench()
        self.assertTrue(pbench_complete(good, "low"))
        for bad in ("", good.replace("VDI Text ", "Unknown test "),
                    good + "\nVDI Text 1.000 s 42 lines", good.replace("320x200", "640x400"),
                    good.replace("42 objects", "0 objects", 1),
                    good + "\nScreen 640x400, 1 planes"):
            with self.subTest(pbench=bad), self.assertRaisesRegex(ValueError, "pBench"):
                self.check(log(aes()), bad)

    def test_invalid_extra_results_are_not_discarded(self):
        good = pbench()
        for extra in ("VDI Text 1.000 s 0 lines", "Unknown test 1.000 s 0 lines",
                      "VDI Text 1.000 s -1 lines", "VDI Text malformed result"):
            with self.subTest(extra=extra):
                self.assertFalse(pbench_complete(good + "\n" + extra, "low"))
                with self.assertRaisesRegex(ValueError, "pBench"):
                    self.check(log(aes()), good + "\n" + extra)

    def test_desktop_budget_from_compiled_symbols(self):
        self.assertEqual(ci_stack.desktop_budget("00000100 B deskstackbottom\n00000b00 b deskustack"), 2560)
        self.assertEqual(ci_stack.desktop_budget("00000100 B _deskstackbottom\n00000d00 b deskustack"), 3072)
        with self.assertRaises((KeyError, ValueError)):
            ci_stack.desktop_budget("")

    def test_display_matrix_and_workload_entry_samples(self):
        self.assertEqual(len(ci_stack.PR_CASES), 2)
        self.assertEqual(len(set(ci_stack.FULL_CASES)), 6)
        self.assertTrue(set(ci_stack.PR_CASES) <= set(ci_stack.FULL_CASES))
        self.assertEqual(set(ci_stack.PROFILES), {"stack-o2", "fastcall-o2", "fastcall-os-lto", "stack-o2-lto"})
        self.assertEqual(len(ci_stack.DESKTOP.findall(log(aes()))), 3)

    def test_effective_profile_flags_cannot_silently_drop_coverage(self):
        for profile, (_, fastcall, lto, optimization) in ci_stack.PROFILES.items():
            for lra in ("off", "on"):
                with self.subTest(profile=profile, lra=lra):
                    allocator = "-mlra" if lra == "on" else ""
                    kernel = " ".join((optimization, allocator,
                                       "-mfastcall" if fastcall == "y" else "",
                                       "-flto=auto" if lto == "y" else ""))
                    flags = dict(CFLAGS=kernel, CPUFLAGS=allocator, TEST_CFLAGS=allocator)
                    ci_stack.validate_flags(flags, profile, lra)
                    for bad in (kernel + " -fno-lto", kernel.replace(optimization, "-O0"),
                                kernel.replace("-mfastcall", "") if fastcall == "y" else kernel + " -mfastcall",
                                kernel.replace("-flto=auto", "") if lto == "y" else kernel + " -flto",
                                kernel.replace("-mlra", "") if lra == "on" else kernel + " -mlra"):
                        with self.assertRaises(ValueError):
                            ci_stack.validate_flags(dict(flags, CFLAGS=bad), profile, lra)
                    if lra == "on":
                        with self.assertRaises(ValueError):
                            ci_stack.validate_flags(dict(flags, CFLAGS=kernel + " -mno-lra"), profile, lra)

    def test_cli_fails_with_useful_diagnostic_and_json(self):
        with tempfile.TemporaryDirectory() as temp:
            dest = Path(temp)
            (dest / "metadata.json").write_text(json.dumps(self.metadata))
            (dest / "emulator.log").write_text(log(aes(2100)))
            (dest / "PBENCH.TXT").write_text(pbench())
            command = [sys.executable, str(ROOT / "tools/stack_usage.py"),
                       "--metadata", str(dest / "metadata.json"), "--log", str(dest / "emulator.log"),
                       "--pbench", str(dest / "PBENCH.TXT"), "--resolution", "low",
                       "--output", str(dest / "result.json")]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("remaining=460 B, required reserve=512 B", result.stdout)
            self.assertEqual(json.loads((dest / "result.json").read_text())["status"], "fail")
            (dest / "emulator.log").write_text(log(aes(maximum=2200)))
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("cumulative max_usage", result.stdout)
            (dest / "emulator.log").write_text(log(aes()))
            (dest / "PBENCH.TXT").write_text(pbench() + "\nVDI Text 1.000 s 0 lines")
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("incomplete pBench", result.stdout)
            (dest / "emulator.log").unlink()
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)

    def test_controller_crash_and_timeout_do_not_write_completion(self):
        for timeout in (False, True):
            with self.subTest(timeout=timeout), tempfile.TemporaryDirectory() as temp:
                dest = Path(temp)
                (dest / "PBENCH.PRG").write_bytes(b"fixture")
                args = SimpleNamespace(out=dest, hatari="fake-hatari", timeout=2)
                server = MagicMock()
                if timeout:
                    server.accept.side_effect = TimeoutError("fixture timeout")
                else:
                    server.accept.return_value = (MagicMock(), None)
                process = MagicMock()
                process.poll.return_value = 0
                with patch.object(ci_stack.socket, "socket") as socket_mock, \
                     patch.object(ci_stack.subprocess, "Popen", return_value=process), \
                     patch.object(ci_stack.time, "sleep"):
                    socket_mock.return_value.__enter__.return_value = server
                    with self.assertRaises((RuntimeError, TimeoutError)):
                        ci_stack.run_case(args, self.metadata, "low", "off")
                process.terminate.assert_called_once()
                self.assertNotIn(COMPLETED, (dest / "low-off/emulator.log").read_text())


if __name__ == "__main__":
    unittest.main()
