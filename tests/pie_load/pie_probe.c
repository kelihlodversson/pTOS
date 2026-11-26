/*
 * pie_probe.c - PIE payload for the pie_load regression test
 *
 * A standalone executable, not a ptest_* suite -- it is launched by
 * pie_load.c's test_pie_load() via Pexec(), not linked into runtests.tos.
 * It must be built and linked as a position-independent ET_DYN
 * ("ld -pie --no-dynamic-linker") so that loading it exercises
 * elf_pgmld()'s handling of the .rel.dyn / .rela.dyn dynamic relocations
 * a PIE carries (see bdos/elfld.c). A statically initialized pointer to
 * another global forces the linker to emit one: without any absolute
 * pointer in initialized data, a PIE needs no relocations at all and the
 * loader path under test would never run.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#if defined(__arm__) && defined(TEST_NATIVE_ELF_ENTRY) && !defined(TEST_WITH_USER_ASPACE)
#include <mint/basepage.h>
#include <stdint.h>
#include <string.h>
#endif

static int target = 0x1234;
static int *ptr = &target;

int main(int argc, char **argv, char **envp)
{
#if defined(__arm__) && defined(TEST_NATIVE_ELF_ENTRY) && !defined(TEST_WITH_USER_ASPACE)
    const BASEPAGE *bp = _base;

    if (argc != 3 || strcmp(argv[0], "custom name") || strcmp(argv[1], "two words") ||
        argv[2][0] || argv[3] || !envp[0] || strcmp(envp[0], "CHECK_LAYOUT=1") ||
        !envp[1] || envp[1][0] || !envp[2] || strcmp(envp[2], "AFTER_EMPTY=1") || envp[3])
        return 2;
    if (!bp || (uintptr_t)bp->p_dbase != (uintptr_t)bp + sizeof(*bp) + bp->p_tlen ||
        (uintptr_t)bp->p_bbase != (uintptr_t)bp->p_dbase + bp->p_dlen ||
        (uintptr_t)bp->p_bbase + bp->p_blen > (uintptr_t)bp->p_hitpa)
        return 3;
#else
    (void)argc;
    (void)argv;
    (void)envp;
#endif
    return (*ptr == 0x1234) ? 0 : 1;
}
