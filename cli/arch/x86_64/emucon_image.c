/*
 * emucon_image.c - the built-in x32 EmuCON, embedded in the kernel image
 *
 * EmuCON is not part of the LP64 kernel on x86-64.  The top level Makefile
 * builds it as a separate x32 (ILP32) ELF executable, obj/x32/emucon.elf,
 * from the very same cli sources the other architectures link in; this
 * file only carries that executable's bytes in the kernel's read-only data,
 * for bios.c to launch as an ordinary ring-3 process
 * (include/x32image.h, bdos/kproc.c).
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "x32image.h"

#ifndef X32_EMUCON_ELF
#error X32_EMUCON_ELF must name the built EmuCON executable (see the Makefile)
#endif

__asm__(
    ".section .rodata\n"
    ".balign 16\n"
    "x86_64_emucon_elf_start:\n"
    ".incbin \"" X32_EMUCON_ELF "\"\n"
    "x86_64_emucon_elf_end:\n"
    ".byte 0\n"
    ".previous\n"
);

/*
 * Both addresses come from `lea`, not from a C address-of: the kernel's
 * objects are ELF but the final link is PE, which does not relax the GOT
 * loads a plain extern address may compile to (see bios/bios.c's note on
 * exec_os in its history and bios/arch/x86_64/trap.c).
 */
const X32_IMAGE *x86_64_emucon_image(void)
{
    static X32_IMAGE image;

    if (!image.data) {
        const UBYTE *start, *end;

        __asm__("lea x86_64_emucon_elf_start(%%rip), %0" : "=r"(start));
        __asm__("lea x86_64_emucon_elf_end(%%rip), %0" : "=r"(end));
        image.size = (ULONG)(end - start);
        image.data = start;
    }
    return &image;
}
