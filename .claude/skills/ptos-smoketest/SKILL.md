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
| `virt-arm-cli_defconfig` | qemu-system-arm | `virt-arm.elf` | same as `virt-arm_defconfig`, but boots to EmuCON, not the desktop |
| `virt-m68k-cli_defconfig` | qemu-system-m68k | `virt-m68k.elf` | same as `virt-m68k_defconfig`, but boots to EmuCON, not the desktop |

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

> **False alarms to expect (benign, verified on master and all branches):**
> - Two `WARN : Bus Error reading at address $4fffff` / `$cc03c3, PC=$e00c20
>   op_e3=4a10` lines print at every STE boot. They are memory probes in OS
>   init (`TST.B (A0)`), not a hang. Ignore them.
> - The old green-checkerboard metric is unreliable twice over: it
>   false-negatives on mono/non-checker palettes (see above), and a step-4
>   sampling grid (`x%4==0,y%4==0`) can phase-miss even a colour checkerboard
>   when it is offset by one pixel — a fully rendered desktop then reports
>   `green = 0`. The menu-bar/status-bar detector below avoids both traps.

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
  image, so the desktop detector above must NOT be used as the pass signal. Instead, the cartridge runs its diagnostics and renders a **text
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
copy. Required invocations:

```sh
# raspi1 — only the A+ exists as a QEMU machine (`raspi1ap`); there is no Pi 1
# Model B machine. Same BCM2835/ARM1176 silicon, boots to the desktop. Requires
# the FPU-enable fix (#98) — without it the first vldr in _raspi_vcmem_init traps.
make rpi1_defconfig && make
qemu-system-arm -M raspi1ap -bios kernel.img -device usb-mouse -device usb-kbd \
  -d guest_errors -serial stdio

# raspi2 — real video output like raspi1; reads KDEBUG on the serial console.
# NOTE: the machine is `raspi2b` on QEMU >= 9 (`-M raspi2` is gone). Requires the
# FPU-enable fix (#98) — without it the first vldr in _raspi_vcmem_init traps.
make rpi2_defconfig && make
qemu-system-arm -M raspi2b -bios kernel7.img -device usb-mouse -device usb-kbd \
  -d guest_errors -serial stdio

# virt-arm (headless, no framebuffer)
# REQUIRED: -M virt,highmem=off — pTOS has no support for accessing memory or
# MMIO above the 4 GiB boundary yet; this also keeps ECAM at the low
# 0x3f000000 address the PCI backend implements.
make virt-arm_defconfig && make
qemu-system-arm -M virt,highmem=off -cpu cortex-a7 -m 128 -kernel virt-arm.elf -d guest_errors -serial stdio

# virt-m68k (headless) — MUST use -cpu m68020: the 68040 CACR probe hits a fatal QEMU bug
make virt-m68k_defconfig && make
qemu-system-m68k -M virt -m 128 -cpu m68020 -kernel virt-m68k.elf -d guest_errors -serial stdio

# virt-arm-cli / virt-m68k-cli — same images/invocations as above, but boot to
# EmuCON instead of the desktop (CONF_WITH_AES=n). Fastest smoke check: no AVI,
# no framebuffer, just text on -serial stdio. Console input is implemented
# (polling, see pass-signal notes below) but interactive verification in a
# sandboxed shell may be unreliable for environment reasons unrelated to the
# driver -- check with strace before assuming it's broken.
make virt-arm-cli_defconfig && make
qemu-system-arm -M virt,highmem=off -cpu cortex-a7 -m 128 -kernel virt-arm.elf -d guest_errors -serial stdio
make virt-m68k-cli_defconfig && make
qemu-system-m68k -M virt -m 128 -cpu m68020 -kernel virt-m68k.elf -d guest_errors -serial stdio
```

### No `raspi3b`/`raspi4b` QEMU target for pTOS's 32-bit ARM builds

pTOS's Raspberry Pi builds (`rpi1_defconfig`, `rpi2_defconfig`, ...) are
AArch32 (`arm-none-eabi-`, `kernel.img`/`kernel7.img`). QEMU's `raspi3b` and
`raspi4b` machines (`qemu-system-aarch64`) are **AArch64-only and cannot boot
them** — this is a fixed QEMU limitation, not a pTOS gap or a flag to
discover:

- Each raspi machine's board-revision code hardcodes an SoC/CPU type at the
  `TypeInfo` level (`raspi2b` → BCM2836/cortex-a7, `arm_machine_interfaces`;
  `raspi3b`/`raspi4b` → BCM2837/BCM2711, cortex-a53/a72,
  `aarch64_machine_interfaces`) in `hw/arm/raspi.c`. There is no property,
  CLI flag, or kernel-header autodetection that switches a raspi3b/raspi4b
  instance into AArch32 boot.
- On real hardware this is `start.elf`'s job: it picks 32- vs 64-bit mode
  from `config.txt` (`arm_64bit`) and which of `kernel.img`/`kernel7.img`/
  `kernel8.img` is present, then sets up the CPU reset state accordingly.
  QEMU doesn't emulate the GPU/`start.elf` boot ROM, and a raw `kernel7.img`
  has no header QEMU could use to autodetect its bitness anyway.
- **Practical stand-in**: `qemu-system-arm -M raspi2b -bios kernel7.img`
  (`rpi2_defconfig`, already in the table above) is the closest available
  smoke test for 32-bit-mode Pi 2/3 behavior — real Pi 2 and Pi 3 boot the
  same `kernel7.img` in AArch32 mode, so raspi2b under QEMU is a reasonable
  proxy even when the thing you actually care about is Pi 3 hardware.
- If pTOS ever grows an AArch64 port (producing a `kernel8.img`), *that*
  build could target `-M raspi3b`/`raspi4b` — but that doesn't exist today.

