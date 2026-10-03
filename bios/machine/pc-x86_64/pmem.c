/*
 * pmem.c - x86-64 early physical-memory allocator
 *
 * A bump allocator over the free regions the EFI memory map reports,
 * standing in for a real page allocator until #344's later work gets the
 * shared core's own memory management running on this arch. See pmem.h.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "portab.h"
#include "efi.h"
#include "pmem.h"
#include "earlycon.h"
#include "io.h"
#include "pgtable.h"

/*
 * EFI_MEMORY_DESCRIPTOR's minimum possible stride (UEFI spec 7.2): the
 * struct itself is 4 (Type) + 4 (padding, UQUAD alignment) + 8 + 8 + 8 +
 * 8 = 40 bytes, and GetMemoryMap() is free to report a larger
 * DescriptorSize (room to grow the struct in a future spec) but never a
 * smaller one -- so 40 is the true worst case for how many descriptors
 * X86_64_EFI_MAP_BYTES (pmem.h) could possibly hold.
 */
#define MIN_EFI_DESCRIPTOR_SIZE 40

/*
 * The whole saved map could -- at that smallest legal stride -- be
 * entirely free-eligible descriptors, and each one can turn into up to
 * *four* surviving regions, not three: add_free_region_excluding3()
 * clips a descriptor against reserved1 first (which can split it into a
 * low and a high remainder -- 2 pieces), then clips each of those
 * remainders against reserved2 independently, then clips each survivor
 * of that against reserved3 independently in turn. Each additional
 * independent, single contiguous reserved range can increase the total
 * piece count by at most 1, regardless of how many disjoint pieces
 * already exist: splitting a piece into two strictly requires the
 * reserved range's *own* start and end to both fall inside that one
 * piece, which by construction can be true for at most one of the
 * existing (disjoint, ordered) pieces at a time -- a reserved range
 * whose start falls inside one piece and whose end falls inside a later
 * one only erodes an edge off each (worked through in detail in the PR
 * #350 review discussion this sizing originally responds to; the same
 * argument applies unchanged to a third independent range). So the true
 * worst case per descriptor is 2 (from reserved1) + 1 (reserved2) + 1
 * (reserved3) = 4, and MAX_REGIONS has to provide 4 slots per possible
 * descriptor, not a flat "+8" (which only covered a few extra *total*,
 * not per descriptor -- a map with more than a handful of free
 * descriptors actually split by all three reserved ranges would exhaust
 * the list and panic at boot). The "+ 8" left over here is now pure
 * slack, since a little more costs nothing. Derived from
 * X86_64_EFI_MAP_BYTES, rather than guessed independently, so this can
 * never overflow against whatever that buffer can actually hold (#348
 * review) -- in the same spirit as the MAX_PDPTS/MAX_PDS pools in
 * pgtable.c, but provably sized rather than merely generous.
 */
#define MAX_REGIONS (4 * (X86_64_EFI_MAP_BYTES / MIN_EFI_DESCRIPTOR_SIZE) + 8)

typedef struct {
    UQUAD base;
    UQUAD length; /* page-aligned; shrinks as pages are allocated from it */
} free_region_t;

static free_region_t regions[MAX_REGIONS];
static int region_count;
static UQUAD total_free;
static UQUAD highest_addr;

static NORETURN void hang(void)
{
    for (;;) {
        x86_64_cli();
        x86_64_halt();
    }
}

static NORETURN void panic(const char *msg)
{
    earlycon_puts("panic: ");
    earlycon_puts(msg);
    earlycon_puts("\n");
    hang();
}

/*
 * True for every EFI_MEMORY_TYPE that is, or once was, real installed RAM
 * -- as opposed to EfiReservedMemoryType, EfiUnusableMemory or either MMIO
 * type, none of which are backed by real memory at that physical address.
 * OVMF, among others, uses EfiReservedMemoryType for its PCI 64-bit MMIO
 * aperture: a real map has been observed reporting one such descriptor
 * with a base and size (~12 GiB, at ~1 TiB) that dwarf the VM's actual
 * RAM. x86_64_pmem_highest_addr() uses this list (not "every descriptor",
 * and not an exclude list, which a future reserved-but-real-RAM firmware
 * quirk could too easily be missing from) to size the physical-memory
 * direct map off real RAM only, never an MMIO aperture like that one.
 */
