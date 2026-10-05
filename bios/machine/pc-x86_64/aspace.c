/*
 * aspace.c - tracked process address spaces (x86-64)
 *
 * See include/procmem.h for the contract.  An address space owns a PML4
 * page and every page-table page built under it, recorded in a kheap
 * vector as the page-table code allocates them.  Destruction walks that
 * vector, so a mapping that failed half way, a process that exited and a
 * setup that was abandoned all release exactly the same set of pages.
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_PC_X86_64
#error This file must only be compiled for the pc-x86_64 machine
#endif

#include "portab.h"
#include "string.h"
#include "kheap.h"
#include "procmem.h"
#include "pgtable.h"
#include "pmem.h"
#include "pc_x86_64_memory.h"

#define ASPACE_MAGIC    0x41535041434531ULL     /* "ASPACE1" */
#define FIRST_CAPACITY  16

struct x86_64_aspace {
    UQUAD magic;
    UQUAD pml4_phys;
    UQUAD *pages;               /* kheap vector: PML4 first, then tables */
    ULONG count;
    ULONG capacity;
    struct pin {                /* procmem ranges mapped (and so pinned) */
        UQUAD va, bytes;
    } *pins;
    ULONG npins, pin_capacity;
    UQUAD *owned;               /* private backing pages: ours alone, freed with us */
    ULONG nowned, owned_capacity;
};

#define PTE_PRESENT  0x1ULL
#define PTE_WRITABLE 0x2ULL
#define PTE_USER     0x4ULL
#define PTE_PS       0x80ULL
#define PTE_NX       0x8000000000000000ULL
#define PTE_ADDR     0xFFFFFFFFFF000ULL

/* Grows the vector and records a page; false (and nothing recorded) if the
 * vector could not grow. */
static BOOL record_page(struct x86_64_aspace *as, UQUAD phys)
{
    if (as->count == as->capacity) {
        ULONG cap = as->capacity ? as->capacity * 2 : FIRST_CAPACITY;
        UQUAD *grown = kalloc(cap * sizeof(UQUAD));

        if (!grown)
            return FALSE;
        if (as->pages) {
            memcpy(grown, as->pages, as->count * sizeof(UQUAD));
            kfree(as->pages);
        }
        as->pages = grown;
        as->capacity = cap;
    }
    as->pages[as->count++] = phys;
    return TRUE;
}

/* x86_64_map_user_page()'s alloc_page callback: a table page, recorded
 * before it is handed over so it cannot be forgotten. */
static UQUAD alloc_table_page(void *ctx)
{
    UQUAD phys = x86_64_pmem_try_alloc_pages(1, 0);

    if (phys == X86_64_PMEM_NONE)
        return X86_64_PGTABLE_NO_PAGE;
    if (!record_page(ctx, phys)) {
        x86_64_pmem_free_pages(phys, 1);
        return X86_64_PGTABLE_NO_PAGE;
    }
    return phys;
}

/*
 * Ring 0 services a process's system calls under that process's own CR3,
 * so the kernel's low-half data has to be reachable there too: the
 * system-vector area (the OS variables at 0x400.., the trap vectors), the
 * low kernel-data pool and the framebuffer.  Each is a 2 MiB mapping copied
 * from the kernel's own tables, supervisor-only -- ring 3 gets a fault from
 * all of it, the null guard included -- and placed so as not to overlap
 * anything a process maps for itself (include/procmem.h's layout).
 */
static BOOL map_kernel_low(struct x86_64_aspace *as)
{
    UQUAD start, end, va, phys;

    if (x86_64_kernel_low_2m_phys(0, &phys) &&
        x86_64_map_kernel_2m_into(as->pml4_phys, 0, phys, alloc_table_page, as) != 0)
        return FALSE;
    x86_64_low_kernel_range(&start, &end);
    for (va = start; va < end; va += X86_64_PAGE_2M_SIZE)
        if (x86_64_kernel_low_2m_phys(va, &phys) &&
            x86_64_map_kernel_2m_into(as->pml4_phys, va, phys, alloc_table_page, as) != 0)
            return FALSE;

    return TRUE;
}

