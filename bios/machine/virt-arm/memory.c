/*
 * memory.c - QEMU virt (ARM) memory initialization placeholder
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_VIRT_ARM
#error This file must only be compiled for the QEMU virt (ARM) target
#endif

/*
 * bios/build.mk lists memory.o unconditionally, and vpath resolves it to
 * this file for MACHINE_VIRT_ARM (the way bios/machine/raspi/memory.c is
 * resolved for MACHINE_RPI). This machine needs no runtime memory
 * initialization of its own: the RAM size is fixed by the QEMU command
 * line and startup.S sets phystop directly, and the MMU bring-up lives in
 * virt_mmu.c, called from startup.S before any C code runs. So this file
 * is deliberately empty.
 */
