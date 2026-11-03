/*
 * ttram.c - regression test for the TT-RAM of an Atari with address spaces
 *
 * Where the machine reports more RAM than the ST-RAM can be (over 16 MiB,
 * S_MEMINFO), a block is allocated, written and read back.  The page allocator
 * hands out the TT-RAM first, so its pages are in the TT-RAM, which the kernel
 * reaches through its window (the static tree of the 68030, the tables built
 * at boot on the 68040).  Elsewhere the suite passes without testing anything.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>

#define EINVFN      (-32L)
#define BLOCK       (2UL * 1024 * 1024)

void test_ttram(void);

void test_ttram(void)
{
    ptest_begin("ttram");

#if defined(__m68k__)
    {
        struct {
            unsigned long page_size, phys_pages, free_pages, processes;
            unsigned long shared_pages, shared_refs, shared_cross;
        } mi;

        if (Ssystem(-1, -3, 0L) == EINVFN || Ssystem(-3, (long)&mi, (long)sizeof(mi)) != (long)sizeof(mi)
            || mi.phys_pages * mi.page_size <= 16UL * 1024 * 1024
            || (mi.free_pages != 0xffffffffUL && mi.free_pages * mi.page_size < BLOCK + 1024UL * 1024))
        {
            ptest_pass();               /* no TT-RAM, or no page accounting */
            return;
        }
        {
            volatile unsigned long *block = (volatile unsigned long *)Malloc((long)BLOCK);
            unsigned long i, n = BLOCK / sizeof(unsigned long);

            ptest_assert_msg(block != 0, "a block is refused where there is TT-RAM");
            if (block)
            {
                for (i = 0; i < n; i += 1024)
                    block[i] = i ^ 0x5a5a5a5aUL;
                for (i = 0; i < n; i += 1024)
                    if (block[i] != (i ^ 0x5a5a5a5aUL))
                        break;
                ptest_assert_msg(i == n, "a page of a large block does not hold what was written");
                Mfree((void *)block);
            }
        }
    }
#endif
    ptest_pass();
}
