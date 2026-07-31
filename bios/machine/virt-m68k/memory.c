/*
 * memory.c - QEMU virt (m68k) memory initialization
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_VIRT_M68K
#error This file must only be compiled for the QEMU virt (m68k) target
#endif

#include "portab.h"
#include "machine.h"

/*
 * bios/build.mk lists memory.o unconditionally, and vpath resolves it to
 * this file for MACHINE_VIRT_M68K (the way bios/machine/virt-arm/memory.c
 * is resolved for MACHINE_VIRT_ARM). This machine needs no runtime memory
 * initialization of its own beyond the one function below: the RAM size
 * is fixed by the QEMU command line, and startup.S sets _phystop directly
 * before any C code runs.
 */

#if CONF_WITH_ADVANCED_CPU
/*
 * bios/arch/m68k/memory.S's detect_32bit_address_bus() (which every other
 * ARCH_M68K_CLASSIC machine links unmodified) probes for a 32-bit address
 * bus by writing to a fixed low address and checking for its 24-bit
 * wraparound mirror, using a temporary Bus Error vector as a safety net --
 * a real Atari-hardware quirk (the 68000/68010/68EC030's 24-bit-only
 * address bus) that never exists on QEMU's m68k 'virt' board. The board's
 * only selectable CPUs are 68020-68060 (see hw/m68k/virt.c), and there is
 * no chipset that could mirror addresses even if there were: the answer
 * is always 32-bit, so this replaces the Atari-specific probe with that
 * fixed answer instead of linking bios/arch/m68k/memory.S (which this
 * machine's own memory.c already overrides via vpath, the way
 * bios/machine/virt-arm/memory.c overrides it for MACHINE_VIRT_ARM).
 */
BOOL detect_32bit_address_bus(void)
{
    return TRUE;
}
#endif
