---
name: ptos-smoketest
description: Use when smoke-testing or verifying that a built pTOS (Portable EmuTOS) image boots under an emulator. Covers Hatari for m68k Atari targets (atari512/STE/Falcon/TT configs) and QEMU for the raspi1 (QEMU machine `raspi1ap`), raspi2 (QEMU machine `raspi2b`), virt-arm and virt-m68k machines. Use when asked to boot a pTOS build, check it reaches the GEM desktop, diagnose a slow/hung boot, or when you need emulator invocations, --run-vbls/--avirecord/--trace flags, the Hatari debugger gotchas (spurious breakpoints, echo crash), the Falcon IDE 31s boot wait, or the floppy motor/deselection timeouts (motor on/off 1.5-3s + deselect 5s = ~20s STE baseline).
---

# pTOS Smoke Testing

Verify a built pTOS image boots under an emulator. This is a reference skill:
every command here has been run and verified against Hatari v2.5.0 (typically
`/usr/bin/hatari`) on the pTOS tree. Do not rely on the Hatari debugger for
pass/fail (it is unreliable — see Gotchas).

## Build the image first

`configs/*_defconfig` → `make <config>_defconfig && make` (see `CLAUDE.md` in
the pTOS tree). Relevant outputs:

| Config | Emulator | Image | Machine |
|---|---|---|---|
| `atari512_defconfig` | hatari | `ptos512k.img` (+ `.sym` `.map`) | `--machine ste`, `--machine falcon`, `--machine tt` |
| `rpi1_defconfig` | qemu-system-arm | `kernel.img` | `-M raspi1ap` (video + serial) |
| `rpi2_defconfig` | qemu-system-arm | `kernel7.img` | `-M raspi2b` (video + serial) |
| `virt-arm_defconfig` | qemu-system-arm | `virt-arm.elf` | `-M virt,highmem=off -cpu cortex-a7` (headless) |
| `virt-m68k_defconfig` | qemu-system-m68k | `virt-m68k.elf` | `-M virt -cpu m68020` (headless) |

Atari configs build with the default mintelf toolchain (`m68k-atari-mintelf-`)
and produce symbols in `ptos512k.sym` (load in the Hatari debugger with
`symbols <file>`; there is no `--symbols` CLI option). The virt-m68k image is
also ELF, since QEMU's `-kernel` loader needs an `elf32-m68k`-format kernel.

## Hatari smoke test (m68k Atari)

`--run-vbls N` paces emulation at ~real time. At 50 Hz VBL, 3000 VBLs is ~60 s
of emulated time; expect somewhat more wall time due to Hatari overhead. The
debugger `--parse` mode runs at full speed and is NOT paced.

### Boot-completion check (RELIABLE — use this)

Record an AVI and inspect frames; the pass signal is the GEM desktop
rendering. The default EmuTOS desktop uses the `IP_4PATT` background
(a white+green 2×2 checkerboard on color machines, plain light-gray
on ST/STE mono), a menu bar across the top, and floppy/hard-drive
icons above the bottom status bar.

```sh
hatari --tos ptos512k.img --machine ste --memsize 4 --sound off \
  --avirecord --avi-vcodec png --avi-file /tmp/boot.avi --run-vbls 1200
```

Analyze with python3 + PIL (PIL is installed; `ffmpeg`/`scrot`/`import` are
NOT). Extract frames and scan for the desktop. A green-only checkerboard
test is **unreliable**: it false-negatives on ST/STE (mono or non-checker
palettes). Check for the menu bar text (black pixels row near the top)
*and* the status bar (light-gray row near the bottom) instead — both
present means the desktop drew:

```python
import re, io
from PIL import Image
d = open('/tmp/boot.avi','rb').read()
starts = [m.start() for m in re.finditer(b'\x89PNG\r\n\x1a\n', d)]
frames = [d[s:e] for s,e in zip(starts, starts[1:]+[len(d)])]
w,h = Image.open(io.BytesIO(frames[-1])).size
px = Image.open(io.BytesIO(frames[-1])).convert('RGB').load()
# menu bar: a text-like black row in the top 20% (50 < count < half-width,
# so a solid black boot splash does not false-positive)
menu = 0
for y in range(0, int(h*0.20), 2):
    blk = sum(1 for x in range(0,w,2) if px[x,y] == (0,0,0))
    if 50 < blk < w//2:
        menu = blk
        break
# status bar: a light-gray band (>= 80% width) somewhere in the bottom 15%
status = False
for y in range(int(h*0.85), h, 2):
    gray = sum(1 for x in range(0,w,4)
               if px[x,y] == (192,192,192))
    if gray > w//4 * 0.8:
        status = True
        break
print('desktop' if menu and status else 'not booted yet',
      '(menu=%d)' % menu)
```