X86_64_ASPACE *x86_64_aspace_create(void)
{
    struct x86_64_aspace *as = kalloc(sizeof(*as));
    UQUAD pml4;

    if (!as)
        return NULL;
    pml4 = x86_64_pmem_try_alloc_pages(1, 0);
    if (pml4 == X86_64_PMEM_NONE) {
        kfree(as);
        return NULL;
    }
    as->magic = ASPACE_MAGIC;
    as->pml4_phys = pml4;
    if (!record_page(as, pml4)) {
        x86_64_pmem_free_pages(pml4, 1);
        kfree(as);
        return NULL;
    }
    x86_64_new_address_space(pml4);
    if (!map_kernel_low(as)) {
        x86_64_aspace_destroy(as);
        return NULL;
    }
    return as;
}

void x86_64_aspace_destroy(X86_64_ASPACE *as)
{
    ULONG i;

    if (!as || as->magic != ASPACE_MAGIC)
        return;                 /* already destroyed (or never created) */

    /* The tables may not be freed while the CPU still walks them: the
     * Pterm()/error paths run in the dying process's own address space. */
    if (x86_64_read_cr3() == as->pml4_phys)
        x86_64_write_cr3(x86_64_kernel_pml4_phys());

    as->magic = 0;
    for (i = 0; i < as->npins; i++)
        x86_64_procmem_pin(as->pins[i].va, as->pins[i].bytes, -1);
    kfree(as->pins);
    for (i = 0; i < as->nowned; i++)
        x86_64_pmem_free_pages(as->owned[i], 1);
    kfree(as->owned);
    for (i = 0; i < as->count; i++)
        x86_64_pmem_free_pages(as->pages[i], 1);
    kfree(as->pages);
    kfree(as);
}

UQUAD x86_64_aspace_pml4(const X86_64_ASPACE *as)
{
    return as->pml4_phys;
}

ULONG x86_64_aspace_table_pages(const X86_64_ASPACE *as)
{
    return as->count;
}

BOOL x86_64_aspace_map_page(X86_64_ASPACE *as, UQUAD va, UQUAD phys, UWORD prot)
{
    if ((va | phys) & (X86_64_PAGE_SIZE - 1))
        return FALSE;
    /* The 32-bit ABI constraint, checked where the mapping is made: the
     * whole page must sit below 4 GiB.  phys is deliberately unchecked. */
    if (va >= X86_64_USER_VA_LIMIT || X86_64_USER_VA_LIMIT - va < X86_64_PAGE_SIZE)
        return FALSE;
    return x86_64_map_user_page(as->pml4_phys, va, phys,
                                !!(prot & ASPACE_PROT_WRITE),
                                !!(prot & ASPACE_PROT_EXEC),
                                !!(prot & ASPACE_PROT_USER),
                                alloc_table_page, as) == 0;
}

BOOL x86_64_aspace_map_procmem(X86_64_ASPACE *as, UQUAD va, UQUAD bytes, UWORD prot)
{
    UQUAD page, end;

    if (!bytes || va + bytes < va || !x86_64_procmem_range_live(va, bytes))
        return FALSE;
    /* Pin before mapping, and remember it before pinning, so every pin has
     * a record that destroy undoes; a half-finished mapping is covered too. */
    if (as->npins == as->pin_capacity) {
        ULONG cap = as->pin_capacity ? as->pin_capacity * 2 : 4;
        struct pin *grown = kalloc(cap * sizeof(*grown));

        if (!grown)
            return FALSE;
        if (as->pins) {
            memcpy(grown, as->pins, as->npins * sizeof(*grown));
            kfree(as->pins);
        }
        as->pins = grown;
        as->pin_capacity = cap;
    }
    as->pins[as->npins].va = va;
    as->pins[as->npins].bytes = bytes;
    as->npins++;
    x86_64_procmem_pin(va, bytes, +1);

    page = va & ~(X86_64_PAGE_SIZE - 1);
    end = (va + bytes + X86_64_PAGE_SIZE - 1) & ~(X86_64_PAGE_SIZE - 1);
    for (; page < end; page += X86_64_PAGE_SIZE)
        if (!x86_64_aspace_map_page(as, page, x86_64_procmem_phys_of(page), prot))
            return FALSE;
    return TRUE;
}

