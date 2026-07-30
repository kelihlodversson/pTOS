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
 * resolved for MACHINE_RPI). This task's startup.S never calls into any
 * memory or MMU initialization routine -- _main just spins -- so there is
 * nothing to put here yet. A later task (see the comment above _main in
 * startup.S) adds the real MMU bring-up, analogous to raspi_vcmem_init().
 */