static int is_ram_type(ULONG type)
{
    switch (type) {
    case EFI_LOADER_CODE:
    case EFI_LOADER_DATA:
    case EFI_BOOT_SERVICES_CODE:
    case EFI_BOOT_SERVICES_DATA:
    case EFI_RUNTIME_SERVICES_CODE:
    case EFI_RUNTIME_SERVICES_DATA:
    case EFI_CONVENTIONAL_MEMORY:
    case EFI_ACPI_RECLAIM_MEMORY:
    case EFI_ACPI_MEMORY_NVS:
    case EFI_PAL_CODE:
    case EFI_PERSISTENT_MEMORY:
        return 1;
    default:
        return 0;
    }
}

/* Records [base, base+length) as free, narrowing to whole pages so a
 * region whose firmware-reported bounds are not page aligned never
 * claims a partial page as free memory. Drops it silently if narrowing
 * leaves nothing. */
static void add_free_region(UQUAD base, UQUAD length)
{
    UQUAD end = base + length;
    UQUAD aligned_base = (base + X86_64_PAGE_SIZE - 1) & ~(X86_64_PAGE_SIZE - 1);
    UQUAD aligned_end = end & ~(X86_64_PAGE_SIZE - 1);

    if (aligned_end <= aligned_base)
        return;

    if (region_count >= MAX_REGIONS)
        panic("pmem: free region list exhausted");

    regions[region_count].base = aligned_base;
    regions[region_count].length = aligned_end - aligned_base;
    region_count++;
    total_free += aligned_end - aligned_base;
}

/* Adds [base, base+length) as free, clipping out [reserved_base,
 * reserved_end) first -- splitting into the surviving low and/or high
 * remainder if the reserved range falls strictly inside it. */
static void add_free_region_excluding(UQUAD base, UQUAD length,
                                       UQUAD reserved_base, UQUAD reserved_end)
{
    UQUAD end = base + length;

    if (end <= reserved_base || base >= reserved_end) {
        add_free_region(base, length);
        return;
    }
    if (base < reserved_base)
        add_free_region(base, reserved_base - base);
    if (end > reserved_end)
        add_free_region(reserved_end, end - reserved_end);
}

/* Same as add_free_region_excluding(), but against two independent
 * reserved ranges -- this image's own load span and, separately, the low
 * system-vector page (#349) -- rather than the smallest single range that
 * happens to cover both. The two are typically far apart (EFI usually
 * loads this image well above address 0), and reserving everything in
 * between as one contiguous range would falsely exclude a large amount of
 * genuinely free memory; this instead clips each disjoint reserved range
 * out in turn, keeping whatever survives between them. */
static void add_free_region_excluding2(UQUAD base, UQUAD length,
                                        UQUAD reserved1_base, UQUAD reserved1_end,
                                        UQUAD reserved2_base, UQUAD reserved2_end)
{
    UQUAD end = base + length;

    if (end <= reserved1_base || base >= reserved1_end) {
        add_free_region_excluding(base, length, reserved2_base, reserved2_end);
        return;
    }
    if (base < reserved1_base)
        add_free_region_excluding(base, reserved1_base - base, reserved2_base, reserved2_end);
    if (end > reserved1_end)
        add_free_region_excluding(reserved1_end, end - reserved1_end, reserved2_base, reserved2_end);
}

/* Same as add_free_region_excluding(), but against three independent
 * reserved ranges -- this image's own load span, the low system-vector
 * page (#349), and the EFI GOP framebuffer's aperture (#332) -- rather
 * than the smallest single range that happens to cover all three. They
 * are typically far apart (EFI usually loads this image well above
 * address 0, and the framebuffer can be anywhere firmware put it), and
 * reserving everything in between as one contiguous range would falsely
 * exclude a large amount of genuinely free memory; this instead clips
 * each disjoint reserved range out in turn, keeping whatever survives
 * between them. */
static void add_free_region_excluding3(UQUAD base, UQUAD length,
                                        UQUAD reserved1_base, UQUAD reserved1_end,
                                        UQUAD reserved2_base, UQUAD reserved2_end,
                                        UQUAD reserved3_base, UQUAD reserved3_end)
{
    UQUAD end = base + length;

    if (end <= reserved1_base || base >= reserved1_end) {
        add_free_region_excluding2(base, length, reserved2_base, reserved2_end,
                                    reserved3_base, reserved3_end);
        return;
    }
    if (base < reserved1_base)
        add_free_region_excluding2(base, reserved1_base - base, reserved2_base, reserved2_end,
                                    reserved3_base, reserved3_end);
    if (end > reserved1_end)
        add_free_region_excluding2(reserved1_end, end - reserved1_end, reserved2_base, reserved2_end,
                                    reserved3_base, reserved3_end);
}