### Measured boot times (atari512 image, this machine)

| Machine | Desktop at | Wall (AVI, ~10 fps) |
|---|---|---|
| STE (`--machine ste --memsize 4`) | ~VBL 954 (~19 s) | ~95 s |
| Falcon + `--ide-master ide.img` | ~VBL 876 (~17.5 s) | ~137 s |
| Falcon, no IDE image | ~VBL 2426 (~48.5 s) | — |

A plain run without AVI is closer to real time; 3000 VBLs ≈ 76 s wall.

### Falcon IDE 31 s wait (the common "slow boot")

`CONF_WITH_IDE` probes live Falcon IDE registers that Hatari responds to, so
`detect_ide()` finds a controller, then `ide_init()` waits
`LONG_TIMEOUT` = 31 s (`bios/ide.c:269`) for a drive that is not attached.
Fix any one of: attach `--ide-master <any file>` (a raw file works as an ATA
disk), use `--machine ste`, or build without `CONF_WITH_IDE`. This is NOT the
floppy drive; that part of boot is the ~17-19 s baseline on both machines.

### Floppy motor / deselection timeouts

The ST FDC has no explicit "drive present" bit; `flop_detect_drive()`
(`bios/floppy.c:389`) infers a drive from the `TRACK0` status line and is fast
(~ms). The waits that show up in the boot timeline come from the motor and
deselection timers (`bios/floppy.c:156-160`), all in `_hz_200` ticks:

- `MOTORON_TIMEOUT`  = 1.5 s  — per-FDC-command wait when the motor is already on.
- `MOTOROFF_TIMEOUT` = 3.0 s  — per-FDC-command wait when the motor is off (adds spinup).
- `DESELECT_TIMEOUT` = 5.0 s  — `flopunlk()` schedules drive deselection this far in the future; `flopvbl()` (every 8th VBL) performs it.

With no disk inserted these add up to the ~17-19 s STE baseline alongside GEM
initialisation; they are not a single "stuck" wait like the Falcon IDE one, so
a boot that takes ~20 s on STE with no floppy image attached is normal.

### Traces

```sh
hatari --tos ptos512k.img --machine ste --memsize 4 --trace cpu_disasm --run-vbls 50
hatari --tos ptos512k.img --machine falcon --memsize 14 --trace videl --run-vbls 1600
```

`cpu_disasm` is an instruction trace (dominant loops reveal where boot spends
time; e.g. the 31 s IDE wait polls `_hz_200` at `0x4ba`, `addq.l #1,$4ba` at
`0xE00894`). `videl` shows video mode changes (e.g. `640x480@4`, screen base
`0xDB5000`). Traces are slow: ~2.5M instructions by VBL 50.

### Hatari v2.5.0 debugger gotchas (verified)

- **Breakpoints are unreliable**: every `b pc = <addr>` and `b VBL = <n>`
  breakpoint fires spuriously at reset; `cpureg` always shows the all-zero
  reset state. Do NOT use breakpoint-milestone checks to verify boot.
- **Memory reads lie**: a value that the instruction trace shows being stored
  (e.g. `move.l #$752019F3,$420` = memvalid magic) can read back as 0; GEMDOS
  cookies always read empty.
- **`echo` in a debugger `.ini` crashes Hatari** (`Str_UnEscape: Assertion
  's2 < s1'`, rc=134, core dump). Never use `echo` in debugger scripts.
- No `info breakpoints` command; list breakpoints with bare `b`.
- Trust only: `--run-vbls` + `--trace ...` output and AVI frames.

## QEMU smoke test (raspi1 / raspi2 / virt-arm / virt-m68k)

The pTOS `readme.md` is the user-facing source; this skill is the agent-facing
copy. Invocations (verified in-tree):

