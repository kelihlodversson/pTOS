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

int main(void)
{
    volatile unsigned char *ram;
    long address, high, heap, stack_limit, i;

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
    /* Include the former metadata and framebuffer aliases, not just the
     * beginning and end of the reservation. Every page must be private. */
    for (i = 0; i < PAGES * MM_PAGE; i += MM_PAGE) {
        if (ram[i] != 0)
            return 4;
        ram[i] = (unsigned char)(i / MM_PAGE + 1);
    }
    for (i = 0; i < PAGES * MM_PAGE; i += MM_PAGE)
        if (ram[i] != (unsigned char)(i / MM_PAGE + 1))
            return 5;
    if (mm_map(LOW, 1, PROT_READ, 1) != -36L || ram[0] != 1)
        return 6;
    if (mm_map((long)&main & -MM_PAGE, 1, PROT_READ, 1) != -36L ||
        mm_map((long)_base & -MM_PAGE, 1, PROT_READ, 1) != -36L ||
        mm_map(0x08000000L, 1, PROT_NONE, 1) != -36L ||
        mm_map(0L, 1, PROT_READ, 1) != -40L ||
        mm_map(0x7ffff000L, 1, PROT_READ, 1) != -40L)
        return 7;
    high = mm_map(0x42000000L, 2, PROT_READ | PROT_WRITE, 1);
    if (high != 0x42000000L)
        return 8;
    *(volatile long *)high = 946;
    if (mm_prot(high, 1, PROT_READ) || Maccess((void *)high, 4, 0) == 0 ||
        *(volatile long *)high != 946 || mm_unmap(high, 2))
        return 9;
    /* Malloc and Mmap share a ledger, even for untouched/lazy blocks. */
    address = mm_map(0x10000000L, 1, PROT_NONE, 1);
    heap = (long)Malloc(MM_PAGE);
    if (address != 0x10000000L || heap <= address ||
        mm_map(heap, 1, PROT_READ, 1) != -36L)
        return 10;
    if (Mfree((void *)heap) || mm_unmap(address, 1))
        return 11;
    /* Growing the stack may not cover a high mapping or the high image. */
    high = mm_map(0x7e000000L, 1, PROT_NONE, 1);
    stack_limit = sys6(0x11f, 0x100, -1L, 0L, 0L, 0L, 0L);
    if (high != 0x7e000000L ||
        sys6(0x11f, 0x100, 0x04000000L, 0L, 0L, 0L, 0L) != -39L ||
        sys6(0x11f, 0x100, -1L, 0L, 0L, 0L, 0L) != stack_limit ||
        mm_unmap(high, 1))
        return 12;
    if (sys6(0x11f, 0x100, 0x40000000L, 0L, 0L, 0L, 0L) != -39L)
        return 13;
    /* Leave the low guest RAM mapped: process teardown must reclaim it. */
    return 0;
}
