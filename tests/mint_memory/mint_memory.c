/*
 * mint_memory.c - regression test for the MiNT memory calls
 *
 * Mxalloc() with the protection bits of its mode word, Maccess() and
 * Mvalidate(), where processes have address spaces of their own (S_OSFEATURES
 * bit 0 of Ssystem(), the x86-64 port).  Elsewhere the suite passes without
 * testing anything.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>

#define EINVFN      (-32L)
#define EINVAL      (-25L)
#define ESRCH       (-20L)

#define MXT_PRIVATE  0x0010      /* the protection bits plus one: private */
#define MXT_GLOBAL   0x0020
#define MXT_SUPER    0x0030
#define MXT_READABLE 0x0040
#define MXT_INVALID  0x0050
#define MXT_CHANGE   0x0008
#define MXT_ZERO     0x2000
#define MXT_KEEP     0x4000

void test_mint_memory(void);

void test_mint_memory(void)
{
    char *block;
    long flags, rc;

    ptest_begin("mint_memory");

    if (Ssystem(-1, 7, 0L) == EINVFN || !(Ssystem(7, 0L, 0L) & 1))
    {
        ptest_pass();               /* no address spaces of their own here */
        return;
    }

    /* the modes a private block can have: the plain ones, private, zeroed */
    block = (char *)Mxalloc(8192L, 0);
    ptest_assert_msg(block != 0 && (long)block > 0, "Mxalloc(8192, 0) failed");
    Mfree(block);
    block = (char *)Mxalloc(8192L, 3);
    ptest_assert_msg(block != 0 && (long)block > 0, "Mxalloc(8192, prefer TT) failed");
    Mfree(block);
    block = (char *)Mxalloc(8192L, MXT_PRIVATE);
    ptest_assert_msg(block != 0 && (long)block > 0, "Mxalloc(8192, private) failed");
    Mfree(block);
    block = (char *)Mxalloc(8192L, MXT_ZERO);
    ptest_assert_msg(block != 0 && (long)block > 0, "Mxalloc(8192, zeroed) failed");

    /* ... and the ones it cannot: refused, not allocated as something else */
    ptest_assert_msg(Mxalloc(8192L, MXT_GLOBAL) == EINVFN, "Mxalloc(global) was not refused");
    ptest_assert_msg(Mxalloc(8192L, MXT_SUPER) == EINVFN, "Mxalloc(super) was not refused");
    ptest_assert_msg(Mxalloc(8192L, MXT_READABLE) == EINVFN, "Mxalloc(readable) was not refused");
    ptest_assert_msg(Mxalloc(8192L, MXT_INVALID) == EINVFN, "Mxalloc(invalid) was not refused");
    ptest_assert_msg(Mxalloc(8192L, MXT_CHANGE) == EINVFN, "Mxalloc(change) was not refused");
    ptest_assert_msg(Mxalloc(8192L, MXT_KEEP) == EINVFN, "Mxalloc(keep) was not refused");

    if (block && (long)block > 0)
    {
        /* Maccess(): the block, from its first to its last byte, read and write */
        ptest_assert_msg(Maccess(block, 8192L, 0) == 0, "Maccess(block, read/write) failed");
        ptest_assert_msg(Maccess(block, 8192L, 1) == 0, "Maccess(block, read) failed");
        ptest_assert_msg(Maccess(block + 8191, 1L, 0) == 0, "Maccess(last byte) failed");
        /* nothing was backed for the asking: the block can be asked for again */
        ptest_assert_msg(Maccess(block, 8192L, 0) == 0, "Maccess(block) a second time failed");

        /* what is not the process's: a page that is not mapped, a range that runs
         * out of the block, a mode that does not exist, an empty range */
        ptest_assert_msg(Maccess((void *)0x100, 16L, 1) == EINVAL, "Maccess(unmapped) was not refused");
        ptest_assert_msg(Maccess(block, 8192L + 4096L * 1024L, 1) == EINVAL,
                         "Maccess(beyond the block) was not refused");
        ptest_assert_msg(Maccess(block, 16L, 2) == EINVAL, "Maccess(mode 2) was not refused");
        ptest_assert_msg(Maccess(block, 0L, 0) == EINVAL, "Maccess(size 0) was not refused");

        /* Mvalidate(): the caller's own memory, and the protection bits it has */
        flags = -1L;
        rc = Mvalidate(0, block, 8192L, &flags);
        ptest_assert_msg(rc == 0, "Mvalidate(0, block) failed");
        ptest_assert_msg(flags == 0, "Mvalidate: a private block has flags 0");
        ptest_assert_msg(Mvalidate(0, block, 8192L, 0) == 0, "Mvalidate without flags failed");
        ptest_assert_msg(Mvalidate(0, (void *)0x100, 16L, &flags) == EINVAL,
                         "Mvalidate(unmapped) was not refused");
        ptest_assert_msg(Mvalidate(77, block, 8192L, &flags) == ESRCH,
                         "Mvalidate(another process) was not refused");
        ptest_assert_msg(Mvalidate(0, block, 8192L, (long *)0x100) < 0,
                         "Mvalidate into an unmapped flags pointer was not refused");

        Mfree(block);
        /* given back: not the process's any more */
        ptest_assert_msg(Maccess(block, 8192L, 0) == EINVAL, "Maccess of a freed block was not refused");
    }

    ptest_pass();
}
