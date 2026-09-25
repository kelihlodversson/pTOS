/*
 * io.h - x86-64 port I/O and CPU control intrinsics
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef X86_64_IO_H
#define X86_64_IO_H

#include "portab.h"

static inline void x86_64_outb(UWORD port, UBYTE val)
{
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline UBYTE x86_64_inb(UWORD port)
{
    UBYTE val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void x86_64_halt(void)
{
    __asm__ volatile ("hlt");
}

static inline void x86_64_cli(void)
{
    __asm__ volatile ("cli" ::: "memory");
}

/* CR2 holds the linear (faulting) address of the most recent #PF -- read
 * by the panic path (panic.c) while decoding one. */
static inline UQUAD x86_64_read_cr2(void)
{
    UQUAD val;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(val));
    return val;
}

#endif /* X86_64_IO_H */