/* ---- walking, validation and private memory ------------------------- */

BOOL x86_64_aspace_translate(const X86_64_ASPACE *as, UQUAD va, UQUAD *phys, UWORD *prot)
{
    const UQUAD *table = (const UQUAD *)(uintptr_t)(X86_64_PHYS_MAP_BASE + as->pml4_phys);
    UQUAD index[4];
    UQUAD entry = 0, size = 0, base = 0;
    BOOL user = TRUE, write = TRUE, nx = FALSE;
    int level;

    /* canonical addresses only: bits 63..47 all equal */
    if (va >= 0x800000000000ULL && va < 0xFFFF800000000000ULL)
        return FALSE;
    index[0] = (va >> 39) & 0x1FF;
    index[1] = (va >> 30) & 0x1FF;
    index[2] = (va >> 21) & 0x1FF;
    index[3] = (va >> 12) & 0x1FF;
    for (level = 0; level < 4; level++) {
        entry = table[index[level]];
        if (!(entry & PTE_PRESENT))
            return FALSE;
        user = user && (entry & PTE_USER);
        write = write && (entry & PTE_WRITABLE);
        nx = nx || (entry & PTE_NX);
        if (level == 1 && (entry & PTE_PS)) {           /* 1 GiB page */
            size = 0x40000000ULL;
            break;
        }
        if (level == 2 && (entry & PTE_PS)) {           /* 2 MiB page */
            size = 0x200000ULL;
            break;
        }
        if (level == 3) {
            size = X86_64_PAGE_SIZE;
            break;
        }
        table = (const UQUAD *)(uintptr_t)(X86_64_PHYS_MAP_BASE + (entry & PTE_ADDR));
    }
    base = entry & PTE_ADDR & ~(size - 1);
    if (phys)
        *phys = base + (va & (size - 1));
    if (prot)
        *prot = (write ? ASPACE_PROT_WRITE : 0) | (nx ? 0 : ASPACE_PROT_EXEC) |
                (user ? ASPACE_PROT_USER : 0);
    return TRUE;
}

BOOL x86_64_aspace_user_range_ok(const X86_64_ASPACE *as, UQUAD va, UQUAD bytes, BOOL write)
{
    UQUAD page, last;

    if (!bytes || va >= X86_64_USER_VA_LIMIT || bytes > X86_64_USER_VA_LIMIT - va)
        return FALSE;
    last = (va + bytes - 1) & ~(X86_64_PAGE_SIZE - 1);
    for (page = va & ~(X86_64_PAGE_SIZE - 1); ; page += X86_64_PAGE_SIZE) {
        UWORD prot;

        if (!x86_64_aspace_translate(as, page, NULL, &prot) ||
            !(prot & ASPACE_PROT_USER) || (write && !(prot & ASPACE_PROT_WRITE)))
            return FALSE;
        if (page == last)
            break;
    }
    return TRUE;
}

/* Copies page by page through the direct map; the range was validated. */
static void copy_user(const X86_64_ASPACE *as, UBYTE *kernel, UQUAD va, ULONG bytes, BOOL to_user)
{
    while (bytes) {
        UQUAD phys;
        ULONG chunk = (ULONG)(X86_64_PAGE_SIZE - (va & (X86_64_PAGE_SIZE - 1)));
        UBYTE *user;

        if (chunk > bytes)
            chunk = bytes;
        x86_64_aspace_translate(as, va, &phys, NULL);
        user = (UBYTE *)(uintptr_t)(X86_64_PHYS_MAP_BASE + phys);
        if (to_user)
            memcpy(user, kernel, chunk);
        else
            memcpy(kernel, user, chunk);
        kernel += chunk;
        va += chunk;
        bytes -= chunk;
    }
}

