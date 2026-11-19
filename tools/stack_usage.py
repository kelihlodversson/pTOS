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
AES_FIELDS = (r"seq=(\d+) rlr=(0x[\da-fA-F]+) bottom=(0x[\da-fA-F]+) "
              r"current=(0x[\da-fA-F]+) top=(0x[\da-fA-F]+) free=(-?\d+)")
AES_ENTER = re.compile(r"AES enter\s+" + AES_FIELDS + r"\s*$")
AES_END = re.compile(r"AES end seq=(\d+)\s*$", re.M)
AES_EXIT = re.compile(r"AES exit\s+" + AES_FIELDS + r", " +
    r"used=(\d+), _?max_usage=(\d+), recommended AES_STACK_SIZE=(\d+)\s*$")
DESKTOP = re.compile(r"Desktop stack has (-?\d+) bytes available\s*$", re.M)
FATAL_DIAGNOSTIC = re.compile(r"\b(panic|fatal|double bus fault)\b", re.I)
PBENCH = re.compile(r"^(.+?)\s+(\d+\.\d{3}) s\s+(-?\d+)\s+\w+(?:\s+\d+%)?\s*$")


def pbench_complete(text, resolution):
    """Require every named test exactly once and the requested display mode."""
    lines = text.splitlines()
    results = []
    for line in lines:
        if match := PBENCH.fullmatch(line):
            results.append(match)
        elif line.strip() and not line.startswith(("Screen ", "Speed relative to ")):
            return False
    screen = "Screen %dx%d, %d planes" % SCREENS[resolution]
    return (Counter(match.group(1) for match in results) == Counter(PBENCH_TESTS) and
            all(int(match.group(3)) > 0 for match in results) and
            [line for line in lines if line.startswith("Screen ")] == [screen])


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
    entries = 0
    pending = {}
    next_sequence = 1
    ended = False
    observed_maximum = 0
    for line in log.splitlines():
        if "AES enter" in line or "AES exit" in line:
            if ended:
                raise ValueError("AES telemetry after producer end marker")
            entering = "AES enter" in line
            match = (AES_ENTER if entering else AES_EXIT).search(line)
            if not match:
                raise ValueError("malformed AES entry/exit telemetry: " + line)
            sequence = int(match.group(1))
            if sequence != next_sequence:
                raise ValueError(f"AES telemetry sequence gap/reorder: expected={next_sequence}, got={sequence}")
            next_sequence += 1
            process, bottom, current, top = (int(x, 16) for x in match.groups()[1:5])
            free = int(match.group(6))
            if (top - bottom != budgets["aes"] or not bottom <= current <= top or
                    free != current - bottom):
                raise ValueError("inconsistent AES addresses, allocation or usage: " + line)
            key = hex(process)
            frame = (bottom, current, top)
            if entering:
                if key in pending:
                    raise ValueError("AES entry without the previous exit: " + key)
                pending[key] = frame
                # Event waits may still be outstanding at emulator stop.
                # Their occupied prefix is already a measured lower bound.
                processes[key] = max(processes.get(key, 0), top - current)
                entries += 1
                continue
            if pending.pop(key, None) != frame:
                raise ValueError("AES exit without matching entry/frame: " + key)
            used, maximum, _ = map(int, match.groups()[6:])
            if not 0 <= used <= free or maximum < used:
                raise ValueError("inconsistent AES per-call usage: " + line)
            # The producer's max_usage is global across AES calls/processes.
            # Every new cumulative maximum must be explained by a sample we
            # actually saw, including its entry prefix. A later matching call
            # cannot recover the missing earlier call's total stack usage.
            observed_maximum = max(observed_maximum, used)
            if maximum != observed_maximum:
                raise ValueError("incomplete or inconsistent AES cumulative max_usage: "
                                 f"reported={maximum}, observed={observed_maximum}")
            peak = top - current + used
            processes[key] = max(processes.get(key, 0), peak)
            samples += 1
        if "AES end" in line:
            match = AES_END.search(line)
            if not match or ended or int(match.group(1)) != next_sequence - 1:
                raise ValueError("missing/reordered AES telemetry at producer end marker")
            ended = True
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
    if not ended:
        raise ValueError("missing producer AES end marker")
    # Samples are at About entry. A third entry proves two prior dialogs
    # returned to the desktop; merely opening one dialog is insufficient.
    if len(desktop) < 3:
        raise ValueError("incomplete desktop workload: need three About-entry samples")
    if not pbench_complete(pbench, resolution):
        raise ValueError("incomplete pBench workload or wrong display mode")
    if not log.rstrip().endswith(COMPLETED):
        raise ValueError("missing controller completion marker (timeout, crash or incomplete run)")
    if FATAL_DIAGNOSTIC.search(log):
        raise ValueError("fatal emulator/guest diagnostic in log")
    stacks = dict(aes=stack_result(budgets["aes"], max(processes.values()), AES_RESERVE),
                  desktop=stack_result(budgets["desktop"], max(desktop), DESKTOP_RESERVE))
    return dict(status="pass" if all(s["status"] == "pass" for s in stacks.values()) else "fail",
                stacks=stacks, aes_samples=samples, aes_entries=entries,
                aes_outstanding_calls=len(pending), aes_process_peaks=processes,
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
