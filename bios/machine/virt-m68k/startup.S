/*
 * startup.S - EmuTOS startup module for QEMU's m68k 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"
#include "header.h"
#include "memory.h"
#include "virt_mmu.h"

#if CONF_WITH_HIGHER_HALF
/*
 * The code is linked in the window (emutos.ld) but runs, until the MMU is on,
 * where QEMU loaded it: at the physical address, the link address less
 * VIRT_WINDOW_VBASE.  So up to jumping to high_start, nothing may use the
 * address of a symbol of the window as it stands: PHYS() has it converted.
 * The TOS low memory (the vectors, the system variables, the stack) is at the
 * same address in both and needs no conversion.
 */
#define PHYS(sym)       (sym - VIRT_WINDOW_VBASE).l
#endif

        .globl  C_SYM(os_entry)

        .extern C_SYM(stktop)
        .extern C_SYM(biosmain)
        .extern __endvdibss
        .extern __bss
        .extern __ebss
        .extern C_SYM(phystop)
        .extern __low_stram_end
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
#if CONF_WITH_HIGHER_HALF
        // The physical address: QEMU 6.2 starts the CPU at a reset from the
        // copy of this longword at address 4 (bios.c vecs_init()), with the
        // MMU off; main goes on at the physical alias in any case.
        .dc.l   C_SYM(main) - VIRT_WINDOW_VBASE
#else
        .dc.l   C_SYM(main)       // reseth, pointer to reset handler
#endif
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
#if CONF_WITH_HIGHER_HALF
        // Entered at the physical address at the first boot, with the MMU
        // off, and at the window by a software reset (warm_reset(): jmp
        // main) with the MMU on; both reach the low alias of the code, where
        // it is and where the tables map it too.
        lea     main_low(pc),a0
        move.l  a0,d0
        bclr    #31,d0
        move.l  d0,a0
        jmp     (a0)
main_low:
#endif

        // Clear low memory: the exception vectors, the system variables, the
        // BIOS stack and the low ST-RAM section, everything from address 0 to
        // the end of .low_stram.  The ELF has no data for them (they are NOBITS),
        // so QEMU's loader leaves them as the machine's RAM starts out, zeroed,
        // and the code has always relied on that.  After a reset of the machine
        // (Shutdown(SHUT_BOOT), the monitor's system_reset) the RAM is as the last
        // run left it, vectors and variables included, and the boot went wrong
        // at the first illegal instruction of the FPU probe.  The classic Atari
        // boot clears this memory in meminit; here nothing did.  (No stack yet:
        // it is in the memory being cleared.)
        sub.l   a0,a0
        lea     __low_stram_end,a1
clear_low:
        clr.l   (a0)+
        cmp.l   a1,a0
        jlo     clear_low

        lea     C_SYM(stktop),sp              // initial supervisor stack (.stack section, tosvars.ld)
#if CONF_REQUIRE_PMMU
        // An image that needs the PMMU (#564): before anything can depend
        // on it, make sure there is one, and if not, say so on the goldfish
        // TTY (its PUT_CHAR register needs no set-up) and stop.  Only
        // 68000 instructions until the check is done: jsr, not the bsr.l a
        // jbsr becomes for -m68020.
#if CONF_WITH_HIGHER_HALF
        lea     PHYS(C_SYM(pmmu_present)),a0
        jsr     (a0)
#else
        jsr     C_SYM(pmmu_present)
#endif
        tst.l   d0
        jne     pmmu_ok
        lea     no_pmmu_msg(pc),a0
        move.l  #0xff008000,a1          // GOLDFISH_TTY_BASE: PUT_CHAR
no_pmmu_putc:
        moveq   #0,d0
        move.b  (a0)+,d0
        jeq     no_pmmu_stop
        move.l  d0,(a1)
        jra     no_pmmu_putc
no_pmmu_stop:
        stop    #0x2700                 // stays stopped: interrupts masked
        jra     no_pmmu_stop
no_pmmu_msg:
#if CONF_WITH_HIGHER_HALF
        .ascii  "\r\npTOS: this image needs a 68040 or 68060 with an MMU,\r\n"
#else
        .ascii  "\r\npTOS: this image needs a 68030, 68040 or 68060 with an MMU,\r\n"
#endif
        .ascii  "and this CPU has none. Use an image built without\r\n"
        .asciz  "CONF_REQUIRE_PMMU, such as virt-m68k_defconfig.\r\n"
        .even
pmmu_ok:
#endif

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
#if CONF_WITH_HIGHER_HALF
        cmp.l   #0x52414D53,PHYS(ram_size_valid)        // 'RAMS'
        jne     bi_scan
        move.l  PHYS(ram_size_saved),d1
#else
        cmp.l   #0x52414D53,ram_size_valid      // 'RAMS'
        jne     bi_scan
        move.l  ram_size_saved,d1
#endif
        jra     bi_done
bi_scan:
#if CONF_WITH_HIGHER_HALF
        lea     PHYS(__ebss),a0                 // where the list is: physical
#else
        lea     __ebss,a0
#endif
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
#if CONF_WITH_HIGHER_HALF
        cmp.l   #VIRT_MAX_RAM_SIZE,d0   // what the window holds
        jls     bi_size_ok
        move.l  #VIRT_MAX_RAM_SIZE,d0
#else
        cmp.l   #0x7f000000,d0          // keep the size in a positive LONG,
        jls     bi_size_ok              // clear of the devices at 0xff000000
        move.l  #0x7f000000,d0
#endif
bi_size_ok:
        move.l  d0,d1
        jra     bi_done
bi_skip:
        adda.w  d3,a0
        dbra    d2,bi_next
bi_done:
#if CONF_WITH_HIGHER_HALF
        move.l  d1,PHYS(ram_size_saved)
        move.l  #0x52414D53,PHYS(ram_size_valid)

        // The MMU: the tables are built at the top of the RAM, and the RAM
        // that is left is what phystop says.  Back from there the code is
        // still at its physical address, which the tables map too; go to
        // the window.
        move.l  d1,d0
        lea     PHYS(C_SYM(virt_mmu_early)),a0
        jsr     (a0)
        move.l  d0,d1
#if CONF_WITH_PROCESS_ASPACES
        move.l  d0,PHYS(C_SYM(boot_root))       // the tree the MMU was switched on with: a reset needs it back
#endif
        jmp     (high_start).l
high_start:
#else
        move.l  d1,ram_size_saved
        move.l  #0x52414D53,ram_size_valid
#endif

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

#if CONF_WITH_PROCESS_ASPACES
        // the top of the RAM that is left, as an address in the window: with an
        // address space for every process there is no pool at virtual 0
        add.l   #VIRT_WINDOW_VBASE,d1
#endif
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
        .globl  C_SYM(boot_root)
C_SYM(boot_root): .dc.l 0               // (CONF_WITH_PROCESS_ASPACES, see above)
ram_size_valid: .dc.l   0
ram_size_saved: .dc.l   0