void x86_64_pmem_init(const void *efi_map, UQUAD map_size, UQUAD descriptor_size,
                       UQUAD reserved1_base, UQUAD reserved1_end,
                       UQUAD reserved2_base, UQUAD reserved2_end,
                       UQUAD reserved3_base, UQUAD reserved3_end)
{
    const UBYTE *cursor = (const UBYTE *)efi_map;
    const UBYTE *map_end = cursor + map_size;

    region_count = 0;
    total_free = 0;
    highest_addr = 0;

    /* Strided by descriptor_size, not sizeof(EFI_MEMORY_DESCRIPTOR): see
     * the struct's own comment in efi.h. */
    for (; cursor < map_end; cursor += descriptor_size) {
        const EFI_MEMORY_DESCRIPTOR *desc = (const EFI_MEMORY_DESCRIPTOR *)(uintptr_t)cursor;
        UQUAD base = desc->PhysicalStart;
        UQUAD length = desc->NumberOfPages * X86_64_PAGE_SIZE;
        UQUAD end = base + length;

        if (is_ram_type(desc->Type) && end > highest_addr)
            highest_addr = end;

        switch (desc->Type) {
        case EFI_CONVENTIONAL_MEMORY:
        case EFI_BOOT_SERVICES_CODE:
        case EFI_BOOT_SERVICES_DATA:
            add_free_region_excluding3(base, length, reserved1_base, reserved1_end,
                                        reserved2_base, reserved2_end,
                                        reserved3_base, reserved3_end);
            break;
        default:
            /* Reserved, ACPI, MMIO, runtime-services, this image's own
             * Loader{Code,Data}, ... -- not free memory. */
            break;
        }
    }
}

/*
 * Test-only failure injection (see pmem.h): when fail_countdown is
 * positive, the Nth try_alloc from now fails as if memory had run out
 * (1 = the very next call); zero or negative leaves it disarmed.
 */
static LONG fail_countdown;

/* Rejected x86_64_pmem_free_pages() calls (double free, unmanaged memory). */
static UQUAD bad_frees;

/*
 * Runtime page tracking (x86_64_pmem_track()).  Two bitmaps over every
 * page below highest_addr replace the boot-time region list once the
 * physical direct map exists:
 *
 *   managed  the page was free RAM when tracking began (or holds these
 *            bitmaps): memory this allocator owns.  Everything else -- the
 *            kernel image, ACPI and firmware ranges, holes, the
 *            framebuffer -- is never managed, so it can neither be handed
 *            out nor "freed" into the pool by a stray call.
 *   alloc    the managed page is currently allocated.
 *
 * Unlike the region list, whose slots were sized for the firmware's
 * memory map, this can represent any pattern of frees however fragmented,
 * so a valid free can never fail for lack of metadata.  Its size is fixed
 * by the amount of RAM: two bits per 4 KiB page.
 */
static UQUAD *bm_managed;
static UQUAD *bm_alloc;
static UQUAD npages;

#define BM_TEST(bm, i)  (((bm)[(i) / 64] >> ((i) % 64)) & 1)
#define BM_SET(bm, i)   ((bm)[(i) / 64] |= 1ULL << ((i) % 64))
#define BM_CLEAR(bm, i) ((bm)[(i) / 64] &= ~(1ULL << ((i) % 64)))

/* Boot-time allocation from the region list (before tracking). */
static UQUAD region_alloc(UQUAD count, UQUAD limit)
{
    UQUAD want = count * X86_64_PAGE_SIZE;
    int i;

    for (i = 0; i < region_count; i++) {
        if (regions[i].length >= want &&
            (!limit || regions[i].base + want <= limit)) {
            UQUAD addr = regions[i].base;

            regions[i].base += want;
            regions[i].length -= want;
            total_free -= want;
            return addr;
        }
    }
    return X86_64_PMEM_NONE;
}

