/*
 * emutos.ld - Custom linker script for EmuTOS ROM
 *
 * Copyright (C) 2016-2024 The EmuTOS development team
 *
 * Authors:
 *  VRI   Vincent Rivière
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/* Note: this linker script is preprocessed,
 * to allow #include, #define, #if, etc. */
#include "include/config.h"

#if !defined(MACHINE_RPI) && !defined(MACHINE_VIRT_ARM) && !defined(MACHINE_VIRT_M68K)
/* Create a plain binary file, without any header (not possible on ARM).
 * MACHINE_VIRT_M68K is excluded for a different reason than the ARM
 * machines: it needs a real ELF image for QEMU's -kernel loader (see
 * hw/m68k/virt.c's use of load_elf()), not a flat binary -- unlike every
 * other m68k target, none of which boot via an ELF loader. */
OUTPUT_FORMAT(binary)
#endif

#if defined(MACHINE_VIRT_M68K)
/* m68k-atari-mintelf-ld's default output target for this toolchain is
 * elf32-atariprg (tailored to produce MiNT PRG executables via mintbin),
 * not a generic ELF32; QEMU's -kernel loader (hw/m68k/virt.c) needs a
 * plain ELF image, and elf32-atariprg's relocation handling also rejects
 * some relocations this link produces. Force the generic target instead. */
OUTPUT_FORMAT(elf32-m68k)
#endif

#if defined(MACHINE_VIRT_ARM) || defined(MACHINE_VIRT_M68K)
/* Both -kernel-loading QEMU boards otherwise rely on the linker's
 * last-resort "address of .text" fallback to pick an entry point, which
 * only works because startup.o happens to be first in obj-y (see
 * bios/build.mk). Naming the real entry point removes that silent
 * -failure-mode dependency for the two targets that actually need one. */
ENTRY(_os_entry)
#endif

#if !EMUTOS_LIVES_IN_RAM || defined (MACHINE_RPI)
/* The ROM address depends on the target.  MACHINE_RPI needs this even
 * though EMUTOS_LIVES_IN_RAM is set for it: unlike EmuTOS's other
 * RAM-resident targets, its .text is still placed at a fixed address
 * (the ARM kernel load address) rather than following REGION_RAM. */
#if defined(MACHINE_AMIGA)
# define ROM_ORIGIN 0x00fc0000
#elif defined (MACHINE_RPI)
# define ROM_ORIGIN 0x00008000
#elif defined (TARGET_CART)
# define ROM_ORIGIN 0x00fa0000
#elif defined(TARGET_192)
# define ROM_ORIGIN 0x00fc0000
#else
# define ROM_ORIGIN 0x00e00000
#endif
#endif /* !EMUTOS_LIVES_IN_RAM || MACHINE_RPI */

#if !EMUTOS_LIVES_IN_RAM
/* Always assume there is a lot of ROM space, so the link will always succeed.
 * Then our padding tool will eventually report an overflow, with a better
 * diagnostic message than the linker.
 */
#define ROM_LENGTH 1M /* Maximum size before I/O area */

#endif /* !EMUTOS_LIVES_IN_RAM */

/* Memory regions of a typical Atari computer.
 * Only start addresses matter. Lengths just need to be large enough.
 */
MEMORY
{
    stram : ORIGIN = 0x00000000, LENGTH = 14M
#if !EMUTOS_LIVES_IN_RAM
    rom : ORIGIN = ROM_ORIGIN, LENGTH = ROM_LENGTH
#endif
#if CONF_WITH_STATIC_ALT_RAM
    altram : ORIGIN = STATIC_ALT_RAM_ADDRESS, LENGTH = 1M
#endif
}

/* Region used as read/write memory */
#if CONF_WITH_STATIC_ALT_RAM
REGION_ALIAS("REGION_RAM", altram)
#else
REGION_ALIAS("REGION_RAM", stram)
#endif

/* Regions used by EmuTOS sections */
#if EMUTOS_LIVES_IN_RAM
REGION_ALIAS("REGION_READ_ONLY", REGION_RAM)
#else
REGION_ALIAS("REGION_READ_ONLY", rom)
#endif
REGION_ALIAS("REGION_READ_WRITE", REGION_RAM)

