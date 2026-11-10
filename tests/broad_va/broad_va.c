/*
 * broad_va.c - high ELF loads, reservations and process isolation (#946)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "test.h"
#include <mint/osbind.h>
#include <mint/basepage.h>
#include <stdio.h>
#include "include/mmapcall.h"

void test_broad_va(void);

static void expect_probe(long rc, const char *what)
{
    char text[120];

    sprintf(text, "%s (%ld)", what, rc);
    ptest_assert_msg(rc == 0, text);
}

static void run_high(void)
{
    BASEPAGE *bp;
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
        bp = (BASEPAGE *)Pexec(PE_LOAD, "C:\\HIIMAGE.TOS", "", 0);
        ptest_assert_msg((long)bp > 0, "high ET_EXEC split load failed");
        if ((long)bp <= 0)
            continue;
        ptest_assert_msg((unsigned long)bp->p_tbase >= 0x41000000UL &&
                         (unsigned long)bp->p_tbase < 0x41100000UL,
                         "split load changed a relocation-free image's VA");
        ptest_assert_msg(mm_map(0x41000000L, 1, PROT_NONE, 1) == -36L,
                         "a pending image's reservation was replaced");
        rc = Pexec(i == 1 ? PE_GO : PE_GO_FREE, "", bp, 0);
        expect_probe(rc, "high ET_EXEC split launch");
        if (i == 1) {
            ptest_assert_msg(mm_map(0x41000000L, 1, PROT_NONE, 1) == -36L,
                             "PE_GO lost the caller's image reservation");
            ptest_assert_msg(Mfree((void *)0x41000000L) == 0,
                             "freeing the returned image failed");
            Mfree(bp->p_env);
            ptest_assert_msg(Mfree(bp) == 0, "freeing split basepage failed");
        }
        rc = mm_map(0x41000000L, 1, PROT_NONE, 1);
        ptest_assert_msg(rc == 0x41000000L, "split image reservation leaked");
        if (rc > 0)
            mm_unmap(rc, 1);
    }
    bp = (BASEPAGE *)Pexec(PE_LOAD, "C:\\HIIMAGE.TOS", "", 0);
    ptest_assert_msg((long)bp > 0, "discard fixture failed to load");
    if ((long)bp > 0) {
        void *env = bp->p_env;

        ptest_assert_msg(Mfree(bp) == 0, "discarding pending high image failed");
        Mfree(env);
        rc = mm_map(0x41000000L, 1, PROT_NONE, 1);
        ptest_assert_msg(rc == 0x41000000L, "discarded high image reservation leaked");
        if (rc > 0)
            mm_unmap(rc, 1);
    }
    /* A live lazy mapping prevents a relocation-free split load; direct
     * execution uses a new address space and can still use its link VA. */
    rc = mm_map(0x41000000L, 1, PROT_NONE, 1);
    ptest_assert_msg(rc == 0x41000000L, "high collision fixture failed");
    if (rc > 0) {
        ptest_assert_msg(Pexec(PE_LOAD, "C:\\HIIMAGE.TOS", "", 0) == -66L,
                         "a relocation-free image was moved over a reservation");
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
    run_high();
    ptest_pass();
}
