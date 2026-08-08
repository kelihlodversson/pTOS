---
name: ptos-smoketest
description: Use when smoke-testing or verifying that a built pTOS (Portable EmuTOS) image boots under an emulator. Covers Hatari for m68k Atari targets (atari512/STE/Falcon/TT configs) and QEMU for the raspi1 (QEMU machine `raspi1ap`), raspi2 (QEMU machine `raspi2b`), virt-arm and virt-m68k machines. Use when asked to boot a pTOS build, check it reaches the GEM desktop, diagnose a slow/hung boot, or when you need emulator invocations, --run-vbls/--avirecord/--trace flags, the Hatari debugger gotchas (spurious breakpoints, echo crash), or the Falcon IDE 31s boot wait.
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

Record an AVI and inspect frames; the pass signal is the GEM desktop rendering
(EmuTOS default background pattern, a white+green 2×2 checkerboard
`IP_4PATT`, plus floppy/hard-drive icons).

```sh
hatari --tos ptos512k.img --machine ste --memsize 4 --sound off \
  --avirecord --avi-vcodec png --avi-file /tmp/boot.avi --run-vbls 1200
```

Analyze with python3 + PIL (PIL is installed; `ffmpeg`/`scrot`/`import` are
NOT). Extract frames and scan for the desktop:

> **False alarms to expect (benign, verified on master and all branches):**
> - Two `WARN : Bus Error reading at address $4fffff` / `$cc03c3, PC=$e00c20
>   op_e3=4a10` lines print at every STE boot. They are memory probes in OS
>   init (`TST.B (A0)`), not a hang. Ignore them.
> - The `green = 0` result from the OLD step-4 sampling grid (`range(0,460,4)`
>   × `range(0,640,4)`) was a **false negative**: it only samples pixels with
>   `x%4==0,y%4==0`, which lands on a single phase of the 2×2 checkerboard and
>   misses the pattern entirely when it is offset by one pixel. The detector
>   below uses step 1 for this reason. A `green` count in the tens of thousands
>   (e.g. ~100k) at step 1 is the normal desktop, not a rendering bug.

```python
import re, io
from PIL import Image
d = open('/tmp/boot.avi','rb').read()
starts = [m.start() for m in re.finditer(b'\x89PNG\r\n\x1a\n', d)]
frames = [d[s:e] for s,e in zip(starts, starts[1:]+[len(d)])]
# green checkerboard + icons above the bottom status bar = desktop
# IMPORTANT: sample every pixel (step 1). A coarser grid (e.g. step 4,
# x%4==0,y%4==0) systematically misses the WHOLE checkerboard when the
# 2x2 IP_4PATT pattern is phase-offset by one pixel, giving a false
# 'not booted yet' on a desktop that is fully rendered.
px = Image.open(io.BytesIO(frames[-1])).convert('RGB').load()
green = sum(1 for y in range(0,460) for x in range(0,640)
            if px[x,y][1] > 200 and px[x,y][0] < 80)
print('desktop' if green > 1000 else 'not booted yet')
```

### Cartridge smoke test (ptoscart.img)

`cartridge_defconfig` builds a **128 KB Atari diagnostic cartridge**
(`ptoscart.img`, `TARGET_CART`), mapped at `0x00fa0000` with the diagnostic
magic `0xfa52235f`. It is a diagnostic image, NOT a full OS: it is limited to
128 KB, so **the AES and the desktop cannot be included** (`Kconfig.image`).

- **It needs a separate machine TOS ROM.** Hatari requires `--tos <file>` and
  pTOS's own ROM images cannot run the cartridge: pTOS has cartridge detection
  compiled out (`CONF_WITH_CARTRIDGE` is fixed to 0), so a pTOS `--tos` never
  checks for `$fa52235f` and the cartridge is ignored. Use a real Atari TOS
  (e.g. TOS 2.06 for the STE) as the machine TOS:
  ```sh
  make cartridge_defconfig && make
  hatari --tos /path/to/tos206us.img --cartridge ptoscart.img \
    --machine ste --memsize 4 --sound off \
    --avirecord --avi-vcodec png --avi-file /tmp/cart.avi --run-vbls 900
  ```
- **It does NOT boot to the desktop** — the GEM desktop does not exist in this
  image. The green-checkerboard detector above must NOT be used as the pass
  signal. Instead, the cartridge runs its diagnostics and renders a **text
  screen** (light-grey `238,238,238` background with black character glyphs).
  Pass = the last frame shows thousands of dark text pixels:
  ```python
  px = Image.open(io.BytesIO(frames[-1])).convert('RGB').load()
  dark = sum(1 for y in range(0,588) for x in range(0,832)
             if px[x,y][0] < 100)
  print('diag text screen' if dark > 1000 else 'not rendered')
  ```
  (Measured on a TOS 2.06/STE boot: ~3-8k dark pixels on the stable text
  screen. Frame 0 is the uniform-black power-on state — ignore it; the text
  is up by ~VBL 300. The `WARN : Bus Error reading at address $4fffff,
  PC=$fa05ca op_e3=4a10` line is the cartridge probing past the 4 MB RAM —
  benign, ignore.)

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
| Desktop detector says `green = 0` on a booted STE | Sampling grid was phase-sensitive (e.g. step 4) and missed the phase-offset checkerboard; re-check with the step-1 snippet above |
| STE boot prints two `Bus Error reading at address $4fffff/$cc03c3` warnings | Benign init probes at `PC=$e00c20` (`TST.B (A0)`); present on every boot, ignore |
| Testing `ptoscart.img` as the `--tos` image | It is a cartridge, not a TOS: pass it via `--cartridge` and supply a real Atari TOS ROM with `--tos` (pTOS ROMs have cartridge detection compiled out) |
| Expecting a desktop from `ptoscart.img` | The 128 KB cartridge excludes the AES/desktop; pass signal is the rendered diagnostic text screen, not the checkerboard |
