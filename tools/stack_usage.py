#!/usr/bin/env python3
"""Validate stack telemetry and completed workloads, without an emulator."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re

AES_RESERVE = 512
DESKTOP_RESERVE = 512
COMPLETED = "STACK-CHECK workload completed"
PBENCH_TESTS = ("GEM Dialog Box", "VDI Text", "VDI Text Effects", "VDI Small Text",
                "VDI Graphics", "GEM Window", "Blitting", "VDI Scroll",
                "Justified Text", "VDI Enquire")
SCREENS = {"low": (320, 200, 4), "med": (640, 200, 2), "high": (640, 400, 1)}
AES_EXIT = re.compile(
    r"AES exit\s+rlr=(0x[\da-fA-F]+) bottom=(0x[\da-fA-F]+) "
    r"current=(0x[\da-fA-F]+) top=(0x[\da-fA-F]+) free=(-?\d+), "
    r"used=(\d+), _?max_usage=(\d+), recommended AES_STACK_SIZE=(\d+)\s*$")
DESKTOP = re.compile(r"Desktop stack has (-?\d+) bytes available\s*$", re.M)
PBENCH = re.compile(r"^(.+?)\s+(\d+\.\d{3}) s\s+(\d+)\s+\w+(?:\s+\d+%)?\s*$")


def pbench_complete(text, resolution):
    """Require every named test exactly once and the requested display mode."""
    tests = [m.group(1) for line in text.splitlines()
             if (m := PBENCH.fullmatch(line)) and int(m.group(3)) > 0]
    screen = "Screen %dx%d, %d planes" % SCREENS[resolution]
    return Counter(tests) == Counter(PBENCH_TESTS) and screen in text.splitlines()


def stack_result(allocation, peak, reserve):
    if allocation <= 0 or peak < 0 or reserve < 0:
        raise ValueError("invalid stack allocation, usage or reserve")
    remaining = allocation - peak
    return dict(allocated_bytes=allocation, peak_bytes=peak,
                remaining_bytes=remaining, reserve_bytes=reserve,
                status="pass" if remaining >= reserve else "fail")


def analyze(log, pbench, metadata, resolution):
    """Use total AES usage per sample; max_usage/recommended are not budgets."""
    budgets = metadata["allocated_bytes"]
    processes = {}
    desktop = []
    samples = 0
    for line in log.splitlines():
        if "AES exit" in line:
            match = AES_EXIT.search(line)
            if not match:
                raise ValueError("malformed AES exit telemetry: " + line)
            process, bottom, current, top = (int(x, 16) for x in match.groups()[:4])
            free, used, maximum, _ = map(int, match.groups()[4:])
            if (top - bottom != budgets["aes"] or not bottom <= current <= top or
                    free != current - bottom or not 0 <= used <= free or maximum < used):
                raise ValueError("inconsistent AES addresses, allocation or usage: " + line)
            peak = top - current + used
            key = hex(process)
            processes[key] = max(processes.get(key, 0), peak)
            samples += 1
        if "Desktop stack has" in line:
            match = DESKTOP.search(line)
            if not match:
                raise ValueError("malformed desktop telemetry: " + line)
            free = int(match.group(1))
            if not 0 <= free <= budgets["desktop"]:
                raise ValueError("desktop free bytes outside its allocation: " + line)
            desktop.append(budgets["desktop"] - free)
    if not samples:
        raise ValueError("missing AES telemetry")
    # Samples are at About entry. A third entry proves two prior dialogs
    # returned to the desktop; merely opening one dialog is insufficient.
    if len(desktop) < 3:
        raise ValueError("incomplete desktop workload: need three About-entry samples")
    if not pbench_complete(pbench, resolution):
        raise ValueError("incomplete pBench workload or wrong display mode")
    if not log.rstrip().endswith(COMPLETED):
        raise ValueError("missing controller completion marker (timeout, crash or incomplete run)")
    if re.search(r"\b(panic|fatal|double bus fault)\b", log, re.I):
        raise ValueError("fatal emulator/guest diagnostic in log")
    stacks = dict(aes=stack_result(budgets["aes"], max(processes.values()), AES_RESERVE),
                  desktop=stack_result(budgets["desktop"], max(desktop), DESKTOP_RESERVE))
    return dict(status="pass" if all(s["status"] == "pass" for s in stacks.values()) else "fail",
                stacks=stacks, aes_samples=samples, aes_process_peaks=processes,
                desktop_samples=len(desktop), pbench_tests=len(PBENCH_TESTS))


def report_lines(result):
    for name, stack in result.get("stacks", {}).items():
        yield (f"{name}: {stack['status'].upper()} allocation={stack['allocated_bytes']} B, "
               f"peak={stack['peak_bytes']} B, remaining={stack['remaining_bytes']} B, "
               f"required reserve={stack['reserve_bytes']} B")
    if "error" in result:
        yield result["error"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--pbench", type=Path, required=True)
    parser.add_argument("--resolution", choices=SCREENS, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        metadata = json.loads(args.metadata.read_text())
        result = analyze(args.log.read_text(errors="replace"),
                         args.pbench.read_text(errors="replace"), metadata, args.resolution)
    except (OSError, ValueError, KeyError, TypeError) as error:
        result = dict(status="fail", error=str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print("\n".join(report_lines(result)))
    return int(result["status"] != "pass")


if __name__ == "__main__":
    raise SystemExit(main())
