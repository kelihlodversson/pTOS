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
 * one object file twice, with different flags (TEST_PTOS_RELOC_LDFLAGS/
 * TEST_PTOS_RELOC_LDFLAGS2), into PTRELOC.TOS and PTRELOC2.TOS: a
 * fixed-base ET_EXEC with "ld -q" (--emit-relocs), whose DIR32 relocation
 * (R_ARM_ABS32 on ARM, R_68K_32 on m68k) already has its value in the
 * slot either way; and a PIE ET_DYN with "-pie --no-dynamic-linker",
 * whose RELATIVE relocation's value is already in the slot under ARM's
 * REL encoding but only in m68k's RELA r_addend field, needing
 * materialisation into the slot by ptos-elf-pack at pack time. Between
 * the two link shapes, on both architectures, this one source exercises
 * every relocation type/encoding combination elf_relocate_ptos() and
 * ptos-elf-pack support.
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
