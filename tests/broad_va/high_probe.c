/*
 * high_probe.c - relocation-free high ET_EXEC and low guest RAM (#946)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <mint/basepage.h>
#include "include/mmapcall.h"

#define LOW 0x00010000L
#define PAGES ((0x01000000L - LOW) / MM_PAGE)

static int target = 946;
static int *volatile pointer = &target;
static const long guest_offsets[] = {
    0, 0x00200000L - LOW, 0x00800000L - LOW, PAGES * MM_PAGE - MM_PAGE
};

static int fragmented_heap(void)
{
    long blocks[64], base = 0, address;
    int i;

    /* Heap blocks and mappings alternate in address order. Allocation has
     * to merge both lists; after freeing a pair, first fit must reuse it. */
    for (i = 0; i < 64; i++) {
        blocks[i] = (long)Malloc(MM_PAGE);
        if (!i)
            base = blocks[i];
        if (base <= 0 || blocks[i] != base + 2 * i * MM_PAGE ||
            mm_map(blocks[i] + MM_PAGE, 1, PROT_NONE, 1) != blocks[i] + MM_PAGE)
            return 21;
    }
    if (Mfree((void *)blocks[0]) || mm_unmap(base + MM_PAGE, 1))
        return 22;
    address = (long)Malloc(2 * MM_PAGE);
    if (address != base || Mfree((void *)address))
        return 23;
    for (i = 63; i > 0; i--)
        if (mm_unmap(blocks[i] + MM_PAGE, 1) || Mfree((void *)blocks[i]))
            return 24;
    return 0;
}

static int automatic_mapping_limit(void)
{
    long old, first, pages, address;

    old = Psetlimit(0x100, 0x00040000L);
    if (old < 0)
        return 25;
    /* The largest allocatable mapping gap follows the image/startup area.
     * Shrinking the stack creates more heap space above the mapping ceiling;
     * an automatic mapping must neither cross nor use that extra space. */
    first = (long)_base->p_bbase + _base->p_blen + 0x20000L;
    pages = (0x7efff000L - first) / MM_PAGE;
    if (mm_map(0L, pages + 1, PROT_NONE, 0) != -39L)
        return 26;
    address = mm_map(0L, pages, PROT_NONE, 0);
    if (address != first || mm_prot(address + (pages - 1) * MM_PAGE, 1, PROT_READ) ||
        *(volatile long *)(address + (pages - 1) * MM_PAGE) != 0 ||
        mm_unmap(address, pages) || Psetlimit(0x100, old) != 0x00040000L)
        return 27;
    return 0;
}

int main(void)
{
    volatile unsigned char *ram;
    long address, high, heap, largest, stack_limit, i;
    int rc;

    if ((unsigned long)&main < 0x41000000UL ||
        (unsigned long)&main >= 0x41100000UL || *pointer != 946)
        return 1;
    if ((unsigned long)_base < 0x01000000UL ||
        (unsigned long)_base->p_env < 0x01000000UL)
        return 2;
    address = mm_map(LOW, PAGES, PROT_READ | PROT_WRITE, 1);
    if (address != LOW)
        return 3;
    ram = (volatile unsigned char *)address;
    /* Touch the ends and the former metadata and framebuffer aliases.
     * Even a first read can allocate a private physical page: the Amiga
     * test machine has less RAM than this virtual reservation. */
    for (i = 0; i < (long)(sizeof(guest_offsets) / sizeof(guest_offsets[0])); i++) {
        if (ram[guest_offsets[i]] != 0)
            return 4;
        ram[guest_offsets[i]] = (unsigned char)(i + 1);
    }
    for (i = 0; i < (long)(sizeof(guest_offsets) / sizeof(guest_offsets[0])); i++)
        if (ram[guest_offsets[i]] != (unsigned char)(i + 1))
            return 5;
    if (mm_map(LOW, 1, PROT_READ, 1) != -36L || ram[0] != 1)
        return 6;
    if (mm_map((long)&main & -MM_PAGE, 1, PROT_READ, 1) != -36L ||
        mm_map(0x41008000L, 1, PROT_NONE, 1) != -36L ||
        mm_map((long)_base & -MM_PAGE, 1, PROT_READ, 1) != -36L ||
        mm_map(0x08000000L, 1, PROT_NONE, 1) != -36L ||
        mm_map(0L, 1, PROT_READ, 1) != -40L ||
        mm_map(0x7ffff000L, 1, PROT_READ, 1) != -40L)
        return 7;
    high = mm_map(0x42000000L, 2, PROT_READ | PROT_WRITE, 1);
    if (high != 0x42000000L)
        return 8;
    *(volatile long *)high = 946;
    /* The largest heap gap lies above the image and this high mapping.
     * Reserving it is cheap: only the two explicitly touched pages back. */
    largest = (long)Malloc(-1L);
    heap = (long)Malloc(largest);
    if (largest <= 0 || heap < high + 2 * MM_PAGE)
        return 14;
    *(volatile long *)heap = 947;
    *(volatile long *)(heap + largest - MM_PAGE) = 948;
    if (*pointer != 946)
        return 15;
    if (*(volatile long *)high != 946)
        return 16;
    if (mm_map(heap, 1, PROT_NONE, 1) != -36L)
        return 17;
    if (Mfree((void *)heap))
        return 18;
    if (mm_prot(high, 1, PROT_READ) || Maccess((void *)high, 4, 0) == 0 ||
        *(volatile long *)high != 946 || mm_unmap(high, 2))
        return 9;
    /* Malloc and Mmap share a ledger, even for untouched/lazy blocks. */
    heap = (long)Malloc(MM_PAGE);
    if (heap <= 0 || Mfree((void *)heap))
        return 10;
    address = mm_map(heap, 1, PROT_NONE, 1);
    heap = (long)Malloc(MM_PAGE);
    if (address <= 0 || heap <= address ||
        mm_map(heap, 1, PROT_READ, 1) != -36L)
        return 10;
    if (Mfree((void *)heap) || mm_unmap(address, 1))
        return 11;
    /* Growing the stack may not cover a high mapping or the high image. */
    high = mm_map(0x7e000000L, 1, PROT_NONE, 1);
    stack_limit = Psetlimit(0x100, -1L);
    if (high != 0x7e000000L ||
        Psetlimit(0x100, 0x04000000L) != -39L ||
        Psetlimit(0x100, -1L) != stack_limit ||
        mm_unmap(high, 1))
        return 12;
    if (Psetlimit(0x100, 0x40000000L) != -39L)
        return 13;
    rc = fragmented_heap();
    if (rc)
        return rc;
    rc = automatic_mapping_limit();
    if (rc)
        return rc;
    /* Leave the low guest RAM mapped: process teardown must reclaim it. */
    return 0;
}
