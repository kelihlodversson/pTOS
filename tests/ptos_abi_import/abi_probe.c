/*
 * abi_probe.c - packed-ELF payload for the ptos_abi_import regression test
 *
 * A standalone executable, launched by ptos_abi_import.c's
 * test_ptos_abi_import() via Pexec(), not linked into runtests.tos --
 * exactly the same shape as tests/ptos_reloc_load/reloc_probe.c, but
 * exercising the native pTOS ABI import mechanism (doc/elfload.txt)
 * instead of internal load relocations.
 *
 * Links against the SDK's libptos-abi.so.1 stub (built from
 * stub_gemdos.c) as a PIE ("-pie --no-dynamic-linker"), so the linker
 * produces real R_*_JUMP_SLOT/R_*_GLOB_DAT relocations against
 * undefined "gemdos:*" dynamic symbols; tools/ptos-elf-pack.c converts
 * those into .ptos.imports/.ptos.bind, and bdos/elfld.c resolves them
 * against bdos/ptosabi_gemdos.c's real kernel addresses at Pexec() time.
 *
 * Round-trips a fixed string through Fcreate/Fwrite/Fclose/Fopen/Fread/
 * Fclose and byte-compares what comes back, then prints a line with
 * Cconws -- seven distinct imported GEMDOS calls, each only reachable by
 * a correctly resolved import: an unresolved slot left at 0 would jump
 * through a null pointer and fault before any of this could return
 * normally, exactly like an unrelocated pointer would in reloc_probe.c.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "ptos-abi/gemdos.h"

#define TEST_PATH   "C:\\ABIPROBE.TXT"
#define TEST_CONTENT "ptos native ABI import roundtrip\r\n"
#define TEST_LEN    ((long)(sizeof(TEST_CONTENT) - 1))

/* encode an out-of-range diagnostic value into the process exit code so
 * it shows up directly in EmuCON's "error code N" -- a small, fixed
 * offset per checkpoint keeps each one in a disjoint, easily recognised
 * band instead of colliding with an ordinary GEMDOS error code. */
static int diag(int checkpoint, long value)
{
    if (value < 0)
        value = -value;
    return 100 * checkpoint + (int)(value % 100);
}

int main(void)
{
    long h;
    long n;
    char buf[64];
    int i;

    h = Fcreate(TEST_PATH, 0);
    if (h < 0)
        return diag(1, h);
    n = Fwrite((PTOS_WORD)h, TEST_LEN, TEST_CONTENT);
    if (n != TEST_LEN)
        return diag(2, n);
    if (Fclose((PTOS_WORD)h) != 0)
        return diag(3, 0);

    h = Fopen(TEST_PATH, 0);
    if (h < 0)
        return diag(4, h);
    n = Fread((PTOS_WORD)h, (long)sizeof(buf), buf);
    if (n != TEST_LEN)
        return diag(5, n);
    if (Fclose((PTOS_WORD)h) != 0)
        return diag(6, 0);

    for (i = 0; i < (int)TEST_LEN; i++)
    {
        if (buf[i] != TEST_CONTENT[i])
            return diag(7, i);
    }

    Cconws("ptos_abi_import probe: roundtrip ok\r\n");

    return 0;
}