/* On the QEMU ARM 'virt' machine, RAM starts at physical 0x40000000, but
 * the image is still linked (VMA) at the same low addresses as every
 * other machine so the fixed-address TOS system variables keep working.
 * LMA_AT(section) makes QEMU load that stram section's bytes at
 * link_address + 0x40000000; a static MMU table built early in
 * startup.S then maps virtual 0x0-and-up back onto that physical
 * window. See docs/superpowers/plans/2026-07-30-arm-virt-support.md.
 *
 * This is deliberately AT(ADDR(section) + 0x40000000) -- an explicit
 * per-section load address -- rather than a shared "AT>stram_lma"
 * load memory region: several of these sections (.first_stram,
 * .low_stram, .stack, .laststram) hold no actual data, just
 * location-counter reservations, so GNU ld emits them as SHT_NOBITS.
 * A load memory region's address only advances by the sections
 * explicitly assigned to it, but NOBITS sections do not consume any
 * of that region's space, so a shared AT>region silently stops
 * tracking "VMA + 0x40000000" as soon as a NOBITS section intervenes
 * -- every later section collapses back onto the region's origin
 * instead of its correct offset. AT(ADDR(section) + ...) computes
 * each section's own load address directly and is immune to that.
 *
 * This also relies on an implicit fixup in QEMU's -kernel ELF loader:
 * the image's e_entry is a *virtual* address (low ST-RAM, which pre-MMU
 * is flash on this board, not RAM), and QEMU rewrites it to the matching
 * physical address whenever a segment has p_vaddr != p_paddr, i.e. by
 * the same +0x40000000 offset LMA_AT applies here.  That rewrite only
 * happens when the entry point falls inside an executable (PF_X),
 * non-empty (p_filesz-covered) segment.  If a future layout change ever
 * breaks that condition -- an empty or non-executable first segment, or
 * an entry point outside it -- QEMU silently jumps to the unmapped
 * virtual address instead, with no diagnostic of any kind. */
#if defined (MACHINE_VIRT_ARM)
# define LMA_AT(section) AT(ADDR(section) + 0x40000000)
#else
# define LMA_AT(section)
#endif

