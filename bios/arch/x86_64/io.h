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

/* RDMSR/WRMSR (Intel SDM Vol 2B): the MSR index is always a 32-bit value
 * in %ecx regardless of the value's own width, and the 64-bit value
 * itself is split across %edx:%eax (high:low) on both instructions --
 * used by trap.c to program IA32_STAR/LSTAR/FMASK for `syscall`. */
static inline UQUAD x86_64_rdmsr(ULONG msr)
{
    ULONG lo, hi;

    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((UQUAD)hi << 32) | lo;
}

static inline void x86_64_wrmsr(ULONG msr, UQUAD value)
{
    ULONG lo = (ULONG)value;
    ULONG hi = (ULONG)(value >> 32);

    __asm__ volatile ("wrmsr" :: "c"(msr), "a"(lo), "d"(hi) : "memory");
}

#endif /* X86_64_IO_H */