/* The kernel copies through the physical direct map, so every page must be RAM
 * the allocator owns: a page that is mapped for the process but is device
 * memory, or not RAM at all, is valid for ring 3 yet cannot be copied here --
 * refuse it rather than dereference an alias that may fault. */
static BOOL range_copyable(const X86_64_ASPACE *as, UQUAD va, UQUAD bytes, BOOL write)
{
    UQUAD page, last;

    if (!x86_64_aspace_user_range_ok(as, va, bytes, write))
        return FALSE;
    last = (va + bytes - 1) & ~(X86_64_PAGE_SIZE - 1);
    for (page = va & ~(X86_64_PAGE_SIZE - 1); ; page += X86_64_PAGE_SIZE) {
        UQUAD phys;

        if (!x86_64_aspace_translate(as, page, &phys, NULL) || !x86_64_pmem_is_allocated(phys))
            return FALSE;
        if (page == last)
            break;
    }
    return TRUE;
}

BOOL x86_64_aspace_copy_from_user(const X86_64_ASPACE *as, void *dst, UQUAD va, ULONG bytes)
{
    if (!range_copyable(as, va, bytes, FALSE))
        return FALSE;
    copy_user(as, dst, va, bytes, FALSE);
    return TRUE;
}

BOOL x86_64_aspace_copy_to_user(const X86_64_ASPACE *as, UQUAD va, const void *src, ULONG bytes)
{
    if (!range_copyable(as, va, bytes, TRUE))
        return FALSE;
    copy_user(as, (UBYTE *)src, va, bytes, TRUE);
    return TRUE;
}

ULONG x86_64_aspace_private_pages(const X86_64_ASPACE *as)
{
    return as->nowned;
}

/* True iff the table page at phys was allocated at or after index `first`
 * of the address space's table-page record. */
static BOOL table_is_newer(const struct x86_64_aspace *as, ULONG first, UQUAD phys)
{
    ULONG i;

    for (i = first; i < as->count; i++)
        if (as->pages[i] == phys)
            return TRUE;
    return FALSE;
}

/* Walks down toward va and, at the first entry that points at a table
 * allocated since `first`, clears that entry: everything below it is new too,
 * so the whole new subtree is cut off in one step. */
static void unlink_new_tables(const struct x86_64_aspace *as, ULONG first, UQUAD va)
{
    UQUAD *table = (UQUAD *)(uintptr_t)(X86_64_PHYS_MAP_BASE + as->pml4_phys);
    UQUAD index[3];
    int level;

    index[0] = (va >> 39) & 0x1FF;
    index[1] = (va >> 30) & 0x1FF;
    index[2] = (va >> 21) & 0x1FF;
    for (level = 0; level < 3; level++) {
        UQUAD entry = table[index[level]];
        UQUAD child = entry & PTE_ADDR;

        if (!(entry & PTE_PRESENT) || (entry & PTE_PS))
            return;
        if (table_is_newer(as, first, child)) {
            table[index[level]] = 0;
            return;
        }
        table = (UQUAD *)(uintptr_t)(X86_64_PHYS_MAP_BASE + child);
    }
}

