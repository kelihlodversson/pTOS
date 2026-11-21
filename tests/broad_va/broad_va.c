/*
 * broad_va.c - high ELF loads, reservations and process isolation (#946)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "test.h"
#include <mint/osbind.h>
#include <stdio.h>
#include "include/mmapcall.h"

void test_broad_va(void);

static char probe_failure[120];

static void expect_probe(long rc, const char *what)
{
    /* testlib keeps the first failure's pointer until ptest_pass(). */
    if (rc && !probe_failure[0])
        sprintf(probe_failure, "%s (%ld)", what, rc);
    ptest_assert_msg(rc == 0, probe_failure);
}

static void run_high(void)
{
    long rc, address;
    int i;

    address = mm_map(0x10000L, 1, PROT_READ | PROT_WRITE, 1);
    ptest_assert_msg(address == 0x10000L, "parent low mapping failed");
    if (address != 0x10000L)
        return;
    *(volatile long *)address = 0x12345678L;
    for (i = 0; i < 3; i++) {
        rc = Pexec(PE_LOADGO, "C:\\HIIMAGE.TOS", "", 0);
        expect_probe(rc, "high ET_EXEC / low RAM direct probe");
        ptest_assert_msg(*(volatile long *)address == 0x12345678L,
                         "a child's low mapping changed the parent's memory");
    }
    /* A live lazy mapping of the parent at the child's link VA: direct
     * execution uses a new address space and can still use that VA. (An
     * ELF split-loaded into the caller with Pexec(3) is gone, #1018.) */
    rc = mm_map(0x41000000L, 1, PROT_NONE, 1);
    ptest_assert_msg(rc == 0x41000000L, "high collision fixture failed");
    if (rc > 0) {
        ptest_assert_msg(Pexec(PE_LOADGO, "C:\\HIIMAGE.TOS", "", 0) == 0,
                         "parent reservation leaked into the child's VA space");
        mm_unmap(rc, 1);
    }
    ptest_assert_msg(*(volatile long *)address == 0x12345678L,
                     "child mappings were not isolated");
    mm_unmap(address, 1);
}

void test_broad_va(void)
{
    ptest_begin("broad_va");
    probe_failure[0] = '\0';
    run_high();
    ptest_pass();
}
