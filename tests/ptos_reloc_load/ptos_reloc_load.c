/*
 * ptos_reloc_load.c - regression test for the compact PT_PTOS_RELOC loader
 *
 * Launches reloc_probe.c, a separate executable (see that file for its
 * per-architecture link recipe) repacked by tools/ptos-elf-pack.c with
 * --strip-shdr into the compact .ptos.reloc / PT_PTOS_RELOC format
 * (doc/elfload.txt) -- --strip-shdr leaves no SHT_REL/SHT_RELA fallback
 * reachable, so this can only pass by way of the program-header path
 * actually running -- via Pexec() and checks its exit code.
 * reloc_probe.c dereferences a global pointer that only resolves
 * correctly if bdos/elfld.c's elf_relocate_ptos() applied the packed
 * relocation stream from the program header -- exactly the new code
 * path #309 added, which the existing pie_load suite's unpacked ET_DYN
 * payload does not exercise.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>

void test_ptos_reloc_load(void);

void test_ptos_reloc_load(void)
{
    long rc;

    ptest_begin("ptos_reloc_load");

    rc = Pexec(PE_LOADGO, "C:\\PTRELOC.TOS", "", 0);
    ptest_assert_msg(rc == 0,
                      "PTRELOC.TOS exited nonzero "
                      "(packed PT_PTOS_RELOC relocations not applied?)");

    ptest_pass();
}