```sh
# raspi1 — only the A+ exists as a QEMU machine (`raspi1ap`); there is no Pi 1
# Model B machine. Same BCM2835/ARM1176 silicon, boots to the desktop. Requires
# the FPU-enable fix (#98) — without it the first vldr in _raspi_vcmem_init traps.
make rpi1_defconfig && make
qemu-system-arm -M raspi1ap -bios kernel.img -d guest_errors -serial stdio

# raspi2 — real video output like raspi1; reads KDEBUG on the serial console.
# NOTE: the machine is `raspi2b` on QEMU >= 9 (`-M raspi2` is gone). Requires the
# FPU-enable fix (#98) — without it the first vldr in _raspi_vcmem_init traps.
make rpi2_defconfig && make
qemu-system-arm -M raspi2b -bios kernel7.img -d guest_errors -serial stdio

# virt-arm (headless, no framebuffer)
# REQUIRED: -M virt,highmem=off — pTOS has no support for accessing memory or
# MMIO above the 4 GiB boundary yet; this also keeps ECAM at the low
# 0x3f000000 address the PCI backend implements.
make virt-arm_defconfig && make
qemu-system-arm -M virt,highmem=off -cpu cortex-a7 -m 128 -kernel virt-arm.elf -d guest_errors -serial stdio

# virt-m68k (headless) — MUST use -cpu m68020: the 68040 CACR probe hits a fatal QEMU bug
make virt-m68k_defconfig && make
qemu-system-m68k -M virt -m 128 -cpu m68020 -kernel virt-m68k.elf -d guest_errors -serial stdio
```

Device variants (both virt ports; `force-legacy=false` is required on QEMU
versions that default virtio-mmio to legacy v1):

```sh
  -global virtio-mmio.force-legacy=false \
  -drive file=disk.img,if=none,format=raw,id=hd0 -device virtio-blk-device,drive=hd0
  # or keyboard/tablet input:
  -global virtio-mmio.force-legacy=false \
  -device virtio-keyboard-device -device virtio-tablet-device
```

Useful flags: `-S -s` (remote gdb), `-serial file:path.log` (capture console),
`-monitor stdio` + `sendkey a` (keyboard injection), `-display none`
(headless CI).

### Standard smoke pattern and pass signals

```sh
timeout 5 qemu-system-arm -M virt,highmem=off -cpu cortex-a7 -m 128 -kernel virt-arm.elf \
  -d guest_errors,unimp -D /tmp/qemu.log -display none -serial stdio
cat /tmp/qemu.log
```

- **raspi1** (booted with `-M raspi1ap`): serial KDEBUG shows `vdi_v_opnwk: mode layout=1 color_model=0
  bpp=8 backend=none` (later `color_model=1 ... backend=selected`); no
  `guest_errors`; screen draws.
- **raspi2** (booted with `-M raspi2b`): serial KDEBUG shows `vdi_v_opnwk: mode layout=1 color_model=0
  bpp=8 backend=none` (later `color_model=1 ... backend=selected`); no
  `guest_errors`; screen draws.
- **virt-arm / virt-m68k**: process survives the full `timeout` window
  (rc=124), no `guest_errors`/`unimp` output beyond ONE benign `Illegal
  Instruction` entry on m68k from `_detect_fpu` (expected, means CPU
  detection completed). Boot silently reaches the AES `evnt_multi()` event
  loop and idles — that idle is the PASS signal, not a hang. More than one
  entry, an `unimp` entry, or early exit = failure.
- Stock builds print (unconditional, on the console): `VDI video mode = ...`,
  `AES: EMUDESK: appl_init()`, `AES: EMUDESK: evnt_multi()`.
- Known issue: `_hz_200` may stall on virt targets (shared AES/gemdisp.c
  IRQ-mask bug, affects raspi too) — a timer that fires correctly at least
  once is the v1 bar.

## Common mistakes

| Mistake | Fix |
|---|---|
| Verifying boot with Hatari breakpoints | Use AVI frame analysis or `--trace` |
| Measuring wall-clock under `--parse` | `--parse` is full-speed; only `--run-vbls` is paced |
| Falcon boot looks hung | 31 s `ide_init` wait — attach `--ide-master`, use STE, or drop `CONF_WITH_IDE` |
| `qemu-system-m68k` with default CPU | always `-cpu m68020` |
| `-M virt` without `highmem=off` | use `-M virt,highmem=off` — no >4 GiB access support |
| Expecting video from virt-arm/virt-m68k | headless; check serial + guest_errors only |
