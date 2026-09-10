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

static int target = 0x1234;
static int *ptr = &target;

int main(void)
{
    return (*ptr == 0x1234) ? 0 : 1;
}