SECTIONS
{
    /* First section in ST-RAM */
    .first_stram : LMA_AT(.first_stram)
    {
        /* System variables are located at fixed addresses */
#include "tosvars.ld"
        . = 0x800; /* Extend sysvars area upto end of supervisor-only RAM */
        _sysvars_end = .;
    } >stram

#if CONF_WITH_STATIC_ALT_RAM
    /* First section in static Alt-RAM */
    .first_altram :
    {
        __static_altram_start = .;
    } >altram
#endif

#if !defined (MACHINE_RPI) && !defined (MACHINE_VIRT_ARM)
    /* This section is the internal BIOS stack.
     * It will *not* be cleared on startup or reset.
     */
    .stack : LMA_AT(.stack)
    {
        _stkbot = .;
        . += 2K;
        _stktop = .;
    } >REGION_READ_WRITE
#endif
    /* SUBALIGN doesn't just bound the gap between input sections here --
     * per the ld manual it *overrides* each input section's own requested
     * alignment, "whether larger or smaller". SUBALIGN(2) is deliberate
     * for m68k (packs the ROM tightly; m68k tolerates the reduced
     * alignment), but on ARM it silently downgrades any object that needs
     * more than that -- ordinary 4-byte-aligned code and pointers, or a
     * few larger asks like the raspi MMU code's 1KB-aligned coarse page
     * table -- producing undefined-instruction traps (.text) or alignment
     * data aborts (.data/.bss) wherever an object lands short of the
     * alignment it actually needs. Omitting SUBALIGN restores the default
     * behavior of honoring each input section's own alignment, which is
     * what ARM needs; m68k keeps its explicit override.
     */
#if ARCH_ARM
# define MIN_SUBALIGN
#else
# define MIN_SUBALIGN SUBALIGN(2)
#endif

    /* This section is located as low as possible in ST-RAM,
     * but after eventual BIOS stack.
     * Variables requiring very low addresses, while being accessible
     * from user mode, can be put here.
     */
    .low_stram : LMA_AT(.low_stram) MIN_SUBALIGN
    {
        __low_stram_start = .;
        obj/lowstram.o(.bss COMMON)
        __low_stram_end = .;
        ASSERT(ABSOLUTE(_shifty) < 0x8000, "error: bad _shifty address");
        ASSERT((ABSOLUTE(_dskbuf) & 3) == 0, "error: bad _dskbuf alignment");
        ASSERT(ABSOLUTE(__low_stram_end) <= 0x8000, "error: .low_stram section extends beyond 32k");
    } >stram

    /* The TEXT segment contains EmuTOS executable code and read-only data.
     * It is always read-only, so it can safely live in ROM.
     */
#if defined (MACHINE_RPI)
     .text ROM_ORIGIN : MIN_SUBALIGN
#else
     .text : LMA_AT(.text) MIN_SUBALIGN
#endif
    {
        CREATE_OBJECT_SYMBOLS
        __text = .;
        *(.text .rodata .rodata.*)
        /* Note: .rodata sections are only present in ELF objects.
         * We mix .text and .rodata to keep critical parts (such as BIOS
         * and fonts) at the beginning of the ROM.
         */
        __etext = .;
    } >REGION_READ_ONLY =0x4afc

    /* FIXME: Our DATA segment is currently read-only.
     * It currently lives in the ROM, just after the TEXT segment.
     * This means that initialized global variables can't be modified.
     * Since a read-only DATA segment is useless, we try to keep it empty by
     * making all initialized variables const, so they go to the TEXT segment.
     */
    .data : LMA_AT(.data) MIN_SUBALIGN
    {
        __data = .;
        *(.data)
        __edata = .;
    } >REGION_READ_ONLY

    /* The BSS segment contains uninitialized global variables.
     * It will be cleared by the startup code.
     */
    .bss : LMA_AT(.bss) MIN_SUBALIGN
    {
        __bss = .;
        *(.bss COMMON)
        __ebss = .;
    } >REGION_READ_WRITE

#if defined (MACHINE_RPI) || defined (MACHINE_VIRT_ARM)
    /* On the ARM machines (RPI, QEMU virt) we put the BIOS stack /after/
     * the rom data.  We have more RAM, so make it a little bigger than in
     * the ST case.
     */
    .stack : LMA_AT(.stack)
    {
        /* _stktop is shared by _biosmain and by every ARM exception mode's
         * initial sp (IRQ/ABT/UND/SVC/FIQ; see each machine's startup.S).
         * AAPCS requires sp to be 8-byte aligned at every public interface,
         * which gcc's auto-vectorizer (-mfpu=neon-vfpv4) relies on, but
         * nothing otherwise constrains where the preceding .bss section
         * happens to end, so force it here.
         */
        . = ALIGN(8);
        _stkbot = .;
        . += 32K;
        _stktop = .;
    } >REGION_READ_WRITE
#endif

    /* This section is always the last one stored in ST-RAM.
     * It is usually empty, just used to calculate the last address
     * of ST-RAM statically used by EmuTOS. If needed, the address
     * is rounded up to the next multiple of four.
     */
    .laststram : LMA_AT(.laststram) ALIGN(4)
    {
        __end_os_stram = .;
    } >stram

#if CONF_WITH_STATIC_ALT_RAM
    /* Last section in static Alt-RAM */
    .last_altram :
    {
        __static_altram_end = .;
    } >altram
#endif

    /* Discard the following ELF sections.
     * Some of them may be present in ELF libgcc.a.
     */
    /DISCARD/ :
    {
        *(.comment)
#if 0
        *(.debug*)
#endif
    }
}

#ifdef ELF_TOOLCHAIN
/* Our code is compiled with -fleading-underscore, so the references to external
 * libraries will include a leading underscore. However, libgcc was not compiled
 * with that option, so its symbols do not start with a leading underscore. So
 * we need to define leading-underscore aliases to the no-leading-underscore
 * functions in libgcc.
 */
#define DEFINE_SYMBOL_ALIAS(alias, impl) EXTERN(impl) PROVIDE(alias = impl);
#define ELF_LIB_REF(symbol) DEFINE_SYMBOL_ALIAS(_##symbol, symbol)
#if !defined (MACHINE_RPI)
ELF_LIB_REF(__mulsi3)
ELF_LIB_REF(__divsi3)
ELF_LIB_REF(__modsi3)
ELF_LIB_REF(__udivsi3)
ELF_LIB_REF(__umodsi3)
ELF_LIB_REF(__bswapsi2)
ELF_LIB_REF(__aeabi_udiv)
ELF_LIB_REF(__aeabi_udivmod)
#endif
ELF_LIB_REF(__aeabi_idiv)
ELF_LIB_REF(__aeabi_idivmod)
ELF_LIB_REF(__aeabi_uidiv)
ELF_LIB_REF(__aeabi_uidivmod)
#endif
