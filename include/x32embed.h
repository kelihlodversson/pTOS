/*
 * x32embed.h - embedding an x32 executable in the kernel image (x86-64)
 *
 * X32_EMBED_IMAGE(accessor, label, path) defines `const X32_IMAGE *accessor(void)`
 * returning the bytes of the executable at `path` (relative to the directory
    ".incbin \"" path "\"\n"                                                \
 *
 * Both addresses come from `lea`, not from a C address-of: the kernel's
 * objects are ELF but the final link is PE, which does not relax the GOT
 * loads a plain extern address may compile to (see bios/arch/x86_64/trap.c).
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef X32EMBED_H
#define X32EMBED_H

#include "x32image.h"

#define X32_EMBED_IMAGE(accessor, label, path)                              \
__asm__(                                                                    \
    ".section .rodata\n"                                                    \
    ".balign 16\n"                                                          \
    #label "_start:\n"                                                      \
    ".incbin \"" path "\"\n"                                                \
    #label "_end:\n"                                                        \
    ".byte 0\n"                                                             \
    ".previous\n"                                                           \
);                                                                          \
const X32_IMAGE *accessor(void)                                             \
{                                                                           \
    static X32_IMAGE image;                                                 \
                                                                            \
    if (!image.data) {                                                      \
        const UBYTE *start, *end;                                           \
                                                                            \
        __asm__("lea " #label "_start(%%rip), %0" : "=r"(start));           \
        __asm__("lea " #label "_end(%%rip), %0" : "=r"(end));               \
        image.size = (ULONG)(end - start);                                  \
        image.data = start;                                                 \
    }                                                                       \
    return &image;                                                          \
}

#endif /* X32EMBED_H */
