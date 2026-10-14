/*
 * startup.S - EmuTOS startup module for QEMU's m68k 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"
#include "header.h"
#include "memory.h"

        .globl  C_SYM(os_entry)

        .extern C_SYM(stktop)
        .extern C_SYM(biosmain)
        .extern __endvdibss
        .extern __bss
        .extern __ebss
        .extern C_SYM(phystop)
        .extern C_SYM(warm_magic)
        .extern C_SYM(meminit_flags)

        .text

/*
 * OSHEADER -- identical in shape to bios/arch/m68k/startup.S; see that
 * file for a field-by-field description. Unlike the Atari boot chain,
 * none of these fields double as real CPU reset vectors: QEMU's -kernel
 * loader jumps directly to _os_entry (the ELF entry point) with the CPU
 * already in supervisor mode, so nothing ever reads vectors 0/1 from
 * physical address 0 on this board.
 */

        .globl  C_SYM(main)
        .globl  C_SYM(os_header)
        .globl  C_SYM(osxhbootdelay)
        .globl  C_SYM(os_beg)
        .globl  C_SYM(os_magic)
        .globl  C_SYM(os_date)
        .globl  C_SYM(os_conf)
        .globl  C_SYM(os_dosdate)
        .globl  C_SYM(root)
        .globl  C_SYM(shifty)
        .globl  C_SYM(run)

C_SYM(os_header):
C_SYM(os_entry):
        bra.s   C_SYM(main)       // os_entry, branch to _main
os_version:
        .dc.w   TOS_VERSION // os_version, TOS version
reseth:
        .dc.l   C_SYM(main)       // reseth, pointer to reset handler
C_SYM(os_beg):
        .dc.l   C_SYM(os_entry)   // os_beg, base of os = _sysbase
os_end:
        .dc.l   __endvdibss // os_end, end of VDI BSS
os_res1:
        .dc.l   C_SYM(main)       // os_res1, reserved
C_SYM(os_magic):
#if CONF_WITH_AES
        .dc.l   C_SYM(ui_mupb)    // os_magic, pointer to GEM's MUPB
#else
        .dc.l   0           // os_magic, pointer to GEM's MUPB
#endif
C_SYM(os_date):
        .dc.l   OS_DATE     // os_date, Date of system build
C_SYM(os_conf):
#if CONF_MULTILANG
        .dc.w   OS_CONF_MULTILANG
#else
        .dc.w   (OS_COUNTRY << 1) + OS_PAL
#endif
C_SYM(os_dosdate):
        .dc.w   OS_DOSDATE
os_root:
        .dc.l   C_SYM(root)
os_kbshift:
        .dc.l   C_SYM(shifty)
os_run:
        .dc.l   C_SYM(run)
os_dummy:
        .ascii  "ETOS"
C_SYM(osxhbootdelay):
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
        .even
C_SYM(main):
        move.w  #0x2700,sr              // supervisor mode, interrupts masked
        lea     C_SYM(stktop),sp              // initial supervisor stack (.stack section, tosvars.ld)

        // RAM size: QEMU's -m.  The machine puts a Linux-style bootinfo list
        // right after the end of the loaded image (hw/m68k/virt.c: the
        // address after the highest loaded byte, rounded up to even, which is
        // __ebss as the last segment is .data + .bss), and one of its
        // records, BI_MEMCHUNK (tag 5, size 12), holds the start and size of
        // the RAM.  Each record is a tag word, a size word (of the whole
        // record) and the data; the list ends with a zero tag.  Fall back to
        // 128 MB if nothing sensible is found there, e.g. when something
        // else loaded the image.  This must come before the .bss is cleared,
        // which can write a word past __ebss when that is not a multiple of
        // four, onto the start of the list.  _phystop is a fixed-address sysvar
        // (tosvars.ld); bios/biosmem.c's bmem_init() reads it directly to
        // size the entire free-memory pool, and the virtio-gpu framebuffer
        // is allocated just below it, so it has to be right.
        //
        // A software reset (warm_reset or cold_reset in
        // bios/arch/m68k/panicasm.S) comes back here, but QEMU does not
        // create the list again, and the memory above __ebss is in use by
        // then.  The size found the first time is kept in two words in .data
        // (which nothing clears, unlike the .bss and _warm_magic), and used
        // when the first one holds its marker.
        cmp.l   #0x52414D53,ram_size_valid      // 'RAMS'
        jne     bi_scan
        move.l  ram_size_saved,d1
        jra     bi_done
bi_scan:
        lea     __ebss,a0
        move.l  a0,d0
        addq.l  #1,d0
        and.l   #-2,d0
        move.l  d0,a0
        move.l  #0x08000000,d1          // default: 128 MB
        moveq   #31,d2                  // look at the first 32 records
bi_next:
        move.w  (a0),d0                 // tag
        jeq     bi_done                 // BI_LAST (or no list at all)
        move.w  2(a0),d3                // size of this record
        cmp.w   #4,d3
        jcs     bi_done                 // too small to be a record: no list
        cmp.w   #5,d0                   // BI_MEMCHUNK?
        jne     bi_skip
        cmp.w   #12,d3
        jne     bi_skip
        tst.l   4(a0)                   // RAM starts at 0 on this board
        jne     bi_skip
        move.l  8(a0),d0
        cmp.l   #0x00100000,d0          // anything below 1 MB is nonsense
        jcs     bi_skip
        cmp.l   #0x7f000000,d0          // keep the size in a positive LONG,
        jls     bi_size_ok              // clear of the devices at 0xff000000
        move.l  #0x7f000000,d0
bi_size_ok:
        move.l  d0,d1
        jra     bi_done
bi_skip:
        adda.w  d3,a0
        dbra    d2,bi_next
bi_done:
        move.l  d1,ram_size_saved
        move.l  #0x52414D53,ram_size_valid

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

        move.l  d1,C_SYM(phystop)       // the RAM size found above

        // The classic meminit code is not used here, so decide first boot
        // vs. warm boot ourselves, the way it does when EmuTOS lives in RAM:
        // anything but WARM_MAGIC in _warm_magic is a first boot.  Without
        // this, meminit_flags stays 0 and every boot looks like a warm boot
        // (no welcome screen, kept bootdev, ...).
#if CONF_DETECT_FIRST_BOOT_WITHOUT_MEMCONF
        move.l  #0x5741524D,d0          // WARM_MAGIC
        cmp.l   C_SYM(warm_magic).w,d0
        jeq     not_first_boot
        move.b  #MEMINIT_FIRST_BOOT,C_SYM(meminit_flags)
        move.l  d0,C_SYM(warm_magic).w  // next boot will not be a first boot
not_first_boot:
#else
        // No warm_magic to tell the boots apart: every boot is a first boot
        move.b  #MEMINIT_FIRST_BOOT,C_SYM(meminit_flags)
#endif

        jmp     C_SYM(biosmain)

        .data
        .even
ram_size_valid: .dc.l   0
ram_size_saved: .dc.l   0
