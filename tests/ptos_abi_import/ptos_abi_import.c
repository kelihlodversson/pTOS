/*
 * ptos_abi_import.c - regression test for native pTOS ABI imports
 *
 * Launches abi_probe.c (see that file) -- a native ELF program linked
 * against the SDK's libptos-abi.so.1 stub and packed by
 * tools/ptos-elf-pack.c into the .ptos.imports/.ptos.bind format
 * (doc/elfload.txt's "Native pTOS ABI imports" section) -- via Pexec()
 * and checks its exit code. abi_probe.c calls seven imported GEMDOS
 * functions (Fcreate, Fwrite, Fclose, Fopen, Fread, Cconws) with no
 * fallback path: an unresolved import slot left at 0 would jump through
 * a null pointer and fault immediately, so a clean, nonzero-but-
 * expected exit code proves bdos/elfld.c actually resolved every one of
 * them against bdos/ptosabi_gemdos.c's real kernel addresses.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>

void test_ptos_abi_import(void);

void test_ptos_abi_import(void)
{
    long rc;

    ptest_begin("ptos_abi_import");

    rc = Pexec(PE_LOADGO, "C:\\ABIPROBE.TOS", "", 0);
    ptest_assert_msg(rc == 0,
                      "ABIPROBE.TOS did not report a clean roundtrip "
                      "(native pTOS ABI imports not resolved correctly?)");

    ptest_pass();
}
