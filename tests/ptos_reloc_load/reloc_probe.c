/*
 * reloc_probe.c - packed-ELF payload for the ptos_reloc_load regression test
 *
 * A standalone executable, not a ptest_* suite -- it is launched by
 * ptos_reloc_load.c's test_ptos_reloc_load() via Pexec(), not linked into
 * runtests.tos. Unlike tests/pie_load/pie_probe.c, it is always
 * post-processed with tools/ptos-elf-pack.c into the compact PT_PTOS_RELOC
 * format documented in doc/elfload.txt, so loading it exercises
 * bdos/elfld.c's elf_relocate_ptos() -- the new code path this test suite
 * covers, which pie_load's payload never reaches. The Makefile links this
 * source differently per architecture: a fixed-base ET_EXEC with "ld -q"
 * (--emit-relocs) on ARM, matching REL-encoded R_ARM_ABS32/RELATIVE whose
 * value is already in the slot; a PIE ET_DYN on m68k, matching RELA-encoded
 * R_68K_RELATIVE whose value is only in r_addend and must be materialised
 * into the slot by ptos-elf-pack at pack time -- so between the two
 * configurations this one source exercises both of elf_relocate_ptos()'s
 * slot sources.
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

/* ptr is a volatile pointer variable so -O2 cannot constant-fold the
 * comparison below and discard it (and the relocation its initializer
 * needs) as dead, which would let this test pass without ever exercising
 * the loader path it exists to cover: every read of *ptr must load ptr's
 * value from memory first, rather than assuming it still equals &target */
static int target = 0x5678;
static int *volatile ptr = &target;

int main(void)
{
    return (*ptr == 0x5678) ? 0 : 1;
}
