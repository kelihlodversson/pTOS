/*
 * physmem.c - how much physical memory a PC has, for Sysconf()
 *
 * What the page allocator (pmem.c) manages, and what of it is free.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_PC_X86_64
#error This file must only be compiled for the pc-x86_64 machine
#endif

#include "portab.h"
#include "biosext.h"
#include "pmem.h"

LONG phys_pages_total(void)
{
    return (LONG)(pmem_managed_bytes() / 4096ULL);
}

LONG phys_pages_free(void)
{
    return (LONG)(pmem_free_bytes() / 4096ULL);
}