BOOL x86_64_aspace_map_private(X86_64_ASPACE *as, UQUAD va, UQUAD bytes, UWORD prot)
{
    UQUAD pages, i, mapped = 0, backing;
    ULONG first_owned = as->nowned;
    ULONG first_tables = as->count;

    if ((va & (X86_64_PAGE_SIZE - 1)) || !bytes || va >= X86_64_USER_VA_LIMIT ||
        bytes > X86_64_USER_VA_LIMIT - va)
        return FALSE;
    pages = (bytes + X86_64_PAGE_SIZE - 1) / X86_64_PAGE_SIZE;
    if (va + pages * X86_64_PAGE_SIZE > X86_64_USER_VA_LIMIT)
        return FALSE;

    /* all-or-nothing: refuse an overlap before touching anything */
    for (i = 0; i < pages; i++)
        if (x86_64_aspace_translate(as, va + i * X86_64_PAGE_SIZE, NULL, NULL))
            return FALSE;

    /* room to record every backing page first, so recording cannot fail
     * once a page has been taken */
    if (as->nowned + pages > as->owned_capacity) {
        ULONG cap = as->owned_capacity ? as->owned_capacity : 8;
        UQUAD *grown;

        while (cap < as->nowned + pages)
            cap *= 2;
        grown = kalloc(cap * sizeof(UQUAD));
        if (!grown)
            return FALSE;
        if (as->owned) {
            memcpy(grown, as->owned, as->nowned * sizeof(UQUAD));
            kfree(as->owned);
        }
        as->owned = grown;
        as->owned_capacity = cap;
    }

    for (i = 0; i < pages; i++) {
        backing = x86_64_pmem_try_alloc_pages(1, 0);
        if (backing == X86_64_PMEM_NONE)
            break;
        memset((void *)(uintptr_t)(X86_64_PHYS_MAP_BASE + backing), 0, X86_64_PAGE_SIZE);
        as->owned[as->nowned++] = backing;
        if (!x86_64_aspace_map_page(as, va + i * X86_64_PAGE_SIZE, backing, prot))
            break;
        mapped++;
    }
    if (mapped == pages)
        return TRUE;

    /* Roll back so the failed call leaves nothing: unmap what it mapped, cut
     * off the table pages it had to create (every page attempted, the one
     * that failed included, since the failure may have come part-way down),
     * then free the backing and the new tables it took. */
    for (i = 0; i < mapped; i++)
        x86_64_unmap_user_page(as->pml4_phys, va + i * X86_64_PAGE_SIZE);
    for (i = 0; i <= mapped && i < pages; i++)
        unlink_new_tables(as, first_tables, va + i * X86_64_PAGE_SIZE);
    if (x86_64_read_cr3() == as->pml4_phys)
        x86_64_write_cr3(as->pml4_phys);        /* flush stale translations */
    while (as->nowned > first_owned)
        x86_64_pmem_free_pages(as->owned[--as->nowned], 1);
    while (as->count > first_tables)
        x86_64_pmem_free_pages(as->pages[--as->count], 1);
    return FALSE;
}

/*
 * Private segment of a program image: fresh zeroed pages with the segment's
 * own permissions, then the file bytes copied in through the direct map --
 * which, unlike x86_64_aspace_copy_to_user(), does not need the page to be
 * user-writable, so a read-only or executable segment can be filled.  The
 * map_private() contract (all-or-nothing, no leftovers) carries over: the
 * copy cannot fail once the pages are mapped.
 */
BOOL x86_64_aspace_load_private(X86_64_ASPACE *as, UQUAD va, UQUAD memsz,
                                UWORD prot, const void *src, ULONG filesz)
{
    const UBYTE *from = src;
    UQUAD base = va & ~(X86_64_PAGE_SIZE - 1);
    UQUAD at = va;

    if (filesz > memsz || !memsz || memsz > X86_64_USER_VA_LIMIT)
        return FALSE;
    if (!x86_64_aspace_map_private(as, base, memsz + (va - base), prot))
        return FALSE;
    while (filesz) {
        UQUAD phys;
        ULONG chunk = (ULONG)(X86_64_PAGE_SIZE - (at & (X86_64_PAGE_SIZE - 1)));

        if (chunk > filesz)
            chunk = filesz;
        x86_64_aspace_translate(as, at, &phys, NULL);
        memcpy((UBYTE *)(uintptr_t)(X86_64_PHYS_MAP_BASE + phys), from, chunk);
        from += chunk;
        at += chunk;
        filesz -= chunk;
    }
    return TRUE;
}

