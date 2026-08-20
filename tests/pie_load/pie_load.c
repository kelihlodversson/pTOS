/*
 * pie_load.c - regression test for the ELF loader's PIE support
 *
 * Launches pie_probe.c, a separate executable built and linked as a
 * position-independent ET_DYN ("ld -pie --no-dynamic-linker"), via
 * Pexec() and checks its exit code. pie_probe.c dereferences a global
 * pointer that only resolves correctly if elf_pgmld() applied the
 * dynamic (.rel.dyn / .rela.dyn) relocations a PIE carries -- exactly
 * the code path a prior regression silently skipped whenever a
 * relocation's sh_info was 0 (see bdos/elfld.c).
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>

void test_pie_load(void);

void test_pie_load(void)
{
    long rc;

    ptest_begin("pie_load");

    rc = Pexec(PE_LOADGO, "C:\\PIEPROBE.TOS", "", 0);
    ptest_assert_msg(rc == 0,
                      "PIEPROBE.TOS exited nonzero "
                      "(PIE dynamic relocations not applied?)");

    ptest_pass();
}
