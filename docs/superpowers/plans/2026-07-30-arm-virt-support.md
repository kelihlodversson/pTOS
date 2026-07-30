# ARM `virt` Machine Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Boot pTOS under `qemu-system-arm -M virt` with a working serial console and a ticking periodic timer, reaching the same point the existing raspi port reaches (BIOS runs, AES launch attempted, then fails because most of the VDI is non-functional) — this closes GitHub issue #25.

**Architecture:** A new `MACHINE_VIRT_ARM` machine, reusing the existing shared `bios/arch/arm` exception/vector/cache code unchanged, with a new `bios/machine/virt-arm/` directory providing: a boot sequence that starts physically-addressed (QEMU's `virt` board has no RAM at address 0, unlike the raspi board) and switches to the classic low-fixed-address TOS system-variable layout via a small statically-built MMU translation table, a PL011 UART driver, a GICv2 driver, and an ARM generic-timer periodic tick.

**Tech Stack:** C (GNU C90 + GNU extensions, `-std=gnu90`), ARM assembler (`arm-none-eabi-` toolchain), GNU `ld` linker scripts, Kconfig/kconfiglib, QEMU (`qemu-system-arm -M virt`) as the only test target — there is no real hardware for this machine.

## Global Constraints

- C90 with GNU extensions; declarations at the top of a block. 4 spaces, never a hard tab. Run `make gitready` before committing (see `doc/coding.txt`).
- Use `portab.h` types (`WORD`, `LONG`, `UBYTE`, `UWORD`, `ULONG`, `BOOL`), not bare `int`/`long`.
- Trace with `KDEBUG(("..."))` / `KINFO(())` from `include/kprint.h`, never a private printf.
- New source files: object basenames must be unique across the whole tree (objects land flat in `obj/`).
- **Correction found during Task 1's review, binding for every remaining task:** machine subdirectories do **not** have their own `build.mk` — only `bios/`, `bdos/`, `vdi/`, `aes/`, `desk/`, `cli/`, `usb/`, `util/` do, and the top-level `Makefile` only `include`s those. `bios/machine/raspi/` has no `build.mk`; `startup.o` and `memory.o` reach it purely because `bios/build.mk` already unconditionally lists `obj-y += startup.o` (first, per the link-order rule) and `obj-y += memory.o`, resolved to the machine-specific file by `vpath`. Extra machine-specific objects (raspi's UART/interrupt/mailbox/screen/eMMC drivers) are added directly to `bios/build.mk` as `obj-$(MACHINE_RPI) += raspi_board.o raspi_uart.o raspi_int.o raspi_mbox.o raspi_screen.o raspi_emmc.o`. Every task below that says "modify `bios/machine/virt-arm/build.mk`" means: add or extend a single `obj-$(MACHINE_VIRT_ARM) += ...` line in `bios/build.mk` instead, in the same style. Do not create a `bios/machine/virt-arm/build.mk` file — it would never be read.
- Do not add per-target defaults to `include/config.h` — Kconfig only.
- Every task in this plan is verified by building with the `arm-none-eabi-` toolchain and booting the result under QEMU; there is no unit-test suite for BIOS/boot code in this codebase, so "the expected string appears on the QEMU serial console, and `-d guest_errors` prints nothing unexpected" is the test.
- Design reference: `docs/superpowers/specs/2026-07-30-qemu-virt-support-design.md`.

---

## Background needed for every task below