/*
 * Kernel stacks.  A page run from the physical allocator, reached through
 * the direct map (so the same address in every address space); the stack
 * pointer starts at the top less 8, as the syscall entry stub expects
 * ("as if a return address had just been pushed").
 */
UQUAD x86_64_kstack_alloc(UQUAD *top)
{
    UQUAD phys = x86_64_pmem_try_alloc_pages(X86_64_KSTACK_PAGES, 0);

    if (phys == X86_64_PMEM_NONE)
        return 0;
    *top = X86_64_PHYS_MAP_BASE + phys + X86_64_KSTACK_PAGES * X86_64_PAGE_SIZE - 8;
    return phys;
}

void x86_64_kstack_free(UQUAD phys)
{
    if (phys)
        x86_64_pmem_free_pages(phys, X86_64_KSTACK_PAGES);
}

/*
 * Supervisor-only access to someone else's process blocks, for exactly as long
 * as ring 0 needs it.  Pexec() from a ring-3 process builds the child's
 * basepage, environment and program under the launcher's page tables, in
 * blocks allocated after that address space exists; each is borrowed into it
 * (writable, no user bit, pinned so it cannot be freed from under the
 * mapping) when it is allocated and returned when it is freed.  Nothing else
 * in the window is reachable.
 */
BOOL x86_64_aspace_borrow(X86_64_ASPACE *as, UQUAD va, UQUAD bytes)
{
    if (x86_64_aspace_map_procmem(as, va, bytes, ASPACE_PROT_WRITE))
        return TRUE;
    x86_64_aspace_unborrow(as, va, bytes);      /* a half-finished borrow */
    return FALSE;
}

void x86_64_aspace_unborrow(X86_64_ASPACE *as, UQUAD va, UQUAD bytes)
{
    UQUAD page, end;
    ULONG i;

    for (i = as->npins; i-- > 0; )
        if (as->pins[i].va == va && as->pins[i].bytes == bytes) {
            as->pins[i] = as->pins[--as->npins];
            x86_64_procmem_pin(va, bytes, -1);
            break;
        }
    page = va & ~(X86_64_PAGE_SIZE - 1);
    end = (va + bytes + X86_64_PAGE_SIZE - 1) & ~(X86_64_PAGE_SIZE - 1);
    for (; page < end; page += X86_64_PAGE_SIZE)
        x86_64_unmap_user_page(as->pml4_phys, page);
    if (x86_64_read_cr3() == as->pml4_phys)
        x86_64_write_cr3(as->pml4_phys);        /* flush stale translations */
}

/*
 * Gives the caller access to a block it borrowed: the same pages, now with
 * the user bit and the given permissions (ASPACE_PROT_*), e.g. the basepage and
 * TPA of a child that Pexec(PE_BASEPAGE) is about to hand to a ring-3 caller.
 * The block stays pinned and recorded as a borrow, so it goes back exactly as
 * before (x86_64_aspace_unborrow()).  FALSE only if a page table could not be
 * written, which cannot happen for pages already mapped.
 */
BOOL x86_64_aspace_regrant(X86_64_ASPACE *as, UQUAD va, UQUAD bytes, UWORD prot)
{
    UQUAD page = va & ~(X86_64_PAGE_SIZE - 1);
    UQUAD end = (va + bytes + X86_64_PAGE_SIZE - 1) & ~(X86_64_PAGE_SIZE - 1);
    BOOL ok = TRUE;

    for (; page < end; page += X86_64_PAGE_SIZE)
        if (!x86_64_aspace_translate(as, page, NULL, NULL) ||
            !x86_64_aspace_map_page(as, page, x86_64_procmem_phys_of(page), prot))
            ok = FALSE;
    if (x86_64_read_cr3() == as->pml4_phys)
        x86_64_write_cr3(as->pml4_phys);        /* flush stale translations */
    return ok;
}
