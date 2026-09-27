/*
 * ide_io.h - x86-64 port I/O primitives for bios/ide.c's ATA register access
 *
 * A legacy PC IDE controller's ATA task-file registers are port-mapped
 * (in/out at fixed I/O ports), unlike every other machine bios/ide.c
 * supports, which memory-maps them (a real struct with byte offsets,
 * dereferenced directly). This header gives ide.c's own x86-64 register-
 * access macros (see its MACHINE_PC_X86_64 branch) the primitives they
 * need instead, so the shared ATA protocol logic in ide.c itself stays
 * arch-neutral.
 *
 * Single-register access (inb()/outb()) reuses bios/arch/x86_64/io.h's
 * existing x86_64_inb()/x86_64_outb() -- this file only adds what that
 * one doesn't already have: word-wide single-register access and the
 * block transfer primitives PIO sector reads/writes need.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef X86_64_IDE_IO_H
#define X86_64_IDE_IO_H

#include "io.h"     /* x86_64_inb()/x86_64_outb(), reused directly */

UWORD x86_64_ide_inw(UWORD port);
void x86_64_ide_outw(UWORD port, UWORD val);

/*
 * Block-transfers `count` UWORDs between `port` and `buffer` via the
 * x86 `rep insw`/`rep outsw` string instructions -- the port-I/O
 * equivalent of bios/ide.c's own hand-unrolled m68k `move.w` loop
 * (ide_get_data()/ide_put_data()'s __x86_64__ branch calls these
 * instead of that loop). `buffer` need not be aligned.
 */
void x86_64_ide_insw(UWORD port, void *buffer, ULONG count);
void x86_64_ide_outsw(UWORD port, const void *buffer, ULONG count);

#endif /* X86_64_IDE_IO_H */
