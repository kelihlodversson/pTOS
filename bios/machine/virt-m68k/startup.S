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
        .globl  _os_header
        .globl  _osxhbootdelay
        .globl  _os_beg
        .globl  _os_magic
        .globl  _os_date
        .globl  _os_conf
        .globl  _os_dosdate
        .globl  _root
        .globl  _shifty
        .globl  _run

_os_header:
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
_osxhbootdelay:
        .dc.b   0           // no cold-boot delay on this board

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
        jhs     clear_bss_done          // unsigned compare: these are addresses, not signed values
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