void x86_64_pmem_track(void)
{
    UQUAD words = ((highest_addr / X86_64_PAGE_SIZE) + 63) / 64;
    UQUAD bm_pages = (2 * words * sizeof(UQUAD) + X86_64_PAGE_SIZE - 1) / X86_64_PAGE_SIZE;
    UQUAD phys = region_alloc(bm_pages, 0);
    UQUAD i, p;

    if (phys == X86_64_PMEM_NONE)
        panic("pmem: no memory for page tracking");

    npages = highest_addr / X86_64_PAGE_SIZE;
    bm_managed = (UQUAD *)(uintptr_t)(X86_64_PHYS_MAP_BASE + phys);
    bm_alloc = bm_managed + words;
    for (i = 0; i < 2 * words; i++)
        bm_managed[i] = 0;

    /* the bitmaps' own pages: managed, and allocated for good */
    for (p = phys / X86_64_PAGE_SIZE; p < phys / X86_64_PAGE_SIZE + bm_pages; p++) {
        BM_SET(bm_managed, p);
        BM_SET(bm_alloc, p);
    }
    /* everything still on the region list: managed and free */
    for (i = 0; i < (UQUAD)region_count; i++)
        for (p = regions[i].base / X86_64_PAGE_SIZE;
             p < (regions[i].base + regions[i].length) / X86_64_PAGE_SIZE; p++)
            BM_SET(bm_managed, p);
    region_count = 0;
}

static BOOL page_free(UQUAD i)
{
    return BM_TEST(bm_managed, i) && !BM_TEST(bm_alloc, i);
}

/* First-fit (by address) run of `count` free pages ending at or below
 * page index `end`. */
static BOOL find_run(UQUAD count, UQUAD end, UQUAD *first)
{
    UQUAD i = 0, run = 0, start = 0;

    if (end > npages)
        end = npages;
    while (i < end) {
        if (!(i & 63) && !(bm_managed[i / 64] & ~bm_alloc[i / 64])) {
            run = 0;                    /* nothing free in this whole word */
            i += 64;
            continue;
        }
        if (page_free(i)) {
            if (!run)
                start = i;
            if (++run == count) {
                *first = start;
                return TRUE;
            }
        } else {
            run = 0;
        }
        i++;
    }
    return FALSE;
}

UQUAD x86_64_pmem_try_alloc_pages(UQUAD count, UQUAD limit)
{
    UQUAD want = count * X86_64_PAGE_SIZE;
    UQUAD first, i;

    if (!count || want / X86_64_PAGE_SIZE != count)
        return X86_64_PMEM_NONE;

    if (fail_countdown > 0 && --fail_countdown == 0)
        return X86_64_PMEM_NONE;

    if (!bm_managed)
        return region_alloc(count, limit);

    if (!find_run(count, limit ? limit / X86_64_PAGE_SIZE : npages, &first))
        return X86_64_PMEM_NONE;
    for (i = first; i < first + count; i++)
        BM_SET(bm_alloc, i);
    total_free -= want;
    return first * X86_64_PAGE_SIZE;
}

BOOL x86_64_pmem_free_pages(UQUAD base, UQUAD count)
{
    UQUAD first, i;

    /* Every page must be managed memory that is currently allocated: that
     * refuses a double free, a free of the kernel image or any reserved
     * range, and a range straddling either, before anything changes. */
    if (!bm_managed || !count || (base & (X86_64_PAGE_SIZE - 1)) ||
        count > npages || base / X86_64_PAGE_SIZE > npages - count) {
        bad_frees++;
        return FALSE;
    }
    first = base / X86_64_PAGE_SIZE;
    for (i = first; i < first + count; i++)
        if (!BM_TEST(bm_managed, i) || !BM_TEST(bm_alloc, i)) {
            bad_frees++;
            return FALSE;
        }
    for (i = first; i < first + count; i++)
        BM_CLEAR(bm_alloc, i);
    total_free += count * X86_64_PAGE_SIZE;
    return TRUE;
}

UQUAD x86_64_pmem_alloc_pages(UQUAD count)
{
    UQUAD addr = x86_64_pmem_try_alloc_pages(count, 0);

    if (addr == X86_64_PMEM_NONE)
        panic("pmem: out of physical memory");
    return addr;
}

UQUAD x86_64_pmem_alloc_pages_below(UQUAD count, UQUAD limit)
{
    UQUAD addr = x86_64_pmem_try_alloc_pages(count, limit);

    if (addr == X86_64_PMEM_NONE)
        panic("pmem: out of physical memory below requested limit");
    return addr;
}

void x86_64_pmem_test_fail_after(LONG n)
{
    fail_countdown = n;
}

UQUAD x86_64_pmem_bad_frees(void)
{
    return bad_frees;
}

UQUAD x86_64_pmem_free_bytes(void)
{
    return total_free;
}

UQUAD x86_64_pmem_highest_addr(void)
{
    return highest_addr;
}
