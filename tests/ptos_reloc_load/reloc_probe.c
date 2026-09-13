/*
 * reloc_probe.c - packed-ELF payload for the ptos_reloc_load regression test
 *
 * A standalone executable, not a ptest_* suite -- it is launched by
 * ptos_reloc_load.c's test_ptos_reloc_load() via Pexec(), not linked into
 * runtests.tos. Unlike tests/pie_load/pie_probe.c (a position-independent
 * ET_DYN exercising elf_pgmld()'s .rel.dyn/.rela.dyn path), this one is
 * linked as a fixed-base ET_EXEC with "ld -q" (--emit-relocs) and then
 * post-processed with tools/ptos-elf-pack.c into the compact PT_PTOS_RELOC
 * format documented in doc/elfload.txt, so loading it exercises
 * bdos/elfld.c's elf_relocate_ptos() -- the new code path this test suite
 * covers, which pie_load's ET_DYN payload never reaches.
 *
 * A statically initialized pointer to another global forces the linker to
 * emit an absolute relocation: without one, this program would need no
 * relocations at all and the loader path under test would never run.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

static int target = 0x5678;
static int *ptr = &target;

int main(void)
{
    return (*ptr == 0x5678) ? 0 : 1;
}