`-device usb-mouse -device usb-kbd` is mandatory when validating USB HID
input: without these devices, the class drivers register but neither a mouse
nor keyboard is enumerated.

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

- **raspi1** (booted with `-M raspi1ap`): no `guest_errors`; the screen reaches
  the normal boot/desktop path; and, with `-device usb-mouse -device usb-kbd`,
  serial traces confirm both mouse and keyboard HID probes.
- **raspi2** (booted with `-M raspi2b`): no `guest_errors`; the screen reaches
  the normal boot/desktop path; and, with `-device usb-mouse -device usb-kbd`,
  serial traces confirm both mouse and keyboard HID probes.
- The `vdi_v_opnwk: mode layout=... bpp=...` KDEBUG (and its `backend=selected`
  variant) only prints when the VDI runtime dispatcher is built in (both
  renderers enabled, `CONF_WITH_VDI_BACKEND_DISPATCH`, e.g.
  `rpi2-sparse_defconfig`) — default single-renderer rpi1/rpi2 builds have no
  dispatcher and print no such line. For Raspberry Pi HID validation, the
  reliable pass signal is no `guest_errors`, the screen reaching the normal
  boot/desktop path, and mouse and keyboard HID probe traces.
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
- **virt-arm-cli / virt-m68k-cli** (`CONF_WITH_AES=n`, boots straight to
  EmuCON instead of the desktop — for a fast dev-loop check that avoids
  AVI/frame analysis entirely): pass signal is `Welcome to EmuCON2 version
  ...` followed by the `A:>` prompt appearing in the `-serial stdio` output,
  with no `guest_errors`/`unimp` beyond the same benign m68k `_detect_fpu`
  entry noted above.
  - **Console input**: `CONF_SERIAL_CONSOLE` input injection
    (`push_ascii_ikbdiorec`) is wired up for both machines by *polling* the
    UART/TTY from the existing 200 Hz periodic tick
    (`virt_uart0_poll_rx()` from `virt_timer_tick()`;
    `goldfish_tty_poll_rx()` from `goldfish_rtc_service()`) rather than a
    dedicated RX interrupt — deliberately, mirroring how ColdFire's is the
    only machine with a real RX-interrupt hookup
    (`coldfire_rs232_enable_interrupt()`). This also fixed a real pre-existing
    bug: virt-arm's PL011 init never set `LCRH.FEN`, so the UART never
    actually ran in FIFO mode despite the code comment claiming it did.
  - **Verifying input interactively is unreliable in a sandboxed/CI
    shell.** `strace -f -e trace=read,poll,ppoll` on a spawned
    `qemu-system-arm -M virt ... -serial stdio` process, in at least one
    such environment, showed `ppoll()` repeatedly reporting `fd=0`
    (stdin) as `POLLIN`-ready — then QEMU never issued the matching
    `read(0, ...)` before the fd hit `POLLHUP` and was dropped from the
    poll set entirely. TX was unaffected (verified extensively: boot
    banner, EmuCON prompt, etc. all render correctly), and QEMU's own
    monitor chardev on the same spawned process consumed typed input
    correctly in the same session — so this looks like a QEMU/host-pty
    interaction gap specific to the guest-UART chardev path in that
    container, not a general "no ptys here" limitation, and not something
    the pTOS-side driver can work around. **Do not conclude the feature is
    broken from a failed automated keystroke test alone** — first confirm
    with `strace` (or by testing from a real interactive terminal) whether
    the environment is actually delivering bytes to QEMU's serial chardev
    at all before suspecting the driver.

## Common mistakes

| Mistake | Fix |
|---|---|
| Verifying boot with Hatari breakpoints | Use AVI frame analysis or `--trace` |
| Measuring wall-clock under `--parse` | `--parse` is full-speed; only `--run-vbls` is paced |
| Falcon boot looks hung | 31 s `ide_init` wait — attach `--ide-master`, use STE, or drop `CONF_WITH_IDE` |
| `qemu-system-m68k` with default CPU | always `-cpu m68020` |
| `-M virt` without `highmem=off` | use `-M virt,highmem=off` — no >4 GiB access support |
| Expecting video from virt-arm/virt-m68k | headless; check serial + guest_errors only |
| Trying `qemu-system-aarch64 -M raspi3b -bios kernel7.img` for a 32-bit pTOS build | `raspi3b`/`raspi4b` are AArch64-only in QEMU, no override exists; use `raspi2b` under `qemu-system-arm` instead |
| Desktop detector says `green = 0` on a booted STE | Sampling grid was phase-sensitive (e.g. step 4) and missed the phase-offset checkerboard; re-check with the step-1 snippet above |
| STE boot prints two `Bus Error reading at address $4fffff/$cc03c3` warnings | Benign init probes at `PC=$e00c20` (`TST.B (A0)`); present on every boot, ignore |
| Testing `ptoscart.img` as the `--tos` image | It is a cartridge, not a TOS: pass it via `--cartridge` and supply a real Atari TOS ROM with `--tos` (pTOS ROMs have cartridge detection compiled out) |
| Expecting a desktop from `ptoscart.img` | The 128 KB cartridge excludes the AES/desktop; pass signal is the rendered diagnostic text screen, not the checkerboard |
| A scripted keystroke at the `virt-arm-cli`/`virt-m68k-cli` EmuCON prompt over `-serial stdio` gets no response | Input is implemented (polled from the 200 Hz tick), but some sandboxed shells never deliver the bytes to QEMU's serial chardev at all (`ppoll()` sees stdin `POLLIN` but QEMU never `read()`s it) — confirm with `strace` before assuming the driver is broken; reaching the `A:>` prompt alone is still a valid automated pass signal either way |
