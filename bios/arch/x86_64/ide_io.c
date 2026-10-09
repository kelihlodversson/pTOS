/*
 * ide_io.c - x86-64 port I/O primitives for bios/ide.c's ATA register access
 *
 * See ide_io.h's own comment for why this exists separately from the
 * shared, memory-mapped-register ATA protocol logic in bios/ide.c.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "ide_io.h"

#if CONF_WITH_IDE

UWORD x86_64_ide_inw(UWORD port)
{
    UWORD val;

    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

void x86_64_ide_outw(UWORD port, UWORD val)
{
    __asm__ volatile ("outw %0, %1" :: "a"(val), "Nd"(port));
}

/*
 * `rep insw`/`rep outsw` auto-increment (R)DI/(R)SI and decrement (R)CX
 * to zero; the direction flag is guaranteed clear on entry to (and
 * between) any C function under the SysV ABI, so no explicit `cld` is
 * needed here, matching how GCC itself emits `rep movs`-family
 * instructions for things like memcpy(). `+D`/`+S`/`+c` (read-write)
 * rather than plain input constraints because the instruction itself
 * updates all three; the "memory" clobber covers the block `buffer`
 * itself, which the constraints alone don't describe.
 */
void x86_64_ide_insw(UWORD port, void *buffer, ULONG count)
{
    __asm__ volatile ("rep insw"
                       : "+D"(buffer), "+c"(count)
                       : "d"(port)
                       : "memory");
}

void x86_64_ide_outsw(UWORD port, const void *buffer, ULONG count)
{
    __asm__ volatile ("rep outsw"
                       : "+S"(buffer), "+c"(count)
                       : "d"(port)
                       : "memory");
}

#endif /* CONF_WITH_IDE */