**QEMU `virt` (ARM) fixed addresses used throughout this plan** (from QEMU's `hw/arm/virt.c` memory map; treat as "the documented/standard values — if a task's QEMU run shows a fault or garbled output, this is the first thing to double check against the QEMU version actually installed"):

| Device | Base address |
|---|---|
| RAM | `0x40000000` |
| GIC distributor (GICv2) | `0x08000000` |
| GIC CPU interface (GICv2) | `0x08010000` |
| UART0 (PL011) | `0x09000000` |

GICv2 register offsets are architecturally fixed (ARM GIC specification, not QEMU-specific): `GICD_CTLR=+0x000`, `GICD_ISENABLER=+0x100`, `GICD_ICENABLER=+0x180`, `GICD_IPRIORITYR=+0x400` (1 byte/IRQ), `GICD_ITARGETSR=+0x800` (1 byte/IRQ), `GICD_ICFGR=+0xC00`; `GICC_CTLR=+0x000`, `GICC_PMR=+0x004`, `GICC_IAR=+0x00C`, `GICC_EOIR=+0x010`. The non-secure physical timer is always PPI 30 on any GICv2 system with the ARM generic timer (fixed by the GIC/generic-timer binding).

PL011 register offsets are already in this codebase at `bios/raspi_uart.c:35-49` (`DR=+0x00`, `FR=+0x18`, `IBRD=+0x24`, `FBRD=+0x28`, `LCRH=+0x2C`, `CR=+0x30`, `IMSC=+0x38`, `ICR=+0x44`) — QEMU's `virt` PL011 uses the same standard register layout, just without the GPIO pin-mux and VideoCore-mailbox clock query that `raspi_uart.c` needs (that part is Raspberry Pi specific, not PL011-generic).

**Why the boot sequence needs a two-phase (physical-then-virtual) approach:** `tosvars.ld` (included from `emutos.ld`) hardcodes ~147 TOS system-variable addresses as absolute constants in `0x380`-`0x800` (e.g. `tosvars.ld:23` `_proc_lives = 0x380;`), and this scheme is load-bearing well beyond that file — `bios/arch/arm/vectors.c:56` (`init_exc_vec`) and `init_user_vec` write default handlers directly into physical addresses `0x8`-`0x100` and `0x100`-`0x400`, and `bios/vectors.h:62-79` defines `VEC_LEVEL1`..`VEC_TRAP14` etc. as dereferences of literal addresses in that same range, consumed by `_arm_dispatch_svc` in the machine's `startup.S`. Every existing machine (Atari, Amiga, ColdFire eval boards, and the real Raspberry Pi) has actual RAM at physical `0x0`, so `stram : ORIGIN = 0x00000000` in `emutos.ld` just works. QEMU's `virt` board's RAM starts at physical `0x40000000` — address `0` is flash, not RAM. Per the approved design, this plan does **not** touch `tosvars.ld` or change any of these addresses; instead it builds a static MMU (page) table that maps virtual `0x0`-and-up onto physical `0x40000000`-and-up (an offset mapping), so every existing fixed-address symbol keeps working unmodified. Everything that isn't RAM (the GIC, the UART, ...) is identity-mapped (virtual address == physical address), matching how those peripherals are already addressed via literal constants.

This means the image is **linked** at low virtual addresses (exactly like the raspi image) but must be **loaded** by QEMU at `link_address + 0x40000000`, and the code that runs before the MMU is switched on executes at its true physical address, not its linked address. Task 3 below builds this.

---

### Task 1: Kconfig, build wiring, and a booting-but-silent skeleton

**Files:**
- Modify: `Kconfig.machine`
- Modify: `Kconfig.image`
- Modify: `Makefile`
- Create: `configs/virt-arm_defconfig`
- Create: `bios/machine/virt-arm/virt_memmap.h`
- Create: `bios/machine/virt-arm/startup.S`

**Interfaces:**
- Produces: `MACHINE_VIRT_ARM` and `TARGET_VIRT_ARM_KERNEL` Kconfig symbols; `bios/machine/virt-arm/virt_memmap.h` defining `VIRT_RAM_BASE`, `VIRT_GIC_DIST_BASE`, `VIRT_GIC_CPU_BASE`, `VIRT_UART0_BASE` (all `ULONG` hex constants) — every later task in this plan includes this header instead of re-declaring these addresses.
- Consumes: nothing (first task).

- [ ] **Step 1: Add the `MACHINE_VIRT_ARM` choice entry**

In `Kconfig.machine`, in the `Machine` choice (the block containing `config MACHINE_RPI`), add immediately after the `MACHINE_RPI` entry:

```
config MACHINE_VIRT_ARM
	bool "QEMU virt (ARM)"
	help
	  QEMU's generic ARM 'virt' board (qemu-system-arm -M virt). No real
	  hardware; there is no VideoCore/mailbox interface. Exposes a
	  PL011 UART, a GICv2 interrupt controller, and RAM starting at
	  physical 0x40000000 (unlike every other supported machine, whose
	  RAM starts at 0). Boots a -kernel ELF directly; the fixed
	  low-address TOS system variables are made to work via a small
	  statically built MMU translation table set up early in the boot
	  sequence, not by moving them.
```

Then find the `config ARCH_ARM` block (`bool` / `default y if MACHINE_RPI`) a little further down and change it to:

```
config ARCH_ARM
	bool
	default y if MACHINE_RPI || MACHINE_VIRT_ARM
```

- [ ] **Step 2: Add the `TARGET_VIRT_ARM_KERNEL` image type**

In `Kconfig.image`, in the `Image type` choice, add after the `TARGET_RPI_KERNEL` entry (before its `endchoice`):

```
config TARGET_VIRT_ARM_KERNEL
	bool "QEMU virt (ARM) kernel image"
	depends on MACHINE_VIRT_ARM
	help
	  An ELF image passed directly to "qemu-system-arm -M virt -kernel".
	  Unlike the Raspberry Pi kernel image, this is not converted to a
	  flat binary: QEMU's ELF loader is used directly, so the entry
	  point and load addresses come from the ELF header.
```

Then find `config EMUTOS_LIVES_IN_RAM` a bit further down and add `TARGET_VIRT_ARM_KERNEL` to its default-y list:

```
config EMUTOS_LIVES_IN_RAM
	bool
	default y if TARGET_PRG || TARGET_FLOPPY || TARGET_AMIGA_FLOPPY || TARGET_RPI_KERNEL || TARGET_VIRT_ARM_KERNEL
```

- [ ] **Step 3: Wire the machine directory name and image rule into `Makefile`**

Find this line (machine-to-directory mapping block):

```make
MACHINE-$(MACHINE_RPI) += raspi
```

Add immediately after it:

```make
MACHINE-$(MACHINE_VIRT_ARM) += virt-arm
```

Find the `ifdef TARGET_RPI_KERNEL` / `image-default = ...` block (the one computing `kernel7.img` etc.) and add immediately after its `endif`:

```make
ifdef TARGET_VIRT_ARM_KERNEL
image-default = virt-arm.elf
MEMBOT_REFERENCE = TOS162
endif
```

Find the `ifdef TARGET_RPI_KERNEL` / `$(IMAGE): $(EMUTOS_IMG)` build-rule block (the one running `$(OBJCOPY) $< -O binary $@`) and add immediately after its `endif`:

```make
#
# QEMU virt (ARM) kernel image — passed to QEMU as an ELF, unchanged
#

ifdef TARGET_VIRT_ARM_KERNEL
$(IMAGE): $(EMUTOS_IMG)
	cp $< $@
endif
```

- [ ] **Step 4: Add the defconfig**

Create `configs/virt-arm_defconfig`:

```
# ELF kernel image for QEMU's ARM 'virt' machine (qemu-system-arm -M virt)
MACHINE_VIRT_ARM=y
```

- [ ] **Step 5: Add the fixed physical address header**

Create `bios/machine/virt-arm/virt_memmap.h`:

```c
/*
 * virt_memmap.h - fixed physical addresses of the QEMU ARM 'virt' board
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VIRT_MEMMAP_H
#define VIRT_MEMMAP_H

#ifdef MACHINE_VIRT_ARM

/* Where RAM starts on this board.  Unlike every other machine this port
 * supports, address 0 is NOT RAM here (it is flash) -- see the boot
 * sequence in startup.S for how the fixed low-address TOS system
 * variables are made to work regardless. */
#define VIRT_RAM_BASE       0x40000000UL

#define VIRT_GIC_DIST_BASE  0x08000000UL
#define VIRT_GIC_CPU_BASE   0x08010000UL
#define VIRT_UART0_BASE     0x09000000UL

#endif /* MACHINE_VIRT_ARM */

#endif /* VIRT_MEMMAP_H */
```

- [ ] **Step 6: Write the startup.S skeleton**

Create `bios/machine/virt-arm/startup.S`. This is closely modeled on `bios/machine/raspi/startup.S`: same `OSHEADER`/vector-table layout at the top (this part is generic ARM boot-header boilerplate, not Raspberry Pi specific) and the same six `_arm_dispatch_*` exception trampolines (`_arm_dispatch_undef`, `_arm_dispatch_svc`, `_arm_dispatch_prefetch_abort`, `_arm_dispatch_data_abort`, `_arm_dispatch_irq`, `_arm_dispatch_fiq` — copy these six trampolines verbatim, including their inline comments, from `bios/machine/raspi/startup.S:278-375` (not the OSHEADER/HYP-exit code above them), they only reference the shared `bios/arch/arm` mechanism, nothing raspi-specific), but:
- no VideoCore mailbox call, no HYP-mode exit dance, no secondary-core parking (QEMU's `virt` `-kernel` boot starts a single CPU already in SVC mode)
- `_arm_dispatch_irq` calls `_virt_int_handler` (defined in Task 5) instead of `_raspi_int_handler`
- `_main` is a placeholder for now: set the stack pointer to a literal physical scratch address and spin forever, so this task's only job is proving the image builds and the CPU reaches this code without faulting

```asm
/*
 * startup.S - EmuTOS startup module for QEMU's ARM 'virt' machine
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

        .text

/*
 * OSHEADER -- identical in shape to bios/machine/raspi/startup.S; see
 * that file for a field-by-field description.
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
.p2align 5
arm_vectors:
    b   _main
    b   _arm_dispatch_undef
    b   _arm_dispatch_svc
    b   _arm_dispatch_prefetch_abort
    b   _arm_dispatch_data_abort
    b   _arm_dispatch_undef
    b   _arm_dispatch_irq
    b   _arm_dispatch_fiq

os_version:
    .word   TOS_VERSION
reseth:
    .word   _main
_os_beg:
    .word   _os_entry
os_end:
    .word   __endvdibss
os_res1:
    .word   _main
_os_magic:
#if CONF_WITH_AES
    .word   _ui_mupb
#else
    .word   0
#endif
_os_date:
    .word   OS_DATE
_os_conf:
#if CONF_MULTILANG
    .word   OS_CONF_MULTILANG
#else
    .word   (OS_COUNTRY << 1) + OS_PAL
#endif
_os_dosdate:
    .word   OS_DOSDATE
os_root:
    .word   _root
os_kbshift:
    .word   _shifty
os_run:
    .word   _run
os_dummy:
    .ascii  "CAMELTOS"

/*
 * _main -- placeholder for this task only.  Later tasks replace the
 * body: Task 2 adds a physical-mode UART "hello", Task 3 adds the MMU
 * bring-up and the jump into virtual addressing, Task 4 hands off to
 * _biosmain.
 */
.balign 4
_main:
    ldr sp, =0x47ff0000     /* literal physical scratch address, well within a 128M -m allocation */
1:  b   1b

    .globl _arm_dispatch_undef
_arm_dispatch_undef:
    srsfd  sp!, #0x13
    cps    #0x13
    sub    sp, #4*15
    stmia  sp, {r0-r14}^
    ldr    r3, [sp, #+4*15]
    sub    r3, r3, #4
    ldr    r2, [r3]
    ldr    r0, =0xfff000f0
    ldr    r1, =0xe7f000f0
    and    r0, r0, r2
    cmp    r0, r1
    moveq  r0, #0x28
    movne  r0, #0x10
    mov    r1, sp
    ldr    ip, [r0]
    blx ip
    add    sp, #4*15
    ldmfd  sp, {r0-r14}^
    rfefd  sp!

    .globl _arm_dispatch_svc
_arm_dispatch_svc:
    ldr ip, [lr, #-4]
    and ip, ip, #0xF
    lsl ip, ip, #0x2
    ldr ip, [ip, #+0x80]
    bx  ip

    .globl _arm_dispatch_prefetch_abort
_arm_dispatch_prefetch_abort:
    sub    lr, lr, #4
    srsfd  sp!, #0x13
    cps    #0x13
    sub    sp, #4*15
    stmia  sp, {r0-r14}^
    mrc p15, 0, r2, c5, c0,  1
    mrc p15, 0, r3, c6, c0,  2
    mov r0, #0x08
    mov r1, sp
    ldr ip, [r0]
    blx ip
    add    sp, #4*15
    ldmfd  sp, {r0-r14}^
    rfefd  sp!

    .globl _arm_dispatch_data_abort
_arm_dispatch_data_abort:
    sub lr, lr, #8
    srsfd  sp!, #0x13
    cps    #0x13
    sub    sp, #4*15
    stmia  sp, {r0-r14}^
    mrc p15, 0, r2, c6, c0,  0
    mrc p15, 0, r3, c5, c0,  0
    mov r0, #0x0c
    mov r1, sp
    ldr ip, [r0]
    blx ip
    add    sp, #4*15
    ldmfd  sp, {r0-r14}^
    rfefd  sp!

    .extern _virt_int_handler
    .globl _arm_dispatch_irq
_arm_dispatch_irq:
    sub    lr, lr, #4
    srsfd  sp!, #0x13
    cps    #0x13
    stmfd  sp!, {r0-r3, ip, lr}
    bl     _virt_int_handler
    ldmfd  sp!, {r0-r3, ip, lr}
    rfefd  sp!

    .globl _arm_dispatch_fiq
_arm_dispatch_fiq:
    sub    lr, lr, #4
    srsfd  sp!, #0x13
    cps    #0x13
    rfefd  sp!
```

- [ ] **Step 7: No build.mk needed**

`bios/build.mk` already has `obj-y += startup.o` (first, satisfying the link-order rule) and `obj-y += memory.o` unconditionally — with `MACHINE-$(MACHINE_VIRT_ARM) += virt-arm` from Step 3 in place, `vpath` resolves both to `bios/machine/virt-arm/` automatically once `MACHINE_VIRT_ARM` is selected, exactly as it already does for `bios/machine/raspi/startup.S`. Nothing to create for this step; it exists only so a later task that adds a genuinely new object (not already unconditional) knows where that object needs to go (`bios/build.mk`'s own `obj-$(MACHINE_VIRT_ARM) += ...` line — see Task 2).

- [ ] **Step 8: Build**

Run:
```sh
make virt-arm_defconfig
make
```
Expected: build succeeds and produces `virt-arm.elf` (the line `# virt-arm.elf is ready`). If it fails to link with undefined-reference errors for symbols the OSHEADER references (e.g. `_ui_mupb` when `CONF_WITH_AES` is on), that means the defconfig needs `CONF_WITH_AES` explicitly (check what `make virt-arm_defconfig` actually enabled by default via `.config`); resolve by matching whatever `rpi2_defconfig` leaves enabled, since this task intentionally does not change any BIOS feature defaults.

- [ ] **Step 9: Boot-smoke-test under QEMU**

Run (the image spins forever, so bound it with `timeout`):
```sh
timeout 3 qemu-system-arm -M virt -cpu cortex-a7 -kernel virt-arm.elf -d guest_errors -serial stdio
```
Expected: the command runs for ~3 seconds and is killed by `timeout` (exit code 124), with **no** output from `-d guest_errors` (no `Unassigned mem access`, no `unhandled instruction`, no `Illegal ...` lines). No output at all is the correct result for this task — the spin loop has no observable effect yet. If `guest_errors` output appears, the entry point or a literal address used in `_main`/the trampolines is wrong; investigate before proceeding to Task 2, since every later task builds on this one executing cleanly.

- [ ] **Step 10: Commit**

```bash
git add Kconfig.machine Kconfig.image Makefile configs/virt-arm_defconfig \
        bios/machine/virt-arm/virt_memmap.h bios/machine/virt-arm/startup.S
git commit -m "arm-virt: add MACHINE_VIRT_ARM skeleton that boots to a spin loop"
```

---

### Task 2: PL011 UART driver and a physical-mode "hello"

**Files:**
- Create: `bios/machine/virt-arm/virt_uart.h`
- Create: `bios/machine/virt-arm/virt_uart.c`
- Modify: `bios/build.mk`
- Modify: `bios/machine/virt-arm/startup.S`

**Important build-system note:** machine subdirectories do not have their own `build.mk` — `bios/machine/raspi/` has none either. `bios/build.mk` is the single file that lists every BIOS object; machine-specific extra objects are added there directly as `obj-$(MACHINE_RPI) += raspi_board.o raspi_uart.o ...` (see that exact line in `bios/build.mk` for the pattern to copy). This task adds a new line the same way: `obj-$(MACHINE_VIRT_ARM) += virt_uart.o`.

**Interfaces:**
- Consumes: `VIRT_UART0_BASE` from `virt_memmap.h` (Task 1).
- Produces: `void virt_uart0_init(void)`, `BOOL virt_uart0_can_write(void)`, `void virt_uart0_write_byte(UBYTE b)`, `BOOL virt_uart0_can_read(void)`, `UBYTE virt_uart0_read_byte(void)` — Task 4 wires these into `bios/serport.c`, so the names and signatures below must match exactly.

- [ ] **Step 1: Write `virt_uart.h`**

```c
/*
 * virt_uart.h - access to the PL011 UART on QEMU's ARM 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VIRT_UART_H
#define VIRT_UART_H

#ifdef MACHINE_VIRT_ARM

void virt_uart0_init(void);
BOOL virt_uart0_can_write(void);
void virt_uart0_write_byte(UBYTE b);
BOOL virt_uart0_can_read(void);
UBYTE virt_uart0_read_byte(void);

#endif /* MACHINE_VIRT_ARM */

#endif /* VIRT_UART_H */
```

- [ ] **Step 2: Write `virt_uart.c`**

Adapted from `bios/raspi_uart.c:1-135`: same PL011 register layout and the same `write_byte`/`can_write`/`can_read`/`read_byte` bodies, but no GPIO pin-mux (QEMU's `virt` PL011 needs none) and no VideoCore mailbox clock query — the UART clock on this board is a fixed, documented value, so the baud-rate divisor is a compile-time constant.

```c
/*
 * virt_uart.c - PL011 UART driver for QEMU's ARM 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_VIRT_ARM
#error This file must only be compiled for the QEMU ARM virt target
#endif

#include "portab.h"
#include "virt_memmap.h"
#include "virt_uart.h"

#define UART0_DR     (*(volatile ULONG*)(VIRT_UART0_BASE+0x00))
#define UART0_FR     (*(volatile ULONG*)(VIRT_UART0_BASE+0x18))
#define UART0_IBRD   (*(volatile ULONG*)(VIRT_UART0_BASE+0x24))
#define UART0_FBRD   (*(volatile ULONG*)(VIRT_UART0_BASE+0x28))
#define UART0_LCRH   (*(volatile ULONG*)(VIRT_UART0_BASE+0x2C))
#define UART0_CR     (*(volatile ULONG*)(VIRT_UART0_BASE+0x30))
#define UART0_IMSC   (*(volatile ULONG*)(VIRT_UART0_BASE+0x38))
#define UART0_ICR    (*(volatile ULONG*)(VIRT_UART0_BASE+0x44))

/* Fixed PL011 input clock on the QEMU 'virt' board. QEMU's UART model
 * does not enforce real baud-rate timing (bytes are transferred as soon
 * as they are written), so this value only matters for documentation
 * purposes / a real terminal on the other end of -serial. */
#define UART_CLOCK  24000000UL
#define BAUDRATE    115200UL

void virt_uart0_init(void)
{
    ULONG baud16 = BAUDRATE * 16;
    ULONG int_div = UART_CLOCK / baud16;
    ULONG fractdiv2 = (UART_CLOCK % baud16) * 8 / BAUDRATE;
    ULONG fractdiv = fractdiv2 / 2 + fractdiv2 % 2;

    UART0_CR = 0;
    UART0_IMSC = 0;
    UART0_ICR = 0x7FF;
    UART0_IBRD = int_div;
    UART0_FBRD = fractdiv;
    UART0_LCRH = (3 << 5);     /* 8 bits, no parity, FIFOs enabled */
    UART0_CR = 0x301;          /* UARTEN | TXE | RXE */
}

BOOL virt_uart0_can_write(void)
{
    return (UART0_FR & 0x20) == 0;     /* TXFF clear */
}

void virt_uart0_write_byte(UBYTE b)
{
    while (!virt_uart0_can_write())
        ;
    UART0_DR = b;
}

BOOL virt_uart0_can_read(void)
{
    return (UART0_FR & 0x10) == 0;     /* RXFE clear */
}

UBYTE virt_uart0_read_byte(void)
{
    while (!virt_uart0_can_read())
        ;
    return (UBYTE) UART0_DR;
}
```

- [ ] **Step 3: Add it to the build**

In `bios/build.mk`, find the `obj-$(MACHINE_RPI) += raspi_board.o raspi_uart.o raspi_int.o raspi_mbox.o raspi_screen.o raspi_emmc.o` line and add immediately after it:
```make
obj-$(MACHINE_VIRT_ARM) += virt_uart.o
```
(`startup.o` and `memory.o` need no change here — they're already unconditional in this same file, per Task 1.)

- [ ] **Step 4: Print a physical-mode string from `_main`**

This is the key check for this task: `virt_uart0_init`/`virt_uart0_write_byte` only dereference literal address constants (via `VIRT_UART0_BASE`, a `#define`, not a linked symbol), so they are safe to call before the MMU is on — the CPU is executing physically, and these functions never touch a global/static C variable. Replace `_main`'s body in `bios/machine/virt-arm/startup.S`:

```asm
.balign 4
_main:
    ldr sp, =0x47ff0000
    bl  _virt_uart0_init
    ldr r4, =hello_msg
1:  ldrb r0, [r4], #1
    cmp r0, #0
    beq 2f
    bl  _virt_uart0_write_byte
    b   1b
2:  b   2b

hello_msg:
    .asciz "virt-arm: pre-MMU boot OK\r\n"
    .align 2
```

(`_virt_uart0_init`/`_virt_uart0_write_byte` — leading underscore per this codebase's CDECL convention for symbols called from assembler, documented in `doc/coding.txt` and used throughout `bios/machine/raspi/startup.S`.)

- [ ] **Step 5: Build and verify**

```sh
make virt-arm_defconfig && make
timeout 3 qemu-system-arm -M virt -cpu cortex-a7 -kernel virt-arm.elf -d guest_errors -serial stdio
```
Expected output: exactly one line, `virt-arm: pre-MMU boot OK`, then the command runs until `timeout` kills it (the trailing `2: b 2b` spin loop). No `guest_errors` output.

- [ ] **Step 6: Commit**

```bash
git add bios/machine/virt-arm/virt_uart.h bios/machine/virt-arm/virt_uart.c \
        bios/build.mk bios/machine/virt-arm/startup.S
git commit -m "arm-virt: add PL011 UART driver, verify physical-mode boot prints to it"
```

---

### Task 3: Static MMU tree and the jump into virtual addressing

**Files:**
- Modify: `emutos.ld`
- Modify: `bios/machine/virt-arm/startup.S`
- Modify: `bios/build.mk`
- Create: `bios/machine/virt-arm/virt_mmu.c`
- Create: `bios/machine/virt-arm/virt_mmu.h`

**Interfaces:**
- Consumes: `VIRT_RAM_BASE` from `virt_memmap.h` (Task 1); `struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR`, `ARMV6MMUL1SECTIONBASE`, `AP_ALL_ACCESS`, `APX_RW_ACCESS`, `DOMAIN_CLIENT`, `ARM_CONTROL_MMU`, `ARM_CONTROL_L1_CACHE`, `ARM_CONTROL_L1_INSTRUCTION_CACHE`, `ARM_CONTROL_BRANCH_PREDICTION`, `ARM_TTBR_INNER_WRITE_BACK`, `ARM_TTBR_OUTER_WRITE_BACK`, `ARM_TTBR_USE_SHAREABLE_MEM` from `bios/raspi_mmu.h` (this header has no `MACHINE_RPI` guard, so it is directly includable — see `bios/raspi_mmu.h:1-133`); `clean_data_cache()`, `flush_data_cache_all()`, `flush_branch_target_cache()` from `bios/processor_arm.h:78-80`; `data_sync_barrier()`, `flush_prefetch_buffer()` from `include/arch/arm/asm.h:99-110`.
- Produces: `void virt_mmu_bootstrap(ULONG ram_size_bytes, void *pagetable_phys)` — called only from `startup.S`, before the MMU is enabled, so **this function and everything it calls must not reference any global or static C variable** (only its parameters, locals, and literal constants). `startup.S` label `virt_arm_post_mmu` — the jump target once the MMU is live, from which point on normal symbol-addressed code resumes.

- [ ] **Step 1: Add the LMA/VMA split to `emutos.ld`**

The image stays linked (VMA) exactly where every other machine links it — `stram : ORIGIN = 0x00000000` is unchanged, and so is `ROM_ORIGIN`. What changes is where QEMU actually loads the bytes (LMA): `link_address + VIRT_RAM_BASE`.

**Use a per-section `AT(...)` clause, not a shared `AT>region`.** A shared load-memory region (`MEMORY { stram_lma : ... }` referenced via `AT>stram_lma` on each section) looks like the natural approach, but GNU ld only advances that region's "next free load address" pointer for sections that actually contain data (`PROGBITS`) — `.first_stram`, `.low_stram`, and the BIOS-stack `.stack` section (see below) hold no real data, only location-counter reservations for fixed-address symbol slots, so ld emits them as `NOBITS` and they never advance the shared region's pointer. The first real (`PROGBITS`) section then lands at the bare region origin instead of `origin + its own VMA`, silently producing the wrong load address for everything (verify this for yourself with `arm-none-eabi-readelf -l` on the built image if you want to see it directly — every segment's `PhysAddr` collapses to `VIRT_RAM_BASE` instead of `VIRT_RAM_BASE + VMA`). A per-section `AT(ADDR(section) + VIRT_RAM_BASE)` clause computes each section's load address directly from its own VMA and has no such failure mode.

Just above `SECTIONS {` (around line 60-68), add the LMA helper macro next to the existing `REGION_RAM`/`REGION_READ_ONLY` macros:

```
#if defined (MACHINE_VIRT_ARM)
# define LMA_AT(section) AT(ADDR(section) + 0x40000000)
#else
# define LMA_AT(section)
#endif
```

Then add `LMA_AT(name)` right after each section's `name :` (before its `{`, which is where a plain `AT(...)` clause belongs — unlike `AT>region`, it doesn't go after the closing `}`/region name) for every section that lands in `stram`/`REGION_READ_ONLY`/`REGION_READ_WRITE` (since `REGION_RAM` is `stram` and `EMUTOS_LIVES_IN_RAM` makes `REGION_READ_ONLY`/`REGION_READ_WRITE` resolve to it too, this is every section in the file for this target): `.first_stram`, `.low_stram`, `.text`, `.data`, `.bss`, `.laststram` — **and** the generic (non-Raspberry-Pi) BIOS-stack section, `#if !defined(MACHINE_RPI) .stack : { ... }` (a small `NOBITS` reservation, distinct from the *other*, `MACHINE_RPI`-only `.stack` block further down in the file, which stays untouched since it's inactive for `MACHINE_VIRT_ARM`). Getting this last one is easy to miss — it's easy to assume "the `.stack` section is RPI-only" from skimming the file, when there are actually two, and the generic one is exactly the one active here.

- [ ] **Step 2: Write `virt_mmu.h`**

```c
/*
 * virt_mmu.h - static MMU translation table bring-up for QEMU's ARM
 * 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VIRT_MMU_H
#define VIRT_MMU_H

#ifdef MACHINE_VIRT_ARM

/*
 * Builds a 4096-entry (4 GiB, 1 MiB/section) level-1 page table at
 * pagetable_phys and enables the MMU:
 *   - virtual [0, ram_size_bytes) maps to physical [VIRT_RAM_BASE,
 *     VIRT_RAM_BASE + ram_size_bytes) -- this is what makes the fixed
 *     low TOS system-variable addresses (see tosvars.ld) resolve to
 *     real RAM.
 *   - every other virtual address is identity-mapped (virtual ==
 *     physical), which is how the peripheral drivers (virt_uart.c, the
 *     future virt_pic.c/virt_timer.c) already address the GIC and the
 *     UART via VIRT_GIC_*_BASE/VIRT_UART0_BASE literal constants.
 *
 * Must be called with the MMU off, from startup.S, with a physical
 * (not linked/virtual) stack pointer already set up.  Neither this
 * function nor anything it calls may reference a global or static C
 * variable: at the point it runs, no linked (virtual) address is
 * valid yet, only literal constants and its own parameters/locals.
 */
void virt_mmu_bootstrap(ULONG ram_size_bytes, void *pagetable_phys);

#endif /* MACHINE_VIRT_ARM */

#endif /* VIRT_MMU_H */
```

- [ ] **Step 3: Write `virt_mmu.c`**

Section-descriptor field values are the same attributes `bios/machine/raspi/memory.c:118-176` (`init_mmu`) uses for RAM (cacheable, shareable) versus everything above it (device: not cacheable, not bufferable, execute-never).

**This needs two RAM-attributed regions, not one — a point the first draft of this plan got wrong and a task review caught.** The low virtual window `[0, ram_size)` maps to physical `[VIRT_RAM_BASE, VIRT_RAM_BASE+ram_size)` so the fixed-address sysvars work — that part is as described above. But the code executing this very function is *physically* running at its identity address (`VIRT_RAM_BASE` and up, since pre-MMU execution is physical), and that identity-mapped section must **also** be Normal/cacheable/executable RAM, not device — if it's left execute-never (as "everything outside the low window" would naively suggest), the CPU takes a Prefetch Abort on the very next instruction fetch after the MMU-enable write, because it's still fetching from `VIRT_RAM_BASE`-relative addresses at that exact moment and the identity mapping of that address is what's live until the explicit jump-to-virtual in `startup.S`. Also clamp the low window so it can never reach into the peripheral range (`VIRT_GIC_DIST_BASE` and up) even if a future caller passes a larger `ram_size_bytes` — at `-m 128` this doesn't yet bite (128 MiB ends exactly at `VIRT_GIC_DIST_BASE`), but nothing before this fix stopped a larger value from mapping RAM straight over the GIC and UART.

```c
/*
 * virt_mmu.c - static MMU translation table bring-up for QEMU's ARM
 * 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_VIRT_ARM
#error This file must only be compiled for the QEMU ARM virt target
#endif

#include "portab.h"
#include "raspi_mmu.h"
#include "processor_arm.h"
#include "asm.h"
#include "virt_memmap.h"
#include "virt_mmu.h"

#define MEGABYTE            0x100000UL
#define PAGE_TABLE_ENTRIES  4096

#define MMU_MODE    ( ARM_CONTROL_MMU                  \
                    | ARM_CONTROL_L1_CACHE              \
                    | ARM_CONTROL_L1_INSTRUCTION_CACHE  \
                    | ARM_CONTROL_BRANCH_PREDICTION)

#define TTBR_MODE   ( ARM_TTBR_INNER_WRITE_BACK  \
                    | ARM_TTBR_OUTER_WRITE_BACK)

void virt_mmu_bootstrap(ULONG ram_size_bytes, void *pagetable_phys)
{
    struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR *table =
        (struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR *) pagetable_phys;
    ULONG max_window_sections = VIRT_GIC_DIST_BASE / MEGABYTE;
    ULONG ram_window_sections = ram_size_bytes / MEGABYTE;
    ULONG ram_base_section = VIRT_RAM_BASE / MEGABYTE;
    ULONG i;
    ULONG control, aux_control;

    if (ram_window_sections > max_window_sections)
        ram_window_sections = max_window_sections;

    clean_data_cache();

    for (i = 0; i < PAGE_TABLE_ENTRIES; i++)
    {
        struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR *entry = &table[i];
        ULONG phys_base;
        /* Two disjoint aliases of the same physical RAM need Normal
         * (cacheable, executable) attributes: the low virtual window
         * used by the fixed-address sysvars, and the identity mapping
         * at VIRT_RAM_BASE -- which is where this function itself is
         * physically executing right now, pre-MMU, and must keep
         * executing from immediately after the MMU-enable write below,
         * until startup.S's explicit jump into virtual addressing. */
        BOOL low_window = (i < ram_window_sections);
        BOOL ram_identity = (i >= ram_base_section
                           && i < ram_base_section + ram_window_sections);
        BOOL is_ram = low_window || ram_identity;

        if (low_window)
            phys_base = i * MEGABYTE + VIRT_RAM_BASE;
        else
            phys_base = i * MEGABYTE;

        entry->Value10 = 2;
        entry->XNBit   = is_ram ? 0 : 1;
        entry->Domain  = 0;
        entry->IMPBit  = 0;
        entry->AP      = AP_ALL_ACCESS;
        entry->APXBit  = APX_RW_ACCESS;
        entry->NGBit   = 0;
        entry->Value0  = 0;
        entry->SBZ     = 0;
        entry->Base    = ARMV6MMUL1SECTIONBASE(phys_base);

        if (is_ram)
        {
            entry->BBit = 1;
            entry->CBit = 1;
            entry->TEX  = 0;
            entry->SBit = 1;
        }
        else
        {
            /* device: not cacheable, not bufferable */
            entry->BBit = 1;
            entry->CBit = 0;
            entry->TEX  = 0;
            entry->SBit = 1;
        }
    }

    clean_data_cache();

    asm volatile ("mrc p15, 0, %0, c1, c0,  1" : "=r" (aux_control));
    aux_control |= ARM_AUX_CONTROL_SMP;
    asm volatile ("mcr p15, 0, %0, c1, c0,  1" : : "r" (aux_control));

    asm volatile ("mcr p15, 0, %0, c2, c0,  2" : : "r" (0));
    asm volatile ("mcr p15, 0, %0, c2, c0,  0" : : "r" ((ULONG)table | TTBR_MODE));
    asm volatile ("mcr p15, 0, %0, c2, c0,  1" : : "r" ((ULONG)table | TTBR_MODE));
    asm volatile ("mcr p15, 0, %0, c3, c0,  0" : : "r" (DOMAIN_CLIENT << 0));

    flush_data_cache_all();
    flush_branch_target_cache();
    asm volatile ("mcr p15, 0, %0, c8, c7,  0" : : "r" (0));  /* invalidate unified TLB */
    data_sync_barrier();
    flush_prefetch_buffer();

    asm volatile ("mrc p15, 0, %0, c1, c0,  0" : "=r" (control));
    control &= ~ARM_CONTROL_STRICT_ALIGNMENT;
    control |= MMU_MODE;
    asm volatile ("mcr p15, 0, %0, c1, c0,  0" : : "r" (control) : "memory");

    /* Context-synchronize before relying on the new translation regime --
     * required by the architecture, and specifically what makes the very
     * next instruction fetch (back in startup.S) behave predictably. */
    flush_prefetch_buffer();
}
```

- [ ] **Step 4: Wire it into `startup.S`**

Replace the `_main` body written in Task 2 with the full two-phase sequence: a physical-mode "hello" (kept from Task 2, as the first sanity check), then the MMU bring-up (a literal physical page-table address and a literal physical RAM size, both plain numbers so they're safe pre-MMU), then a long jump into virtual addressing, then a second "hello" and a sysvars round-trip check using normal (symbol-based) addressing to prove the low window is correctly mapped.

**Getting the pre-MMU string address right matters here, and is easy to get wrong** — this plan's own first draft got it wrong, caught by a task review. `ldr r4, =hello_msg_phys` loads the label's *linked* (virtual) address, same as any other symbol reference; before the MMU is on and the low window is mapped, that virtual address is not backed by anything (this section's `LMA_AT(...)` from Task 3's `emutos.ld` change means the bytes physically live `0x40000000` higher than that). Use `adr` instead, which computes the label's address as a PC-relative offset from the *current, physical* PC — giving the right answer both before and after the MMU is enabled, unlike `ldr r4, =symbol`:

```asm
.balign 4
_main:
    ldr sp, =0x47ff0000
    bl  _virt_uart0_init
    adr r4, hello_msg_phys
    bl  print_string

    /* MMU bring-up: literal physical constants only, no linked symbols */
    mov r0, #0x08000000     /* 128 MiB RAM window, matches -m 128 in the qemu invocation below */
    ldr r1, =0x47f00000     /* page table: fixed physical scratch address, top of a 128M RAM allocation */
    bl  _virt_mmu_bootstrap

    ldr pc, =virt_arm_post_mmu

virt_arm_post_mmu:
    ldr sp, =0x47ff0000     /* still the physical scratch stack; Task 4 switches to _stktop */
    ldr r4, =hello_msg_virt
    bl  print_string

    /* sysvars round-trip: _phystop lives at a fixed low address defined
     * in tosvars.ld (0x42e for a non-RPI machine, which this is -- do
     * not assume the MACHINE_RPI value from that same file); if the low
     * MMU window is wired correctly this read/write pair works exactly
     * like any other global variable. */
    ldr r0, =_phystop
    ldr r1, =0x12345678
    str r1, [r0]
    ldr r2, [r0]
    cmp r1, r2
    ldreq r4, =sysvars_ok_msg
    ldrne r4, =sysvars_fail_msg
    bl  print_string

2:  b   2b

print_string:
    push {r4, lr}
1:  ldrb r0, [r4], #1
    cmp r0, #0
    beq 2f
    bl  _virt_uart0_write_byte
    b   1b
2:  pop {r4, lr}
    bx  lr

hello_msg_phys:
    .asciz "virt-arm: pre-MMU boot OK\r\n"
hello_msg_virt:
    .asciz "virt-arm: post-MMU boot OK\r\n"
sysvars_ok_msg:
    .asciz "virt-arm: sysvars alias OK\r\n"
sysvars_fail_msg:
    .asciz "virt-arm: sysvars alias FAILED\r\n"
    .align 2
```

Add `.extern _virt_mmu_bootstrap` and `.extern _phystop` near the top of the file with the other `.extern` declarations.

- [ ] **Step 5: Add `virt_mmu.o` to the build**

`bios/machine/raspi/` has no `build.mk` of its own — machine-specific objects are added directly to `bios/build.mk`. In `bios/build.mk`, extend the line Task 2 added:
```make
obj-$(MACHINE_VIRT_ARM) += virt_uart.o virt_mmu.o
```

- [ ] **Step 6: Build and verify**

```sh
make virt-arm_defconfig && make
timeout 3 qemu-system-arm -M virt -cpu cortex-a7 -m 128 -kernel virt-arm.elf -d guest_errors -serial stdio
```
Expected output, in order:
```
virt-arm: pre-MMU boot OK
virt-arm: post-MMU boot OK
virt-arm: sysvars alias OK
```
then the command runs until `timeout` kills it. No `guest_errors` output. If it hangs or faults between the two "boot OK" lines, the fault is in the page-table build or the MMU-enable sequence; if "sysvars alias FAILED" prints, the low-window offset mapping is wrong (check `VIRT_RAM_BASE` against the `-m 128` value actually passed, since `ram_window_sections` in `virt_mmu.c` must cover at least where `_phystop` is linked).

- [ ] **Step 7: Commit**

```bash
git add emutos.ld bios/machine/virt-arm/virt_mmu.h bios/machine/virt-arm/virt_mmu.c \
        bios/build.mk bios/machine/virt-arm/startup.S
git commit -m "arm-virt: build a static MMU tree so the fixed-address sysvars work"
```

---

### Task 4: Hand off to `_biosmain` and route `KDEBUG`/console through the UART

**Files:**
- Modify: `bios/machine/virt-arm/startup.S`
- Modify: `bios/Kconfig`
- Modify: `bios/serport.c`

**Interfaces:**
- Consumes: `virt_uart0_init/can_read/can_write/read_byte/write_byte` (Task 2); `_biosmain` (existing, `bios/bios.c`).
- Produces: nothing new consumed by later tasks — this task's job is making the existing generic BIOS boot path run and produce visible `KDEBUG` output for machine independent code that already exists.

- [ ] **Step 1: Add `CONF_WITH_VIRT_UART` to `bios/Kconfig`**

Immediately after the `config CONF_WITH_RASPI_UART0` block (`bios/Kconfig:215-220`), add:

```
config CONF_WITH_VIRT_UART
	bool "QEMU virt (ARM) PL011 UART support"
	depends on MACHINE_VIRT_ARM
	default y
	help
	  Use the PL011 UART exposed by QEMU's ARM 'virt' machine as the
	  serial console.
```

- [ ] **Step 2: Wire it into `bios/serport.c`**

`bios/serport.c` gates the existing raspi UART on `CONF_WITH_RASPI_UART0`, not on `MACHINE_RPI` (confirmed at `bios/serport.c:131,155,171,192,734`), so this needs the same treatment: add an `#elif CONF_WITH_VIRT_UART` branch calling `virt_uart0_*` next to every `#elif CONF_WITH_RASPI_UART0` branch.

`bios/serport.c:131`:
```c
#elif CONF_WITH_RASPI_UART0
    return raspi_uart0_can_read() ? -1 : 0;
#elif CONF_WITH_VIRT_UART
    return virt_uart0_can_read() ? -1 : 0;
```

`bios/serport.c:155`:
```c
#elif CONF_WITH_RASPI_UART0
    return raspi_uart0_read_byte();
#elif CONF_WITH_VIRT_UART
    return virt_uart0_read_byte();
```

`bios/serport.c:171`:
```c
#elif CONF_WITH_RASPI_UART0
    return raspi_uart0_can_write() ? -1 : 0;
#elif CONF_WITH_VIRT_UART
    return virt_uart0_can_write() ? -1 : 0;
```

`bios/serport.c:192`:
```c
#elif CONF_WITH_RASPI_UART0
    raspi_uart0_write_byte(b);
    return 1;
#elif CONF_WITH_VIRT_UART
    virt_uart0_write_byte(b);
    return 1;
```

`bios/serport.c:734`:
```c
#if CONF_WITH_RASPI_UART0
    raspi_uart0_init();
#endif
#if CONF_WITH_VIRT_UART
    virt_uart0_init();
#endif
```

Add `#include "virt_uart.h"` near the top of `bios/serport.c`, next to its existing `#include "raspi_uart.h"`.

- [ ] **Step 3: Hand off from `startup.S`**

Replace the tail of `_main` in `bios/machine/virt-arm/startup.S` (everything from `virt_arm_post_mmu:`'s sysvars check onward) so that, after confirming the sysvars alias works, it zeroes BSS, sets the real (symbol-based) stack, and jumps into the shared BIOS entry point — the same shape as `bios/machine/raspi/startup.S:187-193` (`bl _raspi_vcmem_init` / `b _biosmain`), except our MMU bring-up already happened, so there is no separate "vcmem_init" call, just the BSS clear:

```asm
virt_arm_post_mmu:
    ldr sp, =0x47ff0000
    ldr r4, =hello_msg_virt
    bl  print_string

    ldr r0, =_phystop
    ldr r1, =0x12345678
    str r1, [r0]
    ldr r2, [r0]
    cmp r1, r2
    ldreq r4, =sysvars_ok_msg
    ldrne r4, =sysvars_fail_msg
    bl  print_string

    /* Clear the BSS segment now that virtual addressing is live. */
    ldr r0, =_bss
    ldr r1, =_ebss
    mov r2, #0
1:  cmp r0, r1
    bge 2f
    str r2, [r0], #4
    b   1b
2:

    ldr sp, =_stktop
    b   _biosmain
```

Add `.extern _bss`, `.extern _ebss`, `.extern _stktop` near the top with the other `.extern` declarations (`_stktop` is already declared in Task 1's skeleton).

- [ ] **Step 4: Build and verify**

```sh
make virt-arm_defconfig && make
timeout 5 qemu-system-arm -M virt -cpu cortex-a7 -m 128 -kernel virt-arm.elf -d guest_errors -serial stdio
```
Expected: the three boot-check lines from Task 3, followed by real `KDEBUG` trace lines from `bios/bios.c` (e.g. `init_delay()`, `machine_detect()`, `machine_init()`, `bmem_init()`, `screen_init()`, `after vt52_init()` — see `bios/bios.c:320-364` for the exact sequence). `bios/delay.c` has no machine-specific hook (boot-time delays are a generic calibrated CPU busy-loop, not hardware-timer-based), so this task does not depend on the GIC/timer work in Task 5 to make progress — record exactly where it stops (a specific `KDEBUG` line, a crash, `guest_errors` output, or it reaches AES already) rather than assuming a particular hang point; that observation is the starting point for Task 5. The pass criterion for this task itself is: real `KDEBUG` output appears, proving `_biosmain` is running and the console driver works, with no `guest_errors` output before wherever it stops.

- [ ] **Step 5: Commit**

```bash
git add bios/Kconfig bios/serport.c bios/machine/virt-arm/startup.S
git commit -m "arm-virt: hand off to _biosmain, route the BIOS console through the PL011 UART"
```

---

### Task 5: GICv2 driver and the ARM generic timer tick

**Files:**
- Create: `bios/machine/virt-arm/virt_pic.h`
- Create: `bios/machine/virt-arm/virt_pic.c`
- Create: `bios/machine/virt-arm/virt_timer.h`
- Create: `bios/machine/virt-arm/virt_timer.c`
- Modify: `bios/build.mk`
- Modify: `bios/bios.c`

**Interfaces:**
- Consumes: `VIRT_GIC_DIST_BASE`, `VIRT_GIC_CPU_BASE` from `virt_memmap.h`; `vector_5ms` (existing, declared `bios/vectors.h:93`, defined `bios/bios.c:122`).
- Produces: `void virt_pic_init(void)`, `PFVOID virt_connect_irq(int irq, PFVOID handler)` (mirrors `raspi_connect_irq`'s signature at `bios/raspi_int.h:19`), `void virt_int_handler(void)` (referenced from `startup.S`'s `_arm_dispatch_irq`, added in Task 1); `void virt_timer_init(void)`.

- [ ] **Step 1: Write `virt_pic.h`**

```c
/*
 * virt_pic.h - GICv2 interrupt controller driver for QEMU's ARM 'virt'
 * machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VIRT_PIC_H
#define VIRT_PIC_H

#ifdef MACHINE_VIRT_ARM

#define VIRT_IRQ_LINES  32     /* only PPIs (16-31) are used by this port; no SPI needed for v1 */

void virt_pic_init(void);
PFVOID virt_connect_irq(int irq, PFVOID handler);
void virt_int_handler(void);

#endif /* MACHINE_VIRT_ARM */

#endif /* VIRT_PIC_H */
```

- [ ] **Step 2: Write `virt_pic.c`**

GICv2 register offsets are architecturally fixed (see "Background" above); this brings up the distributor and this CPU's interface, and dispatches by reading `GICC_IAR` and writing the same value back to `GICC_EOIR` — the standard GICv2 acknowledge/end-of-interrupt pair.

```c
/*
 * virt_pic.c - GICv2 interrupt controller driver for QEMU's ARM 'virt'
 * machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_VIRT_ARM
#error This file must only be compiled for the QEMU ARM virt target
#endif

#include "portab.h"
#include "virt_memmap.h"
#include "virt_pic.h"
#include "kprint.h"

#define GICD_CTLR        (*(volatile ULONG*)(VIRT_GIC_DIST_BASE + 0x000))
#define GICD_ISENABLER0  (*(volatile ULONG*)(VIRT_GIC_DIST_BASE + 0x100))
#define GICD_ICENABLER0  (*(volatile ULONG*)(VIRT_GIC_DIST_BASE + 0x180))
#define GICD_IPRIORITYR(n) (*(volatile UBYTE*)(VIRT_GIC_DIST_BASE + 0x400 + (n)))
#define GICC_CTLR  (*(volatile ULONG*)(VIRT_GIC_CPU_BASE + 0x000))
#define GICC_PMR   (*(volatile ULONG*)(VIRT_GIC_CPU_BASE + 0x004))
#define GICC_IAR   (*(volatile ULONG*)(VIRT_GIC_CPU_BASE + 0x00C))
#define GICC_EOIR  (*(volatile ULONG*)(VIRT_GIC_CPU_BASE + 0x010))

static PFVOID virt_irq_handlers[VIRT_IRQ_LINES];

void virt_pic_init(void)
{
    int i;

    for (i = 0; i < VIRT_IRQ_LINES; i++)
        virt_irq_handlers[i] = 0;

    GICD_ICENABLER0 = 0xffffffffUL;   /* disable everything to start from a known state */
    GICD_CTLR = 1;                    /* enable distributor */

    GICC_PMR = 0xff;                  /* let every priority through */
    GICC_CTLR = 1;                    /* enable this CPU's interface */
}

PFVOID virt_connect_irq(int irq, PFVOID handler)
{
    PFVOID old = virt_irq_handlers[irq];

    virt_irq_handlers[irq] = handler;
    GICD_IPRIORITYR(irq) = 0x80;
    GICD_ISENABLER0 = (1UL << irq);
    return old;
}

void virt_int_handler(void)
{
    ULONG iar = GICC_IAR;
    ULONG irq = iar & 0x3ffUL;

    if (irq < VIRT_IRQ_LINES && virt_irq_handlers[irq])
        ((void (*)(void))virt_irq_handlers[irq])();
    else
        KDEBUG(("virt_int_handler: unexpected IRQ %lu\n", irq));

    GICC_EOIR = iar;
}
```

- [ ] **Step 3: Write `virt_timer.h`**

```c
/*
 * virt_timer.h - ARM generic timer periodic tick for QEMU's ARM
 * 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VIRT_TIMER_H
#define VIRT_TIMER_H

#ifdef MACHINE_VIRT_ARM

void virt_timer_init(void);

#endif /* MACHINE_VIRT_ARM */

#endif /* VIRT_TIMER_H */
```

- [ ] **Step 4: Write `virt_timer.c`**

The generic-timer arithmetic (read `CNTPCT`, add `CNTFRQ/HZ` ticks, write `CNTP_CVAL`, enable via `CNTP_CTL`) mirrors the `RASPI_TIMER_GENERIC` branch already in this codebase at `bios/raspi_int.c:139-159` (handler) and `bios/raspi_int.c:207-217` (init) — reused here without the Raspberry-Pi-specific frequency/prescaler cross-check against `raspi_board`, since `CNTFRQ` is authoritative and architectural on every ARMv7+ core QEMU's `virt` machine models.

```c
/*
 * virt_timer.c - ARM generic timer periodic tick for QEMU's ARM
 * 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_VIRT_ARM
#error This file must only be compiled for the QEMU ARM virt target
#endif

#include "portab.h"
#include "virt_pic.h"
#include "virt_timer.h"
#include "vectors.h"

#define HZ                    200     /* ticks per second, matches the Atari 200 Hz timer C */
#define VIRT_TIMER_PPI_PHYS   30      /* non-secure physical timer, fixed by the GIC/generic-timer binding */

static ULONG ticks_per_hz;

static void virt_timer_tick(void)
{
    ULONG cval_low, cval_high;
    UQUAD cval;

    vector_5ms();

    asm volatile ("mrrc p15, 2, %0, %1, c14" : "=r" (cval_low), "=r" (cval_high));
    cval = ((UQUAD) cval_high << 32 | cval_low) + ticks_per_hz;
    asm volatile ("mcrr p15, 2, %0, %1, c14" :: "r" ((ULONG)(cval & 0xffffffffUL)),
                                                "r" ((ULONG)(cval >> 32)));
}

void virt_timer_init(void)
{
    ULONG cntfrq;
    ULONG cval_low, cval_high;
    UQUAD cval;

    asm volatile ("mrc p15, 0, %0, c14, c0, 0" : "=r" (cntfrq));
    ticks_per_hz = cntfrq / HZ;

    virt_connect_irq(VIRT_TIMER_PPI_PHYS, virt_timer_tick);

    asm volatile ("mrrc p15, 0, %0, %1, c14" : "=r" (cval_low), "=r" (cval_high));
    cval = ((UQUAD) cval_high << 32 | cval_low) + ticks_per_hz;
    asm volatile ("mcrr p15, 2, %0, %1, c14" :: "r" ((ULONG)(cval & 0xffffffffUL)),
                                                "r" ((ULONG)(cval >> 32)));
    asm volatile ("mcr p15, 0, %0, c14, c2, 1" :: "r" (1));   /* CNTP_CTL: ENABLE */
}
```

- [ ] **Step 5: Add the new objects to the build**

`bios/machine/raspi/` has no `build.mk` of its own — machine-specific objects are added directly to `bios/build.mk`. In `bios/build.mk`, extend the line from Tasks 2/3 one more time:
```make
obj-$(MACHINE_VIRT_ARM) += virt_uart.o virt_mmu.o virt_pic.o virt_timer.o
```

- [ ] **Step 6: Wire the init calls into `bios/bios.c`**

Find the existing hook (`bios/bios.c:339-341`):
```c
#ifdef MACHINE_RPI
    raspi_interrupt_init();
#endif
```
Change it to:
```c
#ifdef MACHINE_RPI
    raspi_interrupt_init();
#elif defined(MACHINE_VIRT_ARM)
    virt_pic_init();
    virt_timer_init();
    asm volatile ("cpsie i");
#endif
```
Add `#include "virt_pic.h"` and `#include "virt_timer.h"` near the top of `bios/bios.c`, next to its existing `#include "raspi_int.h"` (`bios/bios.c:68-69`).

The explicit `cpsie i` is needed here: neither `bios/machine/raspi/startup.S` nor the generic `bios/bios.c` path unmasks the CPSR I bit at boot — the only `cpsie` in the whole tree is in `aes/arch/arm/gemdosif.S:102`, which runs much later (as part of dispatching into a GEMDOS call from a running program, not at BIOS boot). This port's tick (`vector_5ms`/`hz_200`, used for AES/mouse timing once the AES is running) needs the timer IRQ delivered from here on, so it can't wait for that later, program-triggered unmask.

- [ ] **Step 7: Build and full end-to-end verification**

```sh
make virt-arm_defconfig && make
timeout 10 qemu-system-arm -M virt -cpu cortex-a7 -m 128 -kernel virt-arm.elf -d guest_errors -serial stdio
```
Expected: the three boot-check lines, the `KDEBUG` trace from Task 4, and this time the boot continues past whatever previously hung waiting on the timer, reaching the same point the raspi port reaches (BIOS runs, AES launch attempted, then fails) — this is the design's Definition of Done for this issue. No `guest_errors` output at any point. If it still hangs at the same place as Task 4, the GIC enable sequence or the `cpsie i` step is the first thing to check.

- [ ] **Step 8: Commit**

```bash
git add bios/machine/virt-arm/virt_pic.h bios/machine/virt-arm/virt_pic.c \
        bios/machine/virt-arm/virt_timer.h bios/machine/virt-arm/virt_timer.c \
        bios/build.mk bios/bios.c
git commit -m "arm-virt: add GICv2 driver and ARM generic timer tick, reach full boot"
```

---

### Task 6: Fix the SVC/TRAP #1 GEMDOS dispatch crash

**Why this task exists:** not in the original plan. Task 5's review confirmed its own scope (GICv2 + timer) works and is correctly verified, but building it exposed that `bios_init()` can now run to completion for the first time on this target — and the very next thing `biosmain()` does, `trap1(0x30)` (`bios/bios.c:753`, `Sversion()`), crashes with a repeating Data-Abort-then-Undefined-Instruction loop. This is the **first SVC/TRAP exception ever taken** on virt-arm (every earlier task's boot hung before reaching this point), so it was never exercised before. The design's stated v1 Definition of Done is "boot + console + timer, matching the raspi port's current maturity" (raspi reaches AES launch and fails there); virt-arm needs to reach that same point before Task 7's final acceptance check can honestly confirm it.

**Facts established already (verified independently by Task 5's reviewer, not just claimed):**
- The crash is in `bdos/arch/arm/rwa.S`'s `_enter` (the TRAP #1 / GEMDOS front end) or in reaching it via `_arm_dispatch_svc` (`bios/machine/virt-arm/startup.S:184-190`) — not in Task 5's own GIC/timer code, and not in interrupt delivery generally.
- IRQ vector dispatch through the same `arm_vectors` table (`bios/machine/virt-arm/startup.S:39-47`) is proven working: the ARM generic timer's periodic IRQ is delivered and handled correctly (Task 5 confirmed via register dump and trace). This means the low-address MMU window built in Task 3 correctly backs the vector-table-adjacent memory at runtime, ruling out "the MMU mapping is broken" as an explanation.
- `_arm_dispatch_svc` decodes the SVC immediate and loads a handler pointer from a **fixed low virtual address** (`ip = (instr & 0xF) << 2`, then `ldr ip, [ip, #0x80]` — i.e. reads from absolute address `imm*4 + 0x80`, not table-relative). This is the same low fixed-address scheme as the TOS sysvars Task 3 already proved works (`0x380`-`0x800`), and `0x80`-`0x100` is inside that same low window, so the read itself should be able to succeed if `Setexc()`/`init_exc_vec()` (`bios/arch/arm/vectors.c`) already populated that slot correctly by this point in boot — `osinit_before_xmaddalt()` (`bdos/bdosmain.c:328`) does call `Setexc(0x21, (long)enter)` before `bios_init()` returns, so the slot should be populated with `_enter`'s address before `trap1(0x30)` runs.
- `bdos/arch/arm/rwa.S` (`_enter`, `_gouser`, `x20`, etc.) is architecture-generic code, shared with `raspi`'s ARM build (not machine-specific) — and raspi reaches past this exact call today. So the bug is not a defect in `rwa.S` itself in the abstract; something about virt-arm's environment at this point differs from raspi's in a way that breaks it (candidates: `_run`'s value/initialization state, CPU mode/PSR handling around the `svc` instruction, the exact vector-table contents at `0x84`, or something else Task 5 didn't chase down).

**This is a debugging task, not a transcription task** — no code is given because the root cause isn't known yet. Use `systematic-debugging` methodology: reproduce, form a hypothesis, test the narrowest thing that confirms or rules it out, don't guess-and-check broadly.

- [ ] **Step 1: Reproduce with full diagnostics**

Temporarily enable `ENABLE_KDEBUG` in `bios/bios.c` and force `boot_status |= RS232_AVAILABLE` at the top of `biosmain()` (Task 4's and Task 5's reports both document this exact technique and how to revert it cleanly — read `.superpowers/sdd/2026-07-30-arm-virt-support/task-4-report.md` and `task-5-report.md` for the precedent). Rebuild and boot with `-d guest_errors,int` (not just `guest_errors` — the exception trace is what showed the Data-Abort/Undefined-Instruction loop in Task 5's diagnosis) to see the exact fault address(es) and confirm you're looking at the same failure Task 5 described.

- [ ] **Step 2: Root-cause it**

Some concrete things worth checking, in no particular order — pick whichever your evidence points to first, don't work this list top-to-bottom blindly:
- Disassemble around the fault PC/LR (`arm-none-eabi-objdump -d obj/*.o` or `virt-arm.elf`) and map the faulting address back to source, the same technique Task 5 used for the `vbl_list` bug.
- Check what `_run` actually contains at the moment `_enter` dereferences it (`bdos/arch/arm/rwa.S:66-68`) — is it correctly zero/valid at this point in boot on this target, and does `_enter`'s logic handle whatever value it legitimately has this early (before any process/basepage exists)?
- Check whether `Setexc(0x21, (long)enter)` (`bdos/bdosmain.c:328`, via `osinit_before_xmaddalt()`) actually wrote a valid pointer into the low vector slot the way it's supposed to, and whether that pointer is the physical/virtual address you expect at the point it's dereferenced.
- Check the CPSR/mode state across the `svc` instruction and `_arm_dispatch_svc`'s `bx ip` — compare against what raspi's boot path does differently, if anything, before reaching the same call.

- [ ] **Step 3: Fix it**

The fix belongs wherever the root cause actually is — that might be `bdos/arch/arm/rwa.S`, `bios/machine/virt-arm/startup.S`, `bios/arch/arm/vectors.c`, or elsewhere. If the root cause turns out to be in architecture-generic code (not `bios/machine/virt-arm/`), fix it there rather than working around it in virt-arm-specific code, and flag in your report that raspi should be re-tested too (it's not expected to regress, since raspi already exercises this path successfully today, but say so explicitly rather than assuming).

- [ ] **Step 4: Verify against the original Task 5 pass criterion**

```sh
make virt-arm_defconfig && make
timeout 30 qemu-system-arm -M virt -cpu cortex-a7 -m 128 -kernel virt-arm.elf -d guest_errors -display none -serial stdio < /dev/null > /tmp/virt-boot.log 2>&1 &
```
Wait ~20-25s (known sandbox serial-output latency, documented in every prior task's report) before reading the log. Expected: boot reaches the same point the raspi port reaches — BIOS runs, AES launch attempted, then fails — with no `guest_errors` output at any point. This is the actual Definition of Done Task 5's brief stated and this task exists to finish reaching. If you reach a *different* blocker than AES-launch-then-fail, record exactly where, the same way Task 4 and Task 5's reports did — don't assume you must keep going past this task's own scope to force a match.

Also re-run the `rpi2_defconfig` regression build if your fix touches anything outside `bios/machine/virt-arm/`:
```sh
make distclean && make rpi2_defconfig && make
```

- [ ] **Step 5: Commit**

```bash
git add <whatever files your fix touched>
git commit -m "arm-virt: fix SVC/TRAP #1 GEMDOS dispatch, reach AES-launch parity with raspi"
```

---

### Task 7: Documentation and final acceptance check

**Files:**
- Modify: `readme.md`

**Interfaces:**
- Consumes: nothing new.
- Produces: nothing (final task).

- [ ] **Step 1: Add the QEMU invocation to `readme.md`**

Next to the existing raspi invocation (`readme.md`, the `qemu-system-arm -M raspi2 ...` line), add:
```markdown
To test the ARM `virt` port, run

    make virt-arm_defconfig && make
    qemu-system-arm -M virt -cpu cortex-a7 -m 128 -kernel virt-arm.elf -d guest_errors -serial stdio
```

- [ ] **Step 2: Run the full definition-of-done check one more time from a clean build**

```sh
make distclean
make virt-arm_defconfig && make
timeout 10 qemu-system-arm -M virt -cpu cortex-a7 -m 128 -kernel virt-arm.elf -d guest_errors -serial stdio
```
Expected: identical to Task 5 Step 7's result, confirming nothing in the working tree was relied upon that a clean checkout wouldn't have (stale `obj/` files, etc.).

- [ ] **Step 3: Confirm the other machines still build**

Per this project's portability requirement (root `CLAUDE.md`: "the upstream Atari, Amiga and ColdFire targets still build"), rebuild at least one machine from each architecture family untouched by this plan:
```sh
make distclean && make rpi2_defconfig && make
make distclean && make atari256_defconfig && make
make distclean && make amiga_defconfig && make
```
Expected: all three succeed unchanged. This specifically guards against a mistake in the `emutos.ld`/`Kconfig.image`/`Kconfig.machine` edits from Task 1 and Task 3 leaking into other machines' builds (e.g. a missing `#if defined(MACHINE_VIRT_ARM)` guard around the new `LMA_AT(...)` macro).

- [ ] **Step 4: Run `make gitready` and commit**

```sh
make gitready
git add readme.md
git commit -m "doc: document the ARM virt QEMU invocation"
```

- [ ] **Step 5: Update the tracking issue**

Comment on GitHub issue #25 (and the #24 tracking issue) that the v1 milestone (boot + console + timer, matching the raspi port's current maturity) is done, and open a follow-up issue for virtio-mmio support (referencing `docs/superpowers/specs/2026-07-30-qemu-virt-support-design.md`'s "Shared code / future virtio-mmio driver" section) rather than adding it to this plan's scope.
