# m68k-virt Boot Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `virt-m68k` machine target so pTOS boots under `qemu-system-m68k -M virt`, reaching a running BIOS with a working serial console and a ticking periodic timer — the same v1 milestone the ARM virt port (#25) and Raspberry Pi port already reach.

**Architecture:** A new `bios/machine/virt-m68k/` directory, modeled on `bios/machine/virt-arm/`'s shape but with its own from-scratch `startup.S` (not derived from the Atari-only `bios/arch/m68k/startup.S`). Three Goldfish devices provide console (`goldfish_tty`), interrupt routing (`goldfish_pic`), and the periodic tick (`goldfish_rtc`), all at fixed physical addresses QEMU's `hw/m68k/virt.c` hard-codes. `tosvars.ld` and `emutos.ld` need **no changes at all** — `MACHINE_VIRT_M68K` falls into the existing classic-m68k branches of both (see the design doc's "resolved" section) because this board's RAM sits at physical `0x0`, exactly where every other m68k target already expects it.

**Tech Stack:** C90 (`-std=gnu90`), hand-written m68k assembly (GNU `as`, `-mshort` off — this is `ARCH_M68K_CLASSIC`, not ColdFire), Kconfig/Kbuild-style build system, `qemu-system-m68k -M virt` for smoke testing.

## Global Constraints

- C90 with GNU extensions; declarations at the top of a block.
- 4 spaces, never a hard tab, in `.c`/`.h`/`.S`. Run `make gitready` before committing.
- Use `portab.h` types (`WORD`, `LONG`, `UBYTE`, `UWORD`, `ULONG`, `BOOL`), not native C types, in new C code.
- Machine-specific code goes in `bios/machine/virt-m68k/`, not behind `#ifdef` in shared files, except where a shared file already has an established `#ifdef MACHINE_*`/`#elif defined(MACHINE_*)` chain (`bios/bios.c`, `bios/serport.c`) — extend those chains rather than inventing a new mechanism.
- New Kconfig options: plain `CONF_WITH_FOO` feature symbols are always defined (`0`/`1`, test with `#if`); `MACHINE_*`/`TARGET_*` are defined only when set (test with `#ifdef`). Never add per-target defaults to `include/config.h`.
- `obj-y` entries in `build.mk` are Kbuild style: `obj-$(SYMBOL) += file.o`.
- Source basenames must be unique across the whole tree (objects land flat in `obj/`).
- Reference doc: `docs/superpowers/specs/2026-07-30-qemu-virt-support-design.md`. GitHub issue: #26 (tracking issue #24). Branch: `m68k-virt/boot-support`, draft PR #36.

## Environment note: m68k toolchain fix already applied

Before Task 1's implementer could verify anything, the only installed m68k
toolchain in this environment (`m68k-atari-mintelf-gcc` 13.3.0 — the
default `m68k-atari-mint-gcc` a.out toolchain isn't installed here at all)
turned out to ICE (`internal compiler error: in print_operand_address, at
config/m68k/m68k.cc:5226`) compiling `bios/xbios.c`'s `supexec()`. This was
independently reproduced against the **pre-existing, unmodified**
`aranym_defconfig` target — no virt-m68k code involved — confirming it as
an environment/compiler defect, not something Task 1 introduced. Root
cause, isolated by a clobber-list bisection: the ICE is triggered
specifically by clobbering `a6` in `supexec()`'s fixed-register inline asm
(every other clobbered register is fine). The project always builds with
`-fomit-frame-pointer` (`Makefile`'s `OTHERFLAGS`), so `a6` is never used
as a dedicated frame pointer here, but GCC's m68k backend still ICEs on
this specific combination of a hard-register asm operand plus an `a6`
clobber during its "final" RTL pass.

Fix already committed (separately from this plan's 4 tasks, since it's an
unrelated pre-existing bug, not part of the virt-m68k feature): in
`bios/xbios.c`'s `supexec()`, `a6` is saved/restored by hand around the
`jsr` inside the asm block (`move.l a6,-(sp)` / `move.l (sp)+,a6`) and
removed from the clobber list. This is the textbook-correct way to protect
a register from a call when the compiler can't be told about it directly:
the physical register is provably restored to its original value
regardless of what the called code does to it, so removing it from the
clobber list changes nothing observable — it just stops relying on the
buggy code path. Verified by rebuilding `aranym_defconfig` (unrelated
existing target) end-to-end with the ELF toolchain after the fix: full
link succeeds, image produced, no ICE.

This fix must land before any task in this plan can be build-verified in
this environment. It does not change any virt-m68k-specific code or
behavior.

## Hardware reference (verified against QEMU source, not memory)

QEMU source read directly from `/home/freyr/qemu` (`hw/m68k/virt.c`, `hw/intc/goldfish_pic.c`, `hw/intc/m68k_irqc.c`, `hw/rtc/goldfish_rtc.c`, `hw/char/goldfish_tty.c`, `target/m68k/cpu.c`) to pin down every register offset and the interrupt wiring used below — nothing here is from general Goldfish-device recollection.

- **CPU reset state** (`hw/m68k/virt.c: main_cpu_reset`, `target/m68k/cpu.c: m68k_cpu_reset_hold`): only `pc` is set (to the ELF entry point); `a7`(sp) is **0**; `memset(env, 0, ...)` zeroes every other register including VBR and the cache/MMU control registers. `sr = SR_S | SR_I` (supervisor, interrupt mask 7 — all interrupts masked).
- **Goldfish PIC** (6 instances, `0xff000000` + `n*0x1000` for n=0..5): `STATUS`=`0x00` (RO, popcount of pending&enabled), `IRQ_PENDING`=`0x04` (RO), `IRQ_DISABLE_ALL`=`0x08` (W, any value disables all 32 lines and clears pending), `DISABLE`=`0x0c` (W, mask), `ENABLE`=`0x10` (W, mask).
- **m68k IRQ controller** (`hw/intc/m68k_irqc.c`): PIC index `i` (0-5) drives CPU autovector level `i+1`, vector `i+25`. So PIC index 5 ("PIC #6") → CPU level 6 → **vector 30** → RAM vector-table address `30*4 = 0x78`.
- **Goldfish RTC** (`0xff006000`, big-endian-configured but that only affects QEMU's internal bus decode — plain `volatile ULONG` access from m68k C code is correct as-is): `TIME_LOW`=`0x00` (R, latches `TIME_HIGH` as a side effect; W, sets low 32 bits, of a **nanosecond** counter), `TIME_HIGH`=`0x04`, `ALARM_LOW`=`0x08` (W — **writing this arms the alarm timer using whatever is currently in `ALARM_HIGH`**, so `ALARM_HIGH` must be written first), `ALARM_HIGH`=`0x0c`, `IRQ_ENABLED`=`0x10`, `CLEAR_ALARM`=`0x14`, `ALARM_STATUS`=`0x18`, `CLEAR_INTERRUPT`=`0x1c`. This is a one-shot compare-match timer, not a free-running periodic tick: every interrupt must clear (`CLEAR_INTERRUPT`) and re-arm (`ALARM_HIGH` then `ALARM_LOW` = now + 5ms) or it never fires again. QEMU wires the first RTC instance to PIC index 5 ("PIC #6"), bit 0.
- **Goldfish TTY** (`0xff008000`): `PUT_CHAR`=`0x00` (W, one byte, output immediately, no setup needed), `BYTES_READY`=`0x04` (R), `CMD`=`0x08` (W; `CMD_READ_BUFFER`=3 DMAs up to `DATA_LEN` bytes from the RX FIFO to `DATA_PTR`), `DATA_PTR`=`0x10`, `DATA_LEN`=`0x14`.
- **Existing shared-code hooks that need zero changes**, confirmed by reading them:
  - `bios/arch/m68k/vectors.S`'s `init_exc_vec`/`_int_timerc` are architecture-generic (no Atari-hardware assumptions) and already called from shared `bios/bios.c`/`bios/mfp.c`.
  - `bios/mfp.c: init_system_timer()` already does `#if !CONF_WITH_MFP: vector_5ms = int_timerc;` unconditionally — true for us since `CONF_WITH_MFP` depends on `CONF_ATARI_HARDWARE`, which we don't set.
  - `bios/bios.c`'s interrupt-unmasking (`set_sr(0x2000)`, since `CONF_WITH_ATARI_VIDEO` is off for us) and `bios/machine.c: machine_init()` (every branch inside is gated by `CONF_WITH_MFP`/`CONF_WITH_VIDEL`/etc., all off for us) are already no-ops for this machine.
  - `bios/Kconfig`'s `CONF_SERIAL_CONSOLE` already defaults to `y` when `!CONF_WITH_ATARI_VIDEO && !MACHINE_AMIGA` (true for us), and `Kconfig.debug`'s `RS232_DEBUG_PRINT` follows from that — so `KDEBUG`/`kprintf` output automatically routes to `bconout1()` with zero changes to `bios/kprint.c`.
  - `bios/bios.c: boot_status |= RS232_AVAILABLE` after `init_serport()` is unconditional, and `bios/serport.c`'s `init_serport()` needs no per-machine init call for the Goldfish TTY (the device needs no setup, unlike the PL011's baud-rate registers).

## File Structure

- `Kconfig.machine` (modify) — `MACHINE_VIRT_M68K`, `ARCH_M68K` default.
- `Kconfig.image` (modify) — `TARGET_VIRT_M68K_KERNEL`, `EMUTOS_LIVES_IN_RAM` default.
- `bios/Kconfig` (modify) — `CONF_WITH_GOLDFISH_TTY`.
- `Makefile` (modify) — `MACHINE-$(MACHINE_VIRT_M68K)`, image-default/build rule.
- `bios/build.mk` (modify) — new `obj-$(MACHINE_VIRT_M68K)` line.
- `bios/bios.c` (modify) — include + machine-init dispatch, mirroring the existing `MACHINE_VIRT_ARM` branch.
- `bios/serport.c` (modify) — include + 4 `#elif CONF_WITH_GOLDFISH_TTY` branches, mirroring the existing `CONF_WITH_VIRT_UART` branches.
- `configs/virt-m68k_defconfig` (create).
- `bios/machine/virt-m68k/startup.S` (create) — boot entry, OSHEADER, bss clear, `_phystop` set, jump to `_biosmain`.
- `bios/machine/virt-m68k/memory.c` (create) — empty placeholder, satisfies the unconditional `obj-y += memory.o`.
- `bios/machine/virt-m68k/goldfish_tty.c` / `.h` (create) — console driver.
- `bios/machine/virt-m68k/goldfish_pic.c` / `.h` (create) — interrupt routing.
- `bios/machine/virt-m68k/goldfish_rtc.c` / `.h` (create) — periodic tick, device-side logic.
- `bios/machine/virt-m68k/goldfish_rtc_isr.S` (create) — the actual CPU exception-vector entry point (must be hand assembly: it runs directly off the hardware autovector, register save/restore and `rte` are its job).
- `readme.md` (modify) — document the `qemu-system-m68k -M virt` invocation.

**Interfaces summary** (so later tasks match earlier ones exactly):
- `goldfish_pic_init(void)`, `goldfish_pic_enable(int pic, int bit)` — from `goldfish_pic.c`/`.h`.
- `goldfish_rtc_init(void)` (called once from `bios.c`), `goldfish_rtc_service(void)` (called only from `goldfish_rtc_isr.S`) — from `goldfish_rtc.c`/`.h`.
- `goldfish_rtc_isr(void)` — the asm entry point `goldfish_rtc.c` installs into the vector table; defined in `goldfish_rtc_isr.S`.
- `goldfish_tty_can_write(void)`/`goldfish_tty_write_byte(UBYTE)`/`goldfish_tty_can_read(void)`/`goldfish_tty_read_byte(void)` — from `goldfish_tty.c`/`.h`, called only from `bios/serport.c`.

---

## Task 1: Kconfig plumbing, build wiring, and minimal boot

**Files:**
- Modify: `Kconfig.machine`
- Modify: `Kconfig.image`
- Modify: `Makefile`
- Create: `configs/virt-m68k_defconfig`
- Create: `bios/machine/virt-m68k/startup.S`
- Create: `bios/machine/virt-m68k/memory.c`

**Interfaces:**
- Produces: a bootable ELF (`virt-m68k.elf`) that clears BSS, sets `_phystop`, and reaches `_biosmain`. No console output yet (Task 2), no working timer yet (Task 3) — `_biosmain` will run past `machine_init()` (a no-op for this machine, see reference section above) and hang inside `calibrate_delay()` waiting for `_hz_200` to change, since nothing drives the timer yet. That hang, with **no** `guest_errors`/`unimp` output, is this task's actual pass signal.

- [ ] **Step 1: Add `MACHINE_VIRT_M68K` to `Kconfig.machine`**

In the `choice prompt "Machine"` block, immediately after the existing `config MACHINE_VIRT_ARM` entry:

```kconfig
config MACHINE_VIRT_M68K
	bool "QEMU virt (m68k)"
	help
	  QEMU's generic m68k 'virt' board (qemu-system-m68k -M virt). No
	  Atari-compatible chipset; Goldfish PIC/RTC/TTY at fixed
	  addresses, boots a -kernel ELF directly. RAM starts at physical
	  0x0, same as every Atari/Amiga/ColdFire target, so no MMU
	  bootstrap trick is needed the way MACHINE_VIRT_ARM needs one.
```

Then change the `ARCH_M68K` derived-symbol block:

```kconfig
config ARCH_M68K
	bool
	default y if MACHINE_ATARI || MACHINE_ARANYM || MACHINE_AMIGA || MACHINE_VIRT_M68K || ARCH_COLDFIRE
```

Do **not** add `MACHINE_VIRT_M68K` to `CONF_ATARI_HARDWARE`'s `default y if ...` list — leaving it out is what turns off `DETECT_NATIVE_FEATURES`, `CONF_WITH_BUS_ERROR`, `CONF_WITH_68030_PMMU`, and `CONF_WITH_68040_PMMU` automatically (all `depends on ... CONF_ATARI_HARDWARE`).

- [ ] **Step 2: Add `TARGET_VIRT_M68K_KERNEL` to `Kconfig.image`**

In the `choice prompt "Image type"` block, immediately after `config TARGET_VIRT_ARM_KERNEL`'s `endchoice`-adjacent entry:

```kconfig
config TARGET_VIRT_M68K_KERNEL
	bool "QEMU virt (m68k) kernel image"
	depends on MACHINE_VIRT_M68K
	help
	  An ELF image passed directly to "qemu-system-m68k -M virt -kernel".
	  Like the ARM virt kernel image, this is not converted to a flat
	  binary: QEMU's ELF loader is used directly.
```

Then extend `EMUTOS_LIVES_IN_RAM`:

```kconfig
config EMUTOS_LIVES_IN_RAM
	bool
	default y if TARGET_PRG || TARGET_FLOPPY || TARGET_AMIGA_FLOPPY || TARGET_RPI_KERNEL || TARGET_VIRT_ARM_KERNEL || TARGET_VIRT_M68K_KERNEL
```

- [ ] **Step 3: Wire the Makefile**

Add after the existing `MACHINE-$(MACHINE_VIRT_ARM) += virt-arm` line:

```make
MACHINE-$(MACHINE_VIRT_M68K) += virt-m68k
```

Add an image-default entry after the `ifdef TARGET_VIRT_ARM_KERNEL` block (near line 321 of the current file):

```make
ifdef TARGET_VIRT_M68K_KERNEL
image-default = virt-m68k.elf
MEMBOT_REFERENCE = TOS162
endif
```

Add a build rule after the `ifdef TARGET_VIRT_ARM_KERNEL` / `cp $< $@` block (near line 473):

```make
#
# QEMU virt (m68k) kernel image — passed to QEMU as an ELF, unchanged
#

ifdef TARGET_VIRT_M68K_KERNEL
$(IMAGE): $(EMUTOS_IMG)
	cp $< $@
endif
```

- [ ] **Step 4: No `bios/build.mk` line needed yet**

`startup.o` is already in the unconditional `obj-y` list at the top of `bios/build.mk`, and `memory.o` is already in the unconditional list further down — both resolve to `bios/machine/virt-m68k/` via `vpath` once that directory exists, the same way they resolve to `bios/machine/virt-arm/` today. **Do not** add an `obj-$(MACHINE_VIRT_M68K) += ...` line in this task: the Goldfish driver objects it would list don't have source files until Tasks 2 and 3, and this task's own build+link step (Step 9 below) would fail with "cannot find obj/goldfish_tty.o" if that line existed here. Tasks 2 and 3 each add their own driver's object to this line when they create its source file.

- [ ] **Step 5: Create `configs/virt-m68k_defconfig`**

```
# ELF kernel image for QEMU's m68k 'virt' machine (qemu-system-m68k -M virt)
MACHINE_VIRT_M68K=y

# QEMU's m68k 'virt' board defaults to an m68040 core (see hw/m68k/virt.c's
# mc->default_cpu_type); there is no per-target CPUFLAGS default for it yet
# (unlike the Raspberry Pi targets), so spell it out explicitly, following
# the MACHINE_ARANYM precedent in Kconfig.machine.
CPUFLAGS="-m68040"

# QEMU's 'virt' board has no IDE interface; without this, bios/ide.c fails
# to build because struct IDE is never defined for this machine. Matched
# to the same override already used by MACHINE_RPI and MACHINE_VIRT_ARM.
CONF_WITH_IDE=n
```

- [ ] **Step 6: Create `bios/machine/virt-m68k/memory.c`**

```c
/*
 * memory.c - QEMU virt (m68k) memory initialization placeholder
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_VIRT_M68K
#error This file must only be compiled for the QEMU virt (m68k) target
#endif

/*
 * bios/build.mk lists memory.o unconditionally, and vpath resolves it to
 * this file for MACHINE_VIRT_M68K (the way bios/machine/virt-arm/memory.c
 * is resolved for MACHINE_VIRT_ARM). This machine needs no runtime memory
 * initialization of its own: the RAM size is fixed by the QEMU command
 * line, and startup.S sets _phystop directly before any C code runs. So
 * this file is deliberately empty.
 */
```

- [ ] **Step 7: Create `bios/machine/virt-m68k/startup.S`**

```asm
/*
 * startup.S - EmuTOS startup module for QEMU's m68k 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"
#include "header.h"

        .globl  _os_entry

        .extern _stktop
        .extern _biosmain
        .extern __endvdibss
        .extern __bss
        .extern __ebss
        .extern _phystop

        .text

/*
 * OSHEADER -- identical in shape to bios/arch/m68k/startup.S; see that
 * file for a field-by-field description. Unlike the Atari boot chain,
 * none of these fields double as real CPU reset vectors: QEMU's -kernel
 * loader jumps directly to _os_entry (the ELF entry point) with the CPU
 * already in supervisor mode, so nothing ever reads vectors 0/1 from
 * physical address 0 on this board.
 */

        .globl  _main
        .globl  _os_beg
        .globl  _os_magic
        .globl  _os_date
        .globl  _os_conf
        .globl  _os_dosdate
        .globl  _root
        .globl  _shifty
        .globl  _run

_os_entry:
        bra.s   _main       // os_entry, branch to _main
os_version:
        .dc.w   TOS_VERSION // os_version, TOS version
reseth:
        .dc.l   _main       // reseth, pointer to reset handler
_os_beg:
        .dc.l   _os_entry   // os_beg, base of os = _sysbase
os_end:
        .dc.l   __endvdibss // os_end, end of VDI BSS
os_res1:
        .dc.l   _main       // os_res1, reserved
_os_magic:
#if CONF_WITH_AES
        .dc.l   _ui_mupb    // os_magic, pointer to GEM's MUPB
#else
        .dc.l   0           // os_magic, pointer to GEM's MUPB
#endif
_os_date:
        .dc.l   OS_DATE     // os_date, Date of system build
_os_conf:
#if CONF_MULTILANG
        .dc.w   OS_CONF_MULTILANG
#else
        .dc.w   (OS_COUNTRY << 1) + OS_PAL
#endif
_os_dosdate:
        .dc.w   OS_DOSDATE
os_root:
        .dc.l   _root
os_kbshift:
        .dc.l   _shifty
os_run:
        .dc.l   _run
os_dummy:
        .ascii  "ETOS"

/*
 * Entry point. QEMU's m68k 'virt' machine resets with SP = 0 and PC = the
 * ELF entry point (hw/m68k/virt.c's main_cpu_reset() only sets
 * initial_pc; initial_stack defaults to 0), and every other register,
 * including VBR and the cache/MMU control registers, zeroed
 * (target/m68k/cpu.c's m68k_cpu_reset_hold() does memset(env, 0, ...)).
 * So unlike the classic Atari boot in bios/arch/m68k/startup.S, there is
 * no reset-instruction dance, no ST-MMU bank probe, no cartridge check,
 * and no cache/PMMU teardown to do here: none of that hardware exists on
 * this board, and the CPU reset state already is what that dance would
 * produce anyway. The board's only selectable CPUs are 68020-68060 (see
 * hw/m68k/virt.c's BI_CPUTYPE handling), so there is no need for the
 * 68000-compatibility fallback the classic startup.S uses around MOVEC.
 */
_main:
        move.w  #0x2700,sr              // supervisor mode, interrupts masked
        moveq   #0,d0
        MOVEC_D0_VBR                    // VBR = 0 (already 0 after reset; explicit)

        lea     _stktop,sp              // initial supervisor stack (.stack section, tosvars.ld)

        // Clear .bss: unlike the classic Atari boot, there is no meminit
        // pass to do this for us here (that routine tests and clears
        // ST-MMU banks, none of which exist on this board), so it has to
        // happen in this file instead.
        lea     __bss,a0
        lea     __ebss,a1
clear_bss:
        cmp.l   a1,a0
        jge     clear_bss_done
        clr.l   (a0)+
        jra     clear_bss
clear_bss_done:

        // RAM size is fixed by convention, matching the "-m 128" in the
        // qemu invocation documented in readme.md -- there is no FDT to
        // parse for v1 (see the design doc). _phystop is a fixed-address
        // sysvar (tosvars.ld); bios/biosmem.c's bmem_init() reads it
        // directly to size the entire free-memory pool.
        move.l  #0x08000000,_phystop

        jmp     _biosmain
```

- [ ] **Step 8: Build it**

Run: `make virt-m68k_defconfig && make`
Expected: build succeeds, producing `virt-m68k.elf` in the repo root. If it fails on an unresolved `int_timerc`/`vector_5ms`/etc. symbol, that means `ARCH_M68K_CLASSIC`'s generic objects (`obj-$(ARCH_M68K)` in `bios/build.mk`) aren't being pulled in — double check `ARCH_M68K` actually evaluates to `y` for this config via `grep ARCH_M68K obj/autoconf.h`.

- [ ] **Step 9: Smoke-test the boot (no console yet — this task's real pass/fail signal)**

Run:
```sh
timeout 5 qemu-system-m68k -M virt -m 128 -kernel virt-m68k.elf \
    -serial file:/tmp/claude-1000/-home-freyr-pTOS/91d8b1db-8d57-4da9-b541-d71de376d987/scratchpad/virt-m68k-serial.log \
    -d guest_errors,unimp -D /tmp/claude-1000/-home-freyr-pTOS/91d8b1db-8d57-4da9-b541-d71de376d987/scratchpad/virt-m68k-qemu.log \
    -monitor none -display none
echo "exit: $?"
cat /tmp/claude-1000/-home-freyr-pTOS/91d8b1db-8d57-4da9-b541-d71de376d987/scratchpad/virt-m68k-qemu.log
```
Expected: the process runs for the full 5 seconds (killed by `timeout`, exit code 124), `virt-m68k-qemu.log` is **empty** (no Address Error / Illegal Instruction / unimplemented-device entries), and `virt-m68k-serial.log` is empty (no console driver yet). An empty guest-errors log after 5 seconds of real execution is meaningful here: `calibrate_delay()` spins waiting on `_hz_200`, which nothing increments yet in this task, so the CPU is legitimately parked in a tight, harmless loop — not crashed, not silently rebooting. If the log is non-empty, or the process exits before the timeout fires, something in the boot chain (bss clear, `_phystop`, stack) is wrong.

- [ ] **Step 10: Commit**

```bash
git add Kconfig.machine Kconfig.image Makefile \
    configs/virt-m68k_defconfig \
    bios/machine/virt-m68k/startup.S bios/machine/virt-m68k/memory.c
git commit -m "m68k-virt: add machine/image Kconfig, build wiring, and minimal boot"
```

---

## Task 2: Goldfish TTY console driver

**Files:**
- Create: `bios/machine/virt-m68k/goldfish_tty.h`
- Create: `bios/machine/virt-m68k/goldfish_tty.c`
- Modify: `bios/Kconfig`
- Modify: `bios/serport.c`
- Modify: `bios/build.mk`

**Interfaces:**
- Consumes: nothing from Task 1 beyond the machine already booting.
- Produces: `goldfish_tty_can_write(void) -> BOOL`, `goldfish_tty_write_byte(UBYTE b) -> void`, `goldfish_tty_can_read(void) -> BOOL`, `goldfish_tty_read_byte(void) -> UBYTE`, consumed only by `bios/serport.c`.

- [ ] **Step 1: Create `bios/machine/virt-m68k/goldfish_tty.h`**

```c
/*
 * goldfish_tty.h - access to the Goldfish TTY on QEMU's m68k 'virt'
 * machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef GOLDFISH_TTY_H
#define GOLDFISH_TTY_H

#ifdef MACHINE_VIRT_M68K

BOOL goldfish_tty_can_write(void);
void goldfish_tty_write_byte(UBYTE b);
BOOL goldfish_tty_can_read(void);
UBYTE goldfish_tty_read_byte(void);

#endif /* MACHINE_VIRT_M68K */

#endif /* GOLDFISH_TTY_H */
```

- [ ] **Step 2: Create `bios/machine/virt-m68k/goldfish_tty.c`**

```c
/*
 * goldfish_tty.c - Goldfish TTY driver for QEMU's m68k 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_VIRT_M68K
#error This file must only be compiled for the QEMU m68k virt target
#endif

#include "portab.h"
#include "goldfish_tty.h"

#define GOLDFISH_TTY_BASE  0xff008000UL

#define TTY_PUT_CHAR      (*(volatile ULONG*)(GOLDFISH_TTY_BASE + 0x00))
#define TTY_BYTES_READY   (*(volatile ULONG*)(GOLDFISH_TTY_BASE + 0x04))
#define TTY_CMD           (*(volatile ULONG*)(GOLDFISH_TTY_BASE + 0x08))
#define TTY_DATA_PTR      (*(volatile ULONG*)(GOLDFISH_TTY_BASE + 0x10))
#define TTY_DATA_LEN      (*(volatile ULONG*)(GOLDFISH_TTY_BASE + 0x14))

#define TTY_CMD_READ_BUFFER  3

/* Scratch buffer for the DMA-style single-byte read below: the Goldfish
 * TTY has no direct "get char" register, only PUT_CHAR for output; input
 * always goes through CMD_READ_BUFFER copying into a RAM address we
 * supply via DATA_PTR/DATA_LEN. RAM sits 1:1 at its physical address on
 * this board (no MMU), so a plain address-of is already correct. */
static UBYTE goldfish_tty_rx_byte;

BOOL goldfish_tty_can_write(void)
{
    return TRUE;    /* PUT_CHAR always accepts a byte immediately on this virtual device */
}

void goldfish_tty_write_byte(UBYTE b)
{
    TTY_PUT_CHAR = b;
}

BOOL goldfish_tty_can_read(void)
{
    return TTY_BYTES_READY != 0;
}

UBYTE goldfish_tty_read_byte(void)
{
    while (!goldfish_tty_can_read())
        ;

    TTY_DATA_PTR = (ULONG)&goldfish_tty_rx_byte;
    TTY_DATA_LEN = 1;
    TTY_CMD = TTY_CMD_READ_BUFFER;

    return goldfish_tty_rx_byte;
}
```

- [ ] **Step 3: Add `CONF_WITH_GOLDFISH_TTY` to `bios/Kconfig`**

Immediately after the existing `config CONF_WITH_VIRT_UART` block:

```kconfig
config CONF_WITH_GOLDFISH_TTY
	bool "QEMU virt (m68k) Goldfish TTY support"
	depends on MACHINE_VIRT_M68K
	default y
	help
	  Use the Goldfish TTY device exposed by QEMU's m68k 'virt' machine
	  as the serial console.
```

- [ ] **Step 4: Wire `bios/serport.c`**

Add the include, next to the existing `CONF_WITH_VIRT_UART` block:

```c
#if CONF_WITH_GOLDFISH_TTY
#include "goldfish_tty.h"
#endif
```

In `bconstat1()`, add a branch next to the existing `CONF_WITH_VIRT_UART` one:

```c
#elif CONF_WITH_GOLDFISH_TTY
    return goldfish_tty_can_read() ? -1 : 0;
```

In `bconin1()`:

```c
#elif CONF_WITH_GOLDFISH_TTY
    return goldfish_tty_read_byte();
```

In `bcostat1()`:

```c
#elif CONF_WITH_GOLDFISH_TTY
    return goldfish_tty_can_write() ? -1 : 0;
```

In `bconout1()`:

```c
#elif CONF_WITH_GOLDFISH_TTY
    goldfish_tty_write_byte(b);
    return 1;
```

(No init call is needed in `init_serport()`: unlike the PL011 UART, the Goldfish TTY has no baud-rate/format registers to program — `PUT_CHAR` and `CMD_READ_BUFFER` work immediately with no setup.)

- [ ] **Step 5: Wire `bios/build.mk`**

Add after the existing `obj-$(MACHINE_VIRT_ARM) += virt_uart.o virt_mmu.o virt_pic.o virt_timer.o` line:

```make
obj-$(MACHINE_VIRT_M68K) += goldfish_tty.o
```

(Task 3 extends this same line with the PIC/RTC objects — don't create a second `obj-$(MACHINE_VIRT_M68K)` line, append to this one.)

- [ ] **Step 6: Build and smoke-test with a console**

Run: `make virt-m68k_defconfig && make`
Then:
```sh
timeout 5 qemu-system-m68k -M virt -m 128 -kernel virt-m68k.elf \
    -serial stdio -d guest_errors,unimp \
    -D /tmp/claude-1000/-home-freyr-pTOS/91d8b1db-8d57-4da9-b541-d71de376d987/scratchpad/virt-m68k-qemu.log \
    -monitor none -display none
```
Expected: `KDEBUG` lines appear on stdout (e.g. `machine_init()`, `bmem_init()`, `chardev_init()`, `init_serport()`), then output stops at `calibrate_delay()` (still no working timer — Task 3), and the process runs the full 5 seconds. `virt-m68k-qemu.log` stays empty.

- [ ] **Step 7: Commit**

```bash
git add bios/machine/virt-m68k/goldfish_tty.h bios/machine/virt-m68k/goldfish_tty.c \
    bios/Kconfig bios/serport.c bios/build.mk
git commit -m "m68k-virt: add Goldfish TTY console driver"
```

---

## Task 3: Goldfish PIC + RTC (interrupt routing and periodic tick)

**Files:**
- Create: `bios/machine/virt-m68k/goldfish_pic.h`
- Create: `bios/machine/virt-m68k/goldfish_pic.c`
- Create: `bios/machine/virt-m68k/goldfish_rtc.h`
- Create: `bios/machine/virt-m68k/goldfish_rtc.c`
- Create: `bios/machine/virt-m68k/goldfish_rtc_isr.S`
- Modify: `bios/bios.c`
- Modify: `bios/build.mk`

**Interfaces:**
- Consumes: `goldfish_pic_enable(int pic, int bit)` (defined this task, used within it); `_vector_5ms` (an existing shared `void (*)(void)` global declared in `bios/bios.c`, already set to `int_timerc` by the existing shared `bios/mfp.c: init_system_timer()` — see reference section above, no change needed there).
- Produces: `goldfish_pic_init(void)`, `goldfish_rtc_init(void)` — both called once from `bios/bios.c`, mirroring the existing `virt_pic_init()`/`virt_timer_init()` calls for `MACHINE_VIRT_ARM`.

- [ ] **Step 1: Create `bios/machine/virt-m68k/goldfish_pic.h`**

```c
/*
 * goldfish_pic.h - Goldfish PIC interrupt routing for QEMU's m68k
 * 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef GOLDFISH_PIC_H
#define GOLDFISH_PIC_H

#ifdef MACHINE_VIRT_M68K

void goldfish_pic_init(void);
void goldfish_pic_enable(int pic, int bit);

#endif /* MACHINE_VIRT_M68K */

#endif /* GOLDFISH_PIC_H */
```

- [ ] **Step 2: Create `bios/machine/virt-m68k/goldfish_pic.c`**

```c
/*
 * goldfish_pic.c - Goldfish PIC interrupt routing for QEMU's m68k
 * 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_VIRT_M68K
#error This file must only be compiled for the QEMU m68k virt target
#endif

#include "portab.h"
#include "goldfish_pic.h"

/*
 * 6 goldfish-pic instances at 0xff000000, 0x1000 bytes apart (see
 * hw/m68k/virt.c). Each one is wired by QEMU's m68k IRQ controller
 * (hw/intc/m68k_irqc.c) to one CPU autovector level: PIC index n (0-5)
 * drives CPU level n+1, so its interrupts are taken at vector n+25.
 */
#define GOLDFISH_PIC_BASE(n)    (0xff000000UL + (ULONG)(n) * 0x1000UL)

#define PIC_IRQ_DISABLE_ALL(n)  (*(volatile ULONG*)(GOLDFISH_PIC_BASE(n) + 0x08))
#define PIC_ENABLE(n)           (*(volatile ULONG*)(GOLDFISH_PIC_BASE(n) + 0x10))

#define GOLDFISH_PIC_COUNT  6

void goldfish_pic_init(void)
{
    int n;

    for (n = 0; n < GOLDFISH_PIC_COUNT; n++)
        PIC_IRQ_DISABLE_ALL(n) = 1;   /* value is ignored; any write disables all 32 lines */
}

void goldfish_pic_enable(int pic, int bit)
{
    PIC_ENABLE(pic) = (1UL << bit);
}
```

- [ ] **Step 3: Create `bios/machine/virt-m68k/goldfish_rtc.h`**

```c
/*
 * goldfish_rtc.h - Goldfish RTC periodic tick for QEMU's m68k
 * 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef GOLDFISH_RTC_H
#define GOLDFISH_RTC_H

#ifdef MACHINE_VIRT_M68K

void goldfish_rtc_init(void);

/* Called only from goldfish_rtc_isr.S's exception-vector entry point. */
void goldfish_rtc_service(void);

#endif /* MACHINE_VIRT_M68K */

#endif /* GOLDFISH_RTC_H */
```

- [ ] **Step 4: Create `bios/machine/virt-m68k/goldfish_rtc.c`**

```c
/*
 * goldfish_rtc.c - Goldfish RTC periodic tick for QEMU's m68k
 * 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_VIRT_M68K
#error This file must only be compiled for the QEMU m68k virt target
#endif

#include "portab.h"
#include "goldfish_pic.h"
#include "goldfish_rtc.h"

#define GOLDFISH_RTC_BASE  0xff006000UL   /* first of 2 instances; only this one is used */

#define RTC_TIME_LOW         (*(volatile ULONG*)(GOLDFISH_RTC_BASE + 0x00))
#define RTC_TIME_HIGH        (*(volatile ULONG*)(GOLDFISH_RTC_BASE + 0x04))
#define RTC_ALARM_LOW        (*(volatile ULONG*)(GOLDFISH_RTC_BASE + 0x08))
#define RTC_ALARM_HIGH       (*(volatile ULONG*)(GOLDFISH_RTC_BASE + 0x0c))
#define RTC_IRQ_ENABLED      (*(volatile ULONG*)(GOLDFISH_RTC_BASE + 0x10))
#define RTC_CLEAR_INTERRUPT  (*(volatile ULONG*)(GOLDFISH_RTC_BASE + 0x1c))

/* QEMU wires this RTC instance to PIC index 5 ("PIC #6"), bit 0 (see
 * hw/m68k/virt.c: VIRT_GF_RTC_IRQ_BASE = PIC_IRQ(6, 1)), which the m68k
 * IRQ controller (hw/intc/m68k_irqc.c) takes at CPU autovector level 6,
 * i.e. vector 30 -- RAM vector-table address 30*4 = 0x78. */
#define GOLDFISH_RTC_PIC_INDEX  5
#define GOLDFISH_RTC_PIC_BIT    0
#define GOLDFISH_RTC_VECTOR     30

#define TICK_NS  5000000UL   /* 5 ms = 200 Hz, matches the classic Atari timer C rate */

extern void goldfish_rtc_isr(void);   /* defined in goldfish_rtc_isr.S */

static void goldfish_rtc_arm_next(void)
{
    ULONG lo, hi, new_lo, new_hi;

    lo = RTC_TIME_LOW;    /* latches TIME_HIGH as a side effect (see goldfish_rtc_read()) */
    hi = RTC_TIME_HIGH;

    new_lo = lo + TICK_NS;
    new_hi = hi + (new_lo < lo ? 1 : 0);   /* 64-bit carry */

    RTC_ALARM_HIGH = new_hi;   /* write high first: writing low is what arms the alarm */
    RTC_ALARM_LOW = new_lo;
}

/* Called from goldfish_rtc_isr.S, once per interrupt. */
void goldfish_rtc_service(void)
{
    RTC_CLEAR_INTERRUPT = 1;
    goldfish_rtc_arm_next();
}

void goldfish_rtc_init(void)
{
    *(volatile ULONG*)((ULONG)GOLDFISH_RTC_VECTOR * 4) = (ULONG)goldfish_rtc_isr;

    RTC_IRQ_ENABLED = 1;
    goldfish_rtc_arm_next();

    goldfish_pic_enable(GOLDFISH_RTC_PIC_INDEX, GOLDFISH_RTC_PIC_BIT);
}
```

- [ ] **Step 5: Create `bios/machine/virt-m68k/goldfish_rtc_isr.S`**

```asm
/*
 * goldfish_rtc_isr.S - CPU exception-vector entry point for the Goldfish
 * RTC's periodic tick, on QEMU's m68k 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * This has to be hand-written assembly, unlike goldfish_rtc.c's other
 * logic: it runs directly off the CPU's autovector-30 exception, so it
 * alone is responsible for saving/restoring the registers it clobbers
 * and returning correctly with RTE.
 */

#include "asmdefs.h"

        .globl  _goldfish_rtc_isr
        .extern _goldfish_rtc_service
        .extern _vector_5ms

        .text

_goldfish_rtc_isr:
        movem.l d0-d1/a0-a1,-(sp)      // save the registers goldfish_rtc_service() clobbers
        jsr     _goldfish_rtc_service  // ack + re-arm the device (bios/machine/virt-m68k/goldfish_rtc.c)
        movem.l (sp)+,d0-d1/a0-a1      // restore them; sp is now back at the hardware exception frame

        // _vector_5ms is set to int_timerc (bios/arch/m68k/vectors.S) by
        // the shared bios/mfp.c: init_system_timer(), which runs later in
        // bios_init()'s sequence than goldfish_rtc_init() -- so the first
        // several ticks can legitimately arrive with it still NULL.
        move.l  _vector_5ms,a0
        tst.l   a0
        jeq     no_tick

        // Tail-jump, not jsr/rts: int_timerc ends with its own RTE, which
        // must pop the *original* hardware exception frame still sitting
        // where our own entry left it (see bios/arch/m68k/amiga2.S's
        // _amiga_int_ciaa_timer_b for the same pattern on another
        // non-Atari-hardware m68k machine).
        jmp     (a0)

no_tick:
        rte
```

- [ ] **Step 6: Wire `bios/bios.c`**

Add the includes, next to the existing `#ifdef MACHINE_VIRT_ARM` block:

```c
#ifdef MACHINE_VIRT_M68K
#include "goldfish_pic.h"
#include "goldfish_rtc.h"
#endif
```

Add the init calls, next to the existing `#elif defined(MACHINE_VIRT_ARM)` block:

```c
#elif defined(MACHINE_VIRT_M68K)
    goldfish_pic_init();
    goldfish_rtc_init();
#endif
```

(Producing, combined with the two existing branches:)

```c
#ifdef MACHINE_RPI
    raspi_interrupt_init();
#elif defined(MACHINE_VIRT_ARM)
    virt_pic_init();
    virt_timer_init();
#elif defined(MACHINE_VIRT_M68K)
    goldfish_pic_init();
    goldfish_rtc_init();
#endif
```

- [ ] **Step 7: Wire `bios/build.mk`**

Extend the `obj-$(MACHINE_VIRT_M68K)` line Task 2 created:

```make
obj-$(MACHINE_VIRT_M68K) += goldfish_tty.o goldfish_pic.o goldfish_rtc.o goldfish_rtc_isr.o
```

- [ ] **Step 8: Build and smoke-test the full boot**

Run: `make virt-m68k_defconfig && make`
Then:
```sh
timeout 8 qemu-system-m68k -M virt -m 128 -kernel virt-m68k.elf \
    -serial stdio -d guest_errors,unimp \
    -D /tmp/claude-1000/-home-freyr-pTOS/91d8b1db-8d57-4da9-b541-d71de376d987/scratchpad/virt-m68k-qemu.log \
    -monitor none -display none
```
Expected: `KDEBUG` output now progresses **past** `calibrate_delay()` to later init lines (`chardev_init()`, `init_serport()`, `bmem_init()`, `screen_init()`, `cookie_init()`, ...), since `calibrate_delay()` depends on `_hz_200` actually incrementing — proof the RTC interrupt is really firing and `int_timerc` is really running. Matching the ARM/raspi milestone, it's fine (expected, out of v1 scope) if boot eventually stalls or panics once it reaches AES/VDI-dependent code; what this step confirms is that the timer works. `virt-m68k-qemu.log` should still be empty of guest errors up to that point.

- [ ] **Step 9: Commit**

```bash
git add bios/machine/virt-m68k/goldfish_pic.h bios/machine/virt-m68k/goldfish_pic.c \
    bios/machine/virt-m68k/goldfish_rtc.h bios/machine/virt-m68k/goldfish_rtc.c \
    bios/machine/virt-m68k/goldfish_rtc_isr.S bios/bios.c bios/build.mk
git commit -m "m68k-virt: add Goldfish PIC/RTC interrupt routing and periodic tick"
```

---

## Task 4: Documentation and final verification

**Files:**
- Modify: `readme.md`

**Interfaces:**
- Consumes: the finished `virt-m68k.elf` build from Tasks 1-3.
- Produces: nothing new; this task documents and verifies the finished v1 milestone.

- [ ] **Step 1: Document the invocation in `readme.md`**

Add, right after the existing `To test the ARM virt port, run` section (readme.md:34-37):

```markdown
To test the m68k `virt` port, run

    make virt-m68k_defconfig && make
    qemu-system-m68k -M virt -m 128 -kernel virt-m68k.elf -d guest_errors -serial stdio
```

- [ ] **Step 2: Run the definition-of-done check from the design doc**

Run:
```sh
make virt-m68k_defconfig && make
timeout 8 qemu-system-m68k -M virt -m 128 -kernel virt-m68k.elf -d guest_errors -serial stdio
```
Expected: matches the design doc's stated definition of done — the image boots, `KDEBUG` output is visible on the serial console, and it reaches the same point the ARM virt and Raspberry Pi ports reach (BIOS runs; full AES/desktop success is out of v1 scope and not required here).

- [ ] **Step 3: Also confirm the existing targets still build (portability check per CLAUDE.md)**

Run:
```sh
make rpi2_defconfig && make
make virt-arm_defconfig && make
```
Expected: both still succeed — this change only added new `#ifdef MACHINE_VIRT_M68K`/`#elif defined(MACHINE_VIRT_M68K)` branches and new files, touching no code path any other machine takes.

- [ ] **Step 4: Commit**

```bash
git add readme.md
git commit -m "m68k-virt: document the qemu-system-m68k -M virt invocation"
```

- [ ] **Step 5: Update the draft PR**

Push the branch, then edit PR #36's description to check off the completed items in its task list and switch it from draft to ready for review (or leave as draft if the user prefers a final look first — confirm with the user before marking ready).

---

## Self-Review

**Spec coverage:** v1 scope (boot + console + timer, matching raspi/virt-arm's milestone) — Tasks 1-3. Kconfig additions exactly as resolved in the design doc's "resolved" section — Task 1 Steps 1-2, Task 2 Step 3. `tosvars.ld`/`emutos.ld` needing no changes — verified in the Hardware reference section, no task touches either file. Build wiring (`Makefile`, `build.mk`, defconfig) — Task 1 Steps 3-5. Definition of done (`make virt-m68k_defconfig && make` + qemu boot to visible `KDEBUG`) — Task 4 Step 2. `readme.md` documentation — Task 4 Step 1.

**Placeholder scan:** none found — every step has real, complete code or a concrete shell command with a stated expected result.

**Type consistency:** `goldfish_pic_enable(int pic, int bit)` declared in `goldfish_pic.h` (Task 3 Step 1) matches its definition (Step 2) and its one call site in `goldfish_rtc_init()` (Step 4: `goldfish_pic_enable(GOLDFISH_RTC_PIC_INDEX, GOLDFISH_RTC_PIC_BIT)`). `goldfish_rtc_service(void)` declared in the header (Step 3), defined in `goldfish_rtc.c` (Step 4), and called only from `goldfish_rtc_isr.S` (Step 5) via `jsr _goldfish_rtc_service` — consistent leading-underscore convention throughout. `goldfish_rtc_isr(void)` is `extern`'d in `goldfish_rtc.c` (Step 4) and defined/`.globl`'d in `goldfish_rtc_isr.S` (Step 5) as `_goldfish_rtc_isr`. `goldfish_tty_*` functions (Task 2) match between header and implementation and are consumed only by the 4 `bios/serport.c` call sites listed.
