#!/usr/bin/env python3
"""Build one m68k profile and enforce its AES/desktop stack reserves in Hatari."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time

from stack_usage import (COMPLETED, DESKTOP, analyze, pbench_complete, report_lines)

ROOT = Path(__file__).resolve().parents[1]
PROFILES = {
    "stack-o2": ("atari512", "n", "n", "-O2"),
    "fastcall-o2": ("atari512-fastcall", "y", "n", "-O2"),
    "fastcall-os-lto": ("atari256", "y", "y", "-Os"),
    "stack-o2-lto": ("atari512", "n", "y", "-O2"),
}
PR_CASES = (("low", "off"), ("high", "on"))
FULL_CASES = tuple((mode, blitter) for mode in ("low", "med", "high")
                  for blitter in ("off", "on"))


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def desktop_budget(symbols):
    values = {}
    for line in symbols.splitlines():
        parts = line.split()
        if len(parts) == 3:
            values[parts[2].lstrip("_")] = int(parts[0], 16)
    size = values["deskustack"] - values["deskstackbottom"]
    if size <= 0 or size % 4:
        raise ValueError("invalid desktop stack allocation in deskstart.o")
    return size


def build(args, metadata):
    import kconfiglib

    os.environ["CONFIG_"] = ""
    os.environ["KCONFIG_CONFIG"] = ".config"
    preset, fastcall, lto, optimization = PROFILES[args.profile]
    config = kconfiglib.Kconfig(warn_to_stderr=False)
    config.load_config(str(ROOT / "configs" / (preset + "_defconfig")))
    for name, value in (("CONF_WITH_MFASTCALL", fastcall), ("BUILD_LTO", lto),
                        ("OPTFLAGS", optimization), ("BUILD_M68K_LRA", "y" if args.lra == "on" else "n")):
        config.syms[name].set_value(value)
        if config.syms[name].str_value != value:
            raise ValueError("cannot select profile setting " + name + "=" + value)
    if config.syms["ARCH_M68K"].str_value != "y" or config.syms["CONF_WITH_AES"].str_value != "y":
        raise ValueError("stack profiles require m68k kernel AES")
    # Record the production configuration before instrumentation. No larger
    # diagnostic allocation: an overflow/crash must fail the workload too.
    config.write_config(str(args.out / "production.config"))
    metadata.update(preset=preset, allocated_bytes=dict(aes=4 * int(config.syms["AES_STACK_SIZE"].str_value)),
                    sdk_pin=next(line.split(":", 1)[1].strip() for line in
                                 (ROOT / "tools/ptos-toolchain.sha256").read_text().splitlines()
                                 if line.startswith("# release:")))
    write_json(args.out / "metadata.json", metadata)
    for name in ("CONF_DEBUG_AES_STACK", "CONF_DEBUG_DESK_STACK", "DEBUG_PRINT_NONE"):
        config.syms[name].set_value("y")
        if config.syms[name].str_value != "y":
            raise ValueError("missing required instrumentation: " + name)
    config.syms["INITINFO_DURATION"].set_value("0")
    make = shutil.which("gmake") or shutil.which("make")
    compiler = (config.syms["CROSS_COMPILE"].str_value or "m68k-unknown-ptos-prgelf-") + "gcc"
    metadata["compiler"] = subprocess.check_output([compiler, "--version"], text=True).splitlines()[0]
    metadata["compiler_target"] = subprocess.check_output([compiler, "-dumpmachine"], text=True).strip()
    with (args.out / "build.log").open("w") as log:
        subprocess.run([make, "clean"], stdout=log, stderr=subprocess.STDOUT, check=True)
        config.write_config(".config")
        shutil.copy2(".config", args.out / "instrumented.config")
        subprocess.run([sys.executable, "tools/genconfig.py", "--header", "obj/autoconf.h",
                        "--makefile", "obj/auto.conf"], stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run([make, "-j" + str(args.jobs), "all", "pbench.prg"],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    symbols = subprocess.check_output([compiler.removesuffix("gcc") + "nm", "-n", "obj/deskstart.o"], text=True)
    (args.out / "desktop-symbols.txt").write_text(symbols)
    metadata["allocated_bytes"]["desktop"] = desktop_budget(symbols)
    probe = args.out / "flags.mk"
    probe.write_text("stack-flags:\n\t@:\n" + "".join(
        "$(info STACK_" + name + "=$(" + name + "))\n"
        for name in ("CPUFLAGS", "CFLAGS", "TEST_CFLAGS")))
    output = subprocess.check_output([make, "-s", "-f", "Makefile", "-f", str(probe), "stack-flags"], text=True)
    metadata["flags"] = dict(line.removeprefix("STACK_").split("=", 1)
                             for line in output.splitlines() if line.startswith("STACK_"))
    if ("-mlra" in metadata["flags"]["CPUFLAGS"].split()) != (args.lra == "on"):
        raise ValueError("effective allocator flags do not match requested profile")
    image = "ptos256us.img" if preset == "atari256" else "ptos512k.img"
    shutil.copy2(image, args.out / "system.img")
    shutil.copy2("pbench.prg", args.out / "PBENCH.PRG")
    write_json(args.out / "metadata.json", metadata)


def run_case(args, metadata, mode, blitter):
    dest = args.out / (mode + "-" + blitter)
    dest.mkdir()
    drive = dest / "drive"
    drive.mkdir()
    shutil.copy2(args.out / "PBENCH.PRG", drive / "PBENCH.PRG")
    (drive / "PBENCH.CFG").write_bytes(b"-q -r 1\r\n")
    # Reuse pBench's isolation of the user's Hatari disk configuration.
    spec = importlib.util.spec_from_file_location("pbench_hatari", ROOT / "tools/pBench/run-hatari.py")
    pbench_hatari = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pbench_hatari)
    cfg = dest / "hatari.cfg"
    cfg.write_text(pbench_hatari.CONFIG + "\n")
    shots = dest / "shots"
    shots.mkdir()
    log_path = dest / "emulator.log"
    # Keep AF_UNIX paths short even in long checkout/artifact directories.
    with tempfile.TemporaryDirectory(prefix="ptos-stack-") as temporary:
        endpoint = str(Path(temporary) / "control.sock")
        with socket.socket(socket.AF_UNIX) as server:
            server.bind(endpoint)
            server.listen(1)
            server.settimeout(min(10, args.timeout))
            command = [args.hatari, "--configfile", str(cfg), "--tos", str(args.out / "system.img"),
                       "--machine", "ste" if blitter == "on" else "st", "--blitter", blitter,
                       "--memsize", "4", "--sound", "off", "--tos-res", mode,
                       "--monitor", "mono" if mode == "high" else "rgb", "--fast-forward", "on",
                       "--fastfdc", "on", "--natfeats", "on", "--control-socket", endpoint,
                       "--auto", "C:\\PBENCH.PRG", "--screenshot-dir", str(shots), str(drive)]
            write_json(dest / "command.json", command)
            with log_path.open("w") as log:
                proc = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                        env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy"))
                deadline = time.monotonic() + args.timeout

                def alive():
                    if proc.poll() is not None:
                        raise RuntimeError("Hatari exited unexpectedly: " + str(proc.returncode))
                    if time.monotonic() >= deadline:
                        raise TimeoutError("Hatari workload timed out")

                try:
                    connection, _ = server.accept()
                    with connection:
                        connection.settimeout(2)

                        def send(value):
                            alive()
                            connection.sendall((value + "\n").encode())

                        def event(value):
                            send("hatari-event " + value)
                            time.sleep(0.15)

                        result = drive / "PBENCH.TXT"
                        while not (result.exists() and pbench_complete(result.read_text(errors="replace"), mode)):
                            alive()
                            time.sleep(0.25)
                        time.sleep(1)  # PBENCH.TXT is written just before appl_exit/Pterm.
                        send("hatari-shortcut screenshot")
                        # About prints the desktop marker on entry. Re-entering
                        # it a third time proves two complete dialog cycles.
                        count = 0
                        for attempt in range(12):
                            event("mousemove -10000 -10000")
                            event("mousemove 20 " + ("8" if mode == "high" else "4"))
                            time.sleep(0.25)
                            event("mousemove 20 " + ("16" if mode == "high" else "8"))
                            event("leftdown")
                            event("leftup")
                            time.sleep(0.3)
                            count = len(DESKTOP.findall(log_path.read_text(errors="replace")))
                            send("hatari-shortcut screenshot")
                            event("keypress 28")
                            time.sleep(0.5)
                            if count >= 3:
                                break
                        if count < 3:
                            raise RuntimeError("desktop About workload did not complete")
                        alive()
                        send("hatari-shortcut screenshot")
                        time.sleep(0.2)
                        alive()
                finally:
                    proc.terminate()
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait()
    # Only the controller writes this, after checking the emulator remained
    # alive through the workload. No marker is written on a timeout/crash.
    with log_path.open("a") as log:
        log.write("\n" + COMPLETED + "\n")
    return analyze(log_path.read_text(errors="replace"), result.read_text(errors="replace"), metadata, mode)


def summary(metadata, results):
    lines = ["# Stack reserve: " + metadata["profile"] + ", LRA " + metadata["lra"], "",
             "SDK pin: " + str(metadata.get("sdk_pin", "unknown")),
             "Compiler: " + str(metadata.get("compiler", "unknown")), "",
             "| Case | Stack | Allocation B | Peak B | Remaining B | Reserve B | Result |",
             "| --- | --- | ---: | ---: | ---: | ---: | --- |"]
    for case, result in results.items():
        for name, stack in result.get("stacks", {}).items():
            lines.append(f"| {case} | {name} | {stack['allocated_bytes']} | {stack['peak_bytes']} | "
                         f"{stack['remaining_bytes']} | {stack['reserve_bytes']} | {stack['status']} |")
        if "error" in result:
            lines += ["", case + ": " + result["error"].replace("\n", " ")]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", required=True, choices=PROFILES)
    parser.add_argument("--lra", required=True, choices=("on", "off"))
    parser.add_argument("--coverage", choices=("pr", "full"), default="pr")
    parser.add_argument("--out", type=Path, required=True, help="new artifact directory; use a dedicated checkout")
    parser.add_argument("--hatari", default="hatari")
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if args.timeout <= 0 or args.jobs <= 0:
        parser.error("timeout and jobs must be positive")
    os.chdir(ROOT)
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    cases = PR_CASES if args.coverage == "pr" else FULL_CASES
    metadata = dict(profile=args.profile, lra=args.lra, coverage=args.coverage, cases=list(cases))
    results = {}
    original = (ROOT / ".config").read_bytes() if (ROOT / ".config").exists() else None
    try:
        build(args, metadata)
        version = subprocess.run([args.hatari, "--version"], capture_output=True, text=True)
        metadata["hatari"] = (version.stdout + version.stderr).strip()
        write_json(args.out / "metadata.json", metadata)
        for mode, blitter in cases:
            key = mode + "-" + blitter
            try:
                result = run_case(args, metadata, mode, blitter)
            except (OSError, ValueError, KeyError, RuntimeError) as error:
                result = dict(status="fail", error=str(error))
            results[key] = result
            write_json(args.out / key / "result.json", result)
            print(key + ": " + "\n".join(report_lines(result)), flush=True)
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.CalledProcessError) as error:
        results["build"] = dict(status="fail", error=str(error))
    finally:
        if original is not None:
            (ROOT / ".config").write_bytes(original)
        else:
            (ROOT / ".config").unlink(missing_ok=True)
        # Restored configurations must be regenerated on the next make.
        (ROOT / "obj/auto.conf").unlink(missing_ok=True)
        (ROOT / "obj/autoconf.h").unlink(missing_ok=True)
        write_json(args.out / "metadata.json", metadata)
        write_json(args.out / "results.json", results)
        report = summary(metadata, results)
        (args.out / "summary.md").write_text(report)
        print(report)
        if os.environ.get("GITHUB_STEP_SUMMARY"):
            with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as output:
                output.write(report)
    return int(len(results) != len(cases) or any(r["status"] != "pass" for r in results.values()))


if __name__ == "__main__":
    raise SystemExit(main())
