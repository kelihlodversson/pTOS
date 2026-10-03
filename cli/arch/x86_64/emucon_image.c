/*
 * emucon_image.c - the built-in x32 EmuCON, embedded in the kernel image
 *
 * EmuCON is not part of the LP64 kernel on x86-64.  The top level Makefile
 * builds it as a separate x32 (ILP32) ELF executable, obj/x32/emucon.elf,
 * from the very same cli sources the other architectures use; this file
 * only carries that executable's bytes in the kernel's read-only data, for
 * bios.c to launch as an ordinary ring-3 process (include/x32image.h,
 * bdos/kproc.c).
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "x32embed.h"

X32_EMBED_IMAGE(x86_64_emucon_image, x86_64_emucon_elf, "obj/x32/emucon.elf")
