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
#include "../../../bdos/kaspace.h"
#include "biosext.h"
#include "gemerror.h"
#include "pgtable.h"
#include "pmem.h"
#include "pc_x86_64_memory.h"
#if CONF_WITH_X86_64_MEMTEST
#include "pc_x86_64_memtest.h"
#endif
#if CONF_WITH_MMAP
#include "../../../bdos/fileback.h"
#endif

#define ASPACE_MAGIC    0x41535041434531ULL     /* "ASPACE1" */
#define FIRST_CAPACITY  16

/* pages of a per-process kernel stack (kstack_alloc()) */
#define X86_64_KSTACK_PAGES 4

#include "aspace_cow_types.h"
struct aspace {
    UQUAD magic;
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
    LONG test_borrow_failure;
#endif
#if CONF_WITH_PRIVATE_CLONE
    struct cow_mapping *cow;
    struct cow_index *cow_root;
    BOOL clone_building;
#endif
    UQUAD pml4_phys;
    UQUAD *pages;               /* kheap vector: PML4 first, then tables */
    ULONG count;
    ULONG capacity;
    struct pin {                /* procmem ranges mapped (and so pinned) */
        UQUAD va, bytes;
        BOOL retired;           /* exact pin remains owned until deferred unborrow */
    } *pins;
    ULONG npins, pin_capacity;
    ULONG nowned;               /* owned mappings, including COW file-page references. Found */
                                /* by X86_64_PTE_OWNED in the leaf entry, not listed */
};

#define PTE_PRESENT  0x1ULL
#define PTE_WRITABLE 0x2ULL
#define PTE_USER     0x4ULL
#define PTE_PS       0x80ULL
#define PTE_NX       0x8000000000000000ULL
#define PTE_ADDR     0xFFFFFFFFFF000ULL

#if CONF_WITH_MMAP
#define PTE_FILE 0x400ULL
/* Every record owns one physical page and a backing-object reference.
 * references counts mapped PTEs; the final PTE drops both page and record. */
#include "aspace_file_index.h"
static ULONG file_page_count, file_page_refs, file_cross_shares;

ULONG aspace_file_cross_shares(void) { return file_cross_shares; }
ULONG aspace_file_pages(void) { return file_page_count; }
ULONG aspace_file_references(void) { return file_page_refs; }

static void file_page_put(UQUAD physical)
{
    FILE_PAGE *p = file_page_find(physical);
    if (!p || !p->references) panic("missing file page reference");
    file_page_refs--;
    if (--p->references) return;
    file_page_unpublish(p); file_page_count--;
    x86_64_pmem_free_pages(physical, 1);
    fileback_put(p->backing); kfree(p);
}
BOOL aspace_file_cow(const ASPACE *as, UQUAD va)
{
    UQUAD *pte = x86_64_user_pte(as->pml4_phys, va);
    return pte && (*pte & (PTE_PRESENT | PTE_FILE)) == (PTE_PRESENT | PTE_FILE);
}
#else
#define PTE_FILE 0
#endif

static void release_backing(UQUAD entry)
{
#if CONF_WITH_PRIVATE_CLONE
    if (cow_release_backing(entry & PTE_ADDR)) return;
#endif
#if CONF_WITH_MMAP
    if (entry & PTE_FILE) { file_page_put(entry & PTE_ADDR); return; }
#endif
    x86_64_pmem_free_pages(entry & PTE_ADDR, 1);
}

/* Grows the vector and records a page; false (and nothing recorded) if the
 * vector could not grow. */
static BOOL record_page(struct aspace *as, UQUAD phys)
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
    UQUAD phys =
#if CONF_WITH_PRIVATE_CLONE
        (((ASPACE *)ctx)->clone_building && clone_injected()) ? X86_64_PMEM_NONE :
#endif
        x86_64_pmem_try_alloc_pages(1, 0);

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
 * framebuffer, if there is one.  Each is a 2 MiB mapping copied
 * from the kernel's own tables, supervisor-only -- ring 3 gets a fault from
 * all of it, the null guard included -- and placed so as not to overlap
 * anything a process maps for itself (include/procmem.h's layout).
 */
static BOOL map_kernel_low(struct aspace *as)
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

ASPACE *aspace_create(void)
{
    struct aspace *as = kalloc(sizeof(*as));
    UQUAD pml4;

    if (!as)
        return NULL;
    pml4 = x86_64_pmem_try_alloc_pages(1, 0);
    if (pml4 == X86_64_PMEM_NONE) {
        kfree(as);
        return NULL;
    }
    as->magic = ASPACE_MAGIC;
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
    as->test_borrow_failure=-1;
#endif
    as->pml4_phys = pml4;
    if (!record_page(as, pml4)) {
        x86_64_pmem_free_pages(pml4, 1);
        kfree(as);
        return NULL;
    }
    x86_64_new_address_space(pml4);
    if (!map_kernel_low(as)) {
        aspace_destroy(as);
        return NULL;
    }
    return as;
}

/* Frees every private page by walking the user half of the page tables for
 * leaf entries marked X86_64_PTE_OWNED.  The 2 MiB mappings of the kernel's low
 * data (PS entries) and the process-window pages are not marked and not
 * followed. */
static void free_owned_pages(struct aspace *as)
{
    UQUAD *l4 = (UQUAD *)(uintptr_t)(X86_64_PHYS_MAP_BASE + as->pml4_phys);
    ULONG i4, i3, i2, i1;

    for (i4 = 0; i4 < 256; i4++) {
        UQUAD *l3;

        if (!(l4[i4] & PTE_PRESENT))
            continue;
        l3 = (UQUAD *)(uintptr_t)(X86_64_PHYS_MAP_BASE + (l4[i4] & PTE_ADDR));
        for (i3 = 0; i3 < 512; i3++) {
            UQUAD *l2;

            if (!(l3[i3] & PTE_PRESENT) || (l3[i3] & PTE_PS))
                continue;
            l2 = (UQUAD *)(uintptr_t)(X86_64_PHYS_MAP_BASE + (l3[i3] & PTE_ADDR));
            for (i2 = 0; i2 < 512; i2++) {
                UQUAD *l1;

                if (!(l2[i2] & PTE_PRESENT) || (l2[i2] & PTE_PS))
                    continue;
                l1 = (UQUAD *)(uintptr_t)(X86_64_PHYS_MAP_BASE + (l2[i2] & PTE_ADDR));
                for (i1 = 0; i1 < 512; i1++)
                    if ((l1[i1] & PTE_PRESENT) && (l1[i1] & X86_64_PTE_OWNED)) {
#if CONF_WITH_PRIVATE_CLONE
                        cow_unlink_mapping(as, ((UQUAD)i4 << 39) | ((UQUAD)i3 << 30) |
                            ((UQUAD)i2 << 21) | ((UQUAD)i1 << 12));
#endif
                        release_backing(l1[i1]);
                        l1[i1] = 0;
                        as->nowned--;
                    }
            }
        }
    }
}

UQUAD aspace_enter(ASPACE *as)
{
    UQUAD previous = x86_64_read_cr3();

    x86_64_write_cr3(as->pml4_phys);
    return previous;
}

void aspace_leave(UQUAD previous)
{
    x86_64_write_cr3(previous);
}

void aspace_destroy(ASPACE *as)
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
        procmem_pin(as->pins[i].va, as->pins[i].bytes, -1);
    kfree(as->pins);
    free_owned_pages(as);
    for (i = 0; i < as->count; i++)
        x86_64_pmem_free_pages(as->pages[i], 1);
    kfree(as->pages);
    kfree(as);
}

UQUAD aspace_root(const ASPACE *as)
{
    return as->pml4_phys;
}

BOOL aspace_is_current(const ASPACE *as)
{
    return as && as->pml4_phys == x86_64_read_cr3();
}

ULONG aspace_table_pages(const ASPACE *as)
{
    return as->count;
}

static BOOL map_user_page(ASPACE *as, UQUAD va, UQUAD phys, UWORD prot, BOOL backing, UWORD logical)
{
#if CONF_WITH_PRIVATE_CLONE
    if (cow_find(as, va)) return FALSE;
#endif
    if (!(backing ? kaspace_materialization_change(as,va,X86_64_PAGE_SIZE,logical) :
                    kaspace_mapping_change(as,va,X86_64_PAGE_SIZE))) return FALSE;
    if ((va | phys) & (X86_64_PAGE_SIZE - 1))
        return FALSE;
    /* The 32-bit ABI constraint, checked where the mapping is made: the
     * whole page must sit below 4 GiB.  phys is deliberately unchecked. */
    if (va >= USER_VA_LIMIT || USER_VA_LIMIT - va < X86_64_PAGE_SIZE)
        return FALSE;
    return x86_64_map_user_page(as->pml4_phys, va, phys,
                                !!(prot & ASPACE_PROT_WRITE),
                                !!(prot & ASPACE_PROT_EXEC),
                                !!(prot & ASPACE_PROT_USER),
                                alloc_table_page, as) == 0;
}

BOOL aspace_map_page(ASPACE *as, UQUAD va, UQUAD phys, UWORD prot)
{
    return map_user_page(as,va,phys,prot,FALSE,0);
}

#define BORROW_ROOT_PAGES(as) 0UL /* PML4 is already in pages[] */
#define BORROW_ROOT(as) ((as)->pml4_phys)
#define BORROW_ALIAS(p) ((void *)(uintptr_t)(X86_64_PHYS_MAP_BASE+(p)))
#define BORROW_FREE(p) x86_64_pmem_free_pages(p,1)
#define BORROW_MAP(as,va,p,prot) (x86_64_map_user_page((as)->pml4_phys,va,p,!!((prot)&ASPACE_PROT_WRITE),!!((prot)&ASPACE_PROT_EXEC),!!((prot)&ASPACE_PROT_USER),alloc_table_page,as)==0)
#define BORROW_OWNED(as,va) borrow_owned(as,va)
#define BORROW_FLUSH(as) do { if(x86_64_read_cr3()==(as)->pml4_phys) x86_64_write_cr3((as)->pml4_phys); } while (0)
#define BORROW_RESTORED(as) BORROW_FLUSH(as)
#if CONF_WITH_PRIVATE_CLONE
static BOOL borrow_owned(ASPACE *as,VADDR va)
{
    UQUAD *pte=x86_64_user_pte(as->pml4_phys,va);
    return pte && (*pte&PTE_PRESENT) && (*pte&X86_64_PTE_OWNED);
}
#endif
#define BORROW_HASH_COUNT(as) ((as)->count)
#define BORROW_HASH_PAGE(as,i) ((const UBYTE *)(uintptr_t)(X86_64_PHYS_MAP_BASE+(as)->pages[i]))
#include "aspace_procmem.h"

/* ---- walking, validation and private memory ------------------------- */

BOOL aspace_translate(const ASPACE *as, UQUAD va, UQUAD *phys, UWORD *prot)
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

BOOL aspace_user_range_ok(const ASPACE *as, UQUAD va, UQUAD bytes, BOOL write)
{
    UQUAD page, last;

    if (!bytes || va >= USER_VA_LIMIT || bytes > USER_VA_LIMIT - va)
        return FALSE;
    last = (va + bytes - 1) & ~(X86_64_PAGE_SIZE - 1);
    for (page = va & ~(X86_64_PAGE_SIZE - 1); ; page += X86_64_PAGE_SIZE) {
        UWORD prot;

        if (!aspace_translate(as, page, NULL, &prot) ||
            !(prot & ASPACE_PROT_USER) || (write && !(prot & ASPACE_PROT_WRITE)))
            return FALSE;
        if (page == last)
            break;
    }
    return TRUE;
}

/* Copies page by page through the direct map; the range was validated. */
static void copy_user(const ASPACE *as, UBYTE *kernel, UQUAD va, ULONG bytes, BOOL to_user)
{
    while (bytes) {
        UQUAD phys;
        ULONG chunk = (ULONG)(X86_64_PAGE_SIZE - (va & (X86_64_PAGE_SIZE - 1)));
        UBYTE *user;

        if (chunk > bytes)
            chunk = bytes;
        aspace_translate(as, va, &phys, NULL);
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
static BOOL range_copyable(const ASPACE *as, UQUAD va, UQUAD bytes, BOOL write)
{
    UQUAD page, last;

    if (!aspace_user_range_ok(as, va, bytes, write))
        return FALSE;
    last = (va + bytes - 1) & ~(X86_64_PAGE_SIZE - 1);
    for (page = va & ~(X86_64_PAGE_SIZE - 1); ; page += X86_64_PAGE_SIZE) {
        UQUAD phys;

        if (!aspace_translate(as, page, &phys, NULL) || !x86_64_pmem_is_allocated(phys))
            return FALSE;
        if (page == last)
            break;
    }
    return TRUE;
}

BOOL aspace_copy_to_user_ok(const ASPACE *as, UQUAD va, ULONG bytes)
{
    return range_copyable(as, va, bytes, TRUE);
}

void *aspace_user_write_alias(const ASPACE *as, UQUAD va, ULONG bytes)
{
    PADDR physical;
    if (!bytes || bytes > USER_PAGE_SIZE - (va & (USER_PAGE_SIZE - 1)) ||
        !range_copyable(as, va, bytes, TRUE) || !aspace_translate(as, va, &physical, NULL)) return NULL;
    return (void *)(uintptr_t)(X86_64_PHYS_MAP_BASE + physical);
}

BOOL aspace_copy_from_user(const ASPACE *as, void *dst, UQUAD va, ULONG bytes)
{
    if (!range_copyable(as, va, bytes, FALSE))
        return FALSE;
    copy_user(as, dst, va, bytes, FALSE);
    return TRUE;
}

BOOL aspace_copy_to_user(const ASPACE *as, UQUAD va, const void *src, ULONG bytes)
{
    if (!range_copyable(as, va, bytes, TRUE))
        return FALSE;
    copy_user(as, (UBYTE *)src, va, bytes, TRUE);
    return TRUE;
}

ULONG aspace_private_pages(const ASPACE *as)
{
    return as->nowned;
}

/* True iff the table page at phys was allocated at or after index `first`
 * of the address space's table-page record. */
static BOOL table_is_newer(const struct aspace *as, ULONG first, UQUAD phys)
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
static void unlink_new_tables(const struct aspace *as, ULONG first, UQUAD va)
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

/*
 * The most aspace_map_private() can map now: the free physical memory less
 * the reserve and the cost it checks for below (a page of tables per 512
 * pages, two more), and a few pages for the bookkeeping a caller may grow
 * first (a kalloc()ed table).
 */
VADDR aspace_mappable_bytes(void)
{
    UQUAD free = pmem_free_bytes(), keep, pages;

    keep = ASPACE_FAULT_RESERVE + ASPACE_FAULT_COST + 6 * X86_64_PAGE_SIZE;
    if (free <= keep)
        return 0;
    pages = (free - keep) / X86_64_PAGE_SIZE;
    pages -= pages / 513 + 1;           /* pages + pages / 512 tables fit */
    return pages * X86_64_PAGE_SIZE;
}

BOOL aspace_map_private(ASPACE *as, UQUAD va, UQUAD bytes, UWORD prot)
{
    UQUAD pages, i, mapped = 0, backing;
    ULONG first_tables = as->count;

    if (!kaspace_mapping_change(as, va, bytes)) return FALSE;
    if ((va & (X86_64_PAGE_SIZE - 1)) || !bytes || va >= USER_VA_LIMIT ||
        bytes > USER_VA_LIMIT - va)
        return FALSE;
    pages = (bytes + X86_64_PAGE_SIZE - 1) / X86_64_PAGE_SIZE;
    if (va + pages * X86_64_PAGE_SIZE > USER_VA_LIMIT)
        return FALSE;

    /* all-or-nothing: refuse an overlap before touching anything */
    for (i = 0; i < pages; i++)
        if (aspace_translate(as, va + i * X86_64_PAGE_SIZE, NULL, NULL))
            return FALSE;

    /* the pages, the tables they need (one per 2 MiB, and the three above) and
     * the bookkeeping must leave the kernel's reserve alone, as a lazy fault
     * does (aspace_fault_in()) */
    if (pmem_free_bytes() < ASPACE_FAULT_RESERVE + ASPACE_FAULT_COST +
                                   (pages + pages / 512 + 2) * X86_64_PAGE_SIZE)
        return FALSE;

    for (i = 0; i < pages; i++) {
        UQUAD *pte;

        backing = x86_64_pmem_try_alloc_pages(1, 0);
        if (backing == X86_64_PMEM_NONE)
            break;
        memset((void *)(uintptr_t)(X86_64_PHYS_MAP_BASE + backing), 0, X86_64_PAGE_SIZE);
        if (!aspace_map_page(as, va + i * X86_64_PAGE_SIZE, backing, prot)) {
            x86_64_pmem_free_pages(backing, 1);
            break;
        }
        pte = x86_64_user_pte(as->pml4_phys, va + i * X86_64_PAGE_SIZE);
        *pte |= X86_64_PTE_OWNED;
        as->nowned++;
        mapped++;
    }
    if (mapped == pages)
        return TRUE;

    /* Roll back so the failed call leaves nothing: unmap what it mapped, cut
     * off the table pages it had to create (every page attempted, the one
     * that failed included, since the failure may have come part-way down),
     * then free the backing and the new tables it took. */
    for (i = 0; i < mapped; i++) {
        UQUAD *pte = x86_64_user_pte(as->pml4_phys, va + i * X86_64_PAGE_SIZE);

        x86_64_pmem_free_pages(*pte & PTE_ADDR, 1);
        as->nowned--;
        x86_64_unmap_user_page(as->pml4_phys, va + i * X86_64_PAGE_SIZE);
    }
    for (i = 0; i <= mapped && i < pages; i++)
        unlink_new_tables(as, first_tables, va + i * X86_64_PAGE_SIZE);
    if (x86_64_read_cr3() == as->pml4_phys)
        x86_64_write_cr3(as->pml4_phys);        /* flush stale translations */
    while (as->count > first_tables)
        x86_64_pmem_free_pages(as->pages[--as->count], 1);
    return FALSE;
}

#if CONF_WITH_MMAP
/* 1: mapped/private copy ready, 2: allocation failure, 3: backing I/O error.
 * Logical permissions are checked by the reservation owner before this call. */
int aspace_fault_file(ASPACE *as, UQUAD va, FILE_BACKING *backing, ULONG offset,
                        UWORD prot, BOOL write)
{
    FILE_PAGE *p;
    UQUAD physical, old = 0, *pte;
    BOOL copy = write && (prot & ASPACE_PROT_WRITE);
    va &= ~(X86_64_PAGE_SIZE - 1);
    if (va >= USER_VA_LIMIT || (offset & (X86_64_PAGE_SIZE - 1))) return 3;
    pte = x86_64_user_pte(as->pml4_phys, va);
    if (pte && (*pte & PTE_PRESENT)) {
        if (!copy || !(*pte & PTE_FILE)) return 3;
        old = *pte & PTE_ADDR;
    }
    if (!kaspace_materialization_change(as, va, X86_64_PAGE_SIZE, prot)) return 2;
    if (old) p = file_page_find(old);
    else for (p = file_pages; p; p = p->next)
        if (p->backing == backing && p->offset == offset) break;
    /* A shared-page hit may still need new page tables and bookkeeping. */
    if (pmem_free_bytes() < ASPACE_FAULT_RESERVE + ASPACE_FAULT_COST) return 2;
    if (!copy && p) {
        if (!map_user_page(as, va, p->physical, prot & ~ASPACE_PROT_WRITE, TRUE, prot)) return 2;
        *x86_64_user_pte(as->pml4_phys, va) |= X86_64_PTE_OWNED | PTE_FILE;
        if (p->first_root != as->pml4_phys) file_cross_shares++;
        p->references++; file_page_refs++; as->nowned++;
        return 1;
    }
    physical = x86_64_pmem_try_alloc_pages(1, 0);
    if (physical == X86_64_PMEM_NONE) return 2;
    if (p) memcpy((void *)(uintptr_t)(X86_64_PHYS_MAP_BASE + physical),
                    (const void *)(uintptr_t)(X86_64_PHYS_MAP_BASE + p->physical), X86_64_PAGE_SIZE);
    else if (fileback_read(backing, offset,
                (void *)(uintptr_t)(X86_64_PHYS_MAP_BASE + physical), X86_64_PAGE_SIZE) != E_OK) {
        x86_64_pmem_free_pages(physical, 1); return 3;
    }
    if (!copy) {
        p = kalloc(sizeof(*p));
        if (!p) { x86_64_pmem_free_pages(physical, 1); return 2; }
        p->backing = backing; p->offset = offset; p->physical = physical; p->first_root = as->pml4_phys; p->references = 1;
    }
    /* Replacing an existing leaf uses the present page tables; no old entry
     * is dropped until the complete replacement is ready. */
    if (!map_user_page(as, va, physical, copy ? prot : prot & ~ASPACE_PROT_WRITE, TRUE, prot)) {
        if (!copy) kfree(p);
        x86_64_pmem_free_pages(physical, 1); return 2;
    }
    pte = x86_64_user_pte(as->pml4_phys, va);
    *pte |= X86_64_PTE_OWNED;
    if (!copy) {
        *pte |= PTE_FILE; file_page_publish(p);
        fileback_hold(backing); file_page_count++; file_page_refs++;
    }
    if (old) file_page_put(old);
    else as->nowned++;
    if (x86_64_read_cr3() == as->pml4_phys) x86_64_write_cr3(as->pml4_phys);
    return 1;
}
#endif

BOOL aspace_unmap_private(ASPACE *as, UQUAD va, UQUAD bytes)
{
    UQUAD pages, i;

    if (!kaspace_mapping_change(as, va, bytes)) return FALSE;
    if ((va & (X86_64_PAGE_SIZE - 1)) || !bytes || va >= USER_VA_LIMIT ||
        bytes > USER_VA_LIMIT - va)
        return FALSE;
    pages = (bytes + X86_64_PAGE_SIZE - 1) / X86_64_PAGE_SIZE;

    /* all-or-nothing: every page must be one of ours before any is touched */
    for (i = 0; i < pages; i++) {
        UQUAD *pte = x86_64_user_pte(as->pml4_phys, va + i * X86_64_PAGE_SIZE);

        if (!pte || !(*pte & PTE_PRESENT) || !(*pte & X86_64_PTE_OWNED))
            return FALSE;
    }
    for (i = 0; i < pages; i++) {
        UQUAD *pte = x86_64_user_pte(as->pml4_phys, va + i * X86_64_PAGE_SIZE);
        UQUAD entry = *pte;

        *pte = 0;
#if CONF_WITH_PRIVATE_CLONE
        cow_unlink_mapping(as, va + i * X86_64_PAGE_SIZE);
#endif
        release_backing(entry);
        as->nowned--;
    }
    if (x86_64_read_cr3() == as->pml4_phys)
        x86_64_write_cr3(as->pml4_phys);        /* flush stale translations */
    return TRUE;
}

BOOL aspace_fault_in(ASPACE *as, UQUAD va, UWORD prot)
{
    UQUAD page = va & ~(X86_64_PAGE_SIZE - 1), backing, *pte;

    if (va >= USER_VA_LIMIT)
        return FALSE;
    if (aspace_translate(as, page, NULL, NULL))
        return TRUE;                    /* (a racing mapping: nothing to do) */
    if (!kaspace_materialization_change(as, page, X86_64_PAGE_SIZE, prot)) return FALSE;
    /* the page, the tables it may need (three) and the bookkeeping's growth must
     * all leave the reserve alone */
    if (pmem_free_bytes() < ASPACE_FAULT_RESERVE + ASPACE_FAULT_COST)
        return FALSE;
    backing = x86_64_pmem_try_alloc_pages(1, 0);
    if (backing == X86_64_PMEM_NONE)
        return FALSE;
    memset((void *)(uintptr_t)(X86_64_PHYS_MAP_BASE + backing), 0, X86_64_PAGE_SIZE);
    if (!map_user_page(as, page, backing, prot, TRUE, prot)) {
        x86_64_pmem_free_pages(backing, 1);
        return FALSE;
    }
    pte = x86_64_user_pte(as->pml4_phys, page);
    *pte |= X86_64_PTE_OWNED;
    as->nowned++;
    return TRUE;
}

static BOOL release_lazy_allowed(const ASPACE *as, UQUAD va, UQUAD bytes, BOOL retired)
{
    UQUAD pages, i;

    if (!retired && !kaspace_mapping_can_change(as, va, bytes)) return FALSE;
    if ((va & (X86_64_PAGE_SIZE - 1)) || !bytes || va >= USER_VA_LIMIT ||
        bytes > USER_VA_LIMIT - va)
        return FALSE;
    pages = (bytes + X86_64_PAGE_SIZE - 1) / X86_64_PAGE_SIZE;

    /* all-or-nothing: a page that is there must be one of ours */
    for (i = 0; i < pages; i++) {
        UQUAD *pte = x86_64_user_pte(as->pml4_phys, va + i * X86_64_PAGE_SIZE);

        if (pte && (*pte & PTE_PRESENT) && !(*pte & X86_64_PTE_OWNED))
            return FALSE;
    }
    return TRUE;
}

BOOL aspace_release_lazy_allowed(const ASPACE *as,UQUAD va,UQUAD bytes)
{ return release_lazy_allowed(as,va,bytes,FALSE); }

static BOOL release_lazy_pages(ASPACE *as, UQUAD va, UQUAD bytes, BOOL retired)
{
    VADDR pages, i;
    if (!release_lazy_allowed(as,va,bytes,retired)) return FALSE;
    if (retired ? !kaspace_retirement_change(as,va,bytes,TRUE) :
                  !kaspace_mapping_change(as,va,bytes)) return FALSE;
    pages = (bytes+X86_64_PAGE_SIZE-1)/X86_64_PAGE_SIZE;
    for (i = 0; i < pages; i++) {
        UQUAD *pte = x86_64_user_pte(as->pml4_phys, va + i * X86_64_PAGE_SIZE);

        if (pte && (*pte & PTE_PRESENT)) {
#if CONF_WITH_PRIVATE_CLONE
            cow_unlink_mapping(as, va + i * X86_64_PAGE_SIZE);
#endif
            release_backing(*pte);
            *pte = 0;
            as->nowned--;
        }
    }
    if (x86_64_read_cr3() == as->pml4_phys)
        x86_64_write_cr3(as->pml4_phys);        /* flush stale translations */
    return TRUE;
}

BOOL aspace_release_lazy(ASPACE *as,UQUAD va,UQUAD bytes)
{ return release_lazy_pages(as,va,bytes,FALSE); }
BOOL aspace_release_lazy_retired(ASPACE *as,VADDR va,VADDR bytes)
{ return release_lazy_pages(as,va,bytes,TRUE); }

/* TRUE iff every page of [va, va + pages) is one of the private pages of `as` */
static BOOL range_is_private(const struct aspace *as, UQUAD va, UQUAD pages)
{
    UQUAD i;

    for (i = 0; i < pages; i++) {
        UQUAD *pte = x86_64_user_pte(as->pml4_phys, va + i * X86_64_PAGE_SIZE);

        if (!pte || !(*pte & PTE_PRESENT) || !(*pte & X86_64_PTE_OWNED) || (*pte & PTE_FILE))
            return FALSE;
    }
    return TRUE;
}

BOOL aspace_private_range_ok(const ASPACE *as, UQUAD va, UQUAD bytes)
{
    UQUAD first;
    if (!as || !bytes || va >= USER_VA_LIMIT || bytes > USER_VA_LIMIT - va) return FALSE;
    first = va & ~(UQUAD)(X86_64_PAGE_SIZE - 1);
    return range_is_private(as, first, (va + bytes - first + X86_64_PAGE_SIZE - 1) / X86_64_PAGE_SIZE);
}

BOOL aspace_move_private(ASPACE *from, ASPACE *to,
                                UQUAD va, UQUAD bytes, UWORD prot)
{
    UQUAD pages, i, mapped = 0;
    ULONG first_tables = to->count;

    if (from == to || (va & (X86_64_PAGE_SIZE - 1)) || !bytes || va >= USER_VA_LIMIT ||
        bytes > USER_VA_LIMIT - va)
        return FALSE;
    if (!kaspace_mapping_can_change(from,va,bytes) ||
        !kaspace_mapping_can_change(to,va,bytes)) return FALSE;
    if (!kaspace_mapping_change(from, va, bytes) ||
        !kaspace_mapping_change(to, va, bytes)) return FALSE;
    pages = (bytes + X86_64_PAGE_SIZE - 1) / X86_64_PAGE_SIZE;
    if (!range_is_private(from, va, pages))
        return FALSE;
#if CONF_WITH_PRIVATE_CLONE
    for (i = 0; i < pages; i++) if (cow_find(from, va + i * X86_64_PAGE_SIZE)) return FALSE;
#endif
    for (i = 0; i < pages; i++)
        if (aspace_translate(to, va + i * X86_64_PAGE_SIZE, NULL, NULL))
            return FALSE;

    for (i = 0; i < pages; i++) {
        UQUAD *src = x86_64_user_pte(from->pml4_phys, va + i * X86_64_PAGE_SIZE);

        if (!aspace_map_page(to, va + i * X86_64_PAGE_SIZE, *src & PTE_ADDR, prot))
            break;
        *x86_64_user_pte(to->pml4_phys, va + i * X86_64_PAGE_SIZE) |= X86_64_PTE_OWNED;
        mapped++;
    }
    if (mapped != pages) {
        /* give back what `to` took: its mappings and the table pages it needed;
         * the pages themselves are still `from`'s */
        for (i = 0; i < mapped; i++)
            x86_64_unmap_user_page(to->pml4_phys, va + i * X86_64_PAGE_SIZE);
        for (i = 0; i <= mapped && i < pages; i++)
            unlink_new_tables(to, first_tables, va + i * X86_64_PAGE_SIZE);
        if (x86_64_read_cr3() == to->pml4_phys)
            x86_64_write_cr3(to->pml4_phys);
        while (to->count > first_tables)
            x86_64_pmem_free_pages(to->pages[--to->count], 1);
        return FALSE;
    }
    for (i = 0; i < pages; i++) {
        *x86_64_user_pte(from->pml4_phys, va + i * X86_64_PAGE_SIZE) = 0;
        from->nowned--;
        to->nowned++;
    }
    if (x86_64_read_cr3() == from->pml4_phys)
        x86_64_write_cr3(from->pml4_phys);      /* flush stale translations */
    return TRUE;
}

static BOOL protect_private(ASPACE *as, UQUAD va, UQUAD bytes, UWORD prot, BOOL lazy, BOOL relro)
{
    UQUAD pages, i;

    if (!relro && !kaspace_mapping_change(as, va, bytes)) return FALSE;
    if ((va & (X86_64_PAGE_SIZE - 1)) || !bytes || va >= USER_VA_LIMIT ||
        bytes > USER_VA_LIMIT - va)
        return FALSE;
    pages = (bytes + X86_64_PAGE_SIZE - 1) / X86_64_PAGE_SIZE;
    /* Missing pages are legal only for a caller-validated lazy reservation.
     * Validate ownership of every present page before updating any PTE. */
    for (i = 0; i < pages; i++) {
        UQUAD *pte = x86_64_user_pte(as->pml4_phys, va + i * X86_64_PAGE_SIZE);

        if (!pte || !(*pte & PTE_PRESENT)) {
            /* A missing leaf PTE can also mean a foreign large-page
             * mapping; only genuinely absent pages count as lazy holes. */
            if (!lazy || aspace_translate(as, va + i * X86_64_PAGE_SIZE, NULL, NULL))
                return FALSE;
        } else if (!(*pte & X86_64_PTE_OWNED) || (relro && (*pte & PTE_FILE))) {
            return FALSE;
        }
    }
#if CONF_WITH_SHARED_VM_PEXEC_TEST
    if(relro) {
        for(i=0;i<pages;i++) {
            UWORD logical;
            if(!
#if CONF_WITH_PRIVATE_CLONE
                aspace_logical_protection(as,va+i*X86_64_PAGE_SIZE,&logical)
#else
                aspace_translate(as,va+i*X86_64_PAGE_SIZE,NULL,&logical)
#endif
               || !(logical&ASPACE_PROT_USER) || (logical&ASPACE_PROT_EXEC) ||
               (logical&~(ASPACE_PROT_USER|ASPACE_PROT_WRITE))) return FALSE;
        }
        if(!kaspace_relro_change(as,va,bytes)) return FALSE;
    }
#endif
    for (i = 0; i < pages; i++) {
        UQUAD *pte = x86_64_user_pte(as->pml4_phys, va + i * X86_64_PAGE_SIZE);
        UQUAD e;

        if (!pte || !(*pte & PTE_PRESENT))
            continue;
        e = (*pte & (PTE_ADDR | X86_64_PTE_OWNED | PTE_FILE)) | PTE_PRESENT;

        if (prot & ASPACE_PROT_USER)
            e |= PTE_USER;
#if CONF_WITH_PRIVATE_CLONE
        {
            struct cow_mapping *mapping = cow_find(as, va + i * X86_64_PAGE_SIZE);
            if (mapping) mapping->protection = prot;
            if ((prot & ASPACE_PROT_WRITE) && !(*pte & PTE_FILE) && !mapping)
                e |= PTE_WRITABLE;
        }
#else
        if ((prot & ASPACE_PROT_WRITE) && !(*pte & PTE_FILE))
            e |= PTE_WRITABLE;
#endif
        if (!(prot & ASPACE_PROT_EXEC))
            e |= PTE_NX;
        *pte = e;
    }
    if (x86_64_read_cr3() == as->pml4_phys)
        x86_64_write_cr3(as->pml4_phys);
    return TRUE;
}

BOOL aspace_protect_private(ASPACE *as, UQUAD va, UQUAD bytes, UWORD prot)
{
    return protect_private(as, va, bytes, prot, FALSE, FALSE);
}

#if CONF_WITH_SHARED_VM_PEXEC_TEST
BOOL aspace_protect_relro(ASPACE *as,VADDR address,VADDR bytes)
{
    if((bytes&(X86_64_PAGE_SIZE-1)) || !bytes) return FALSE;
    return protect_private(as,address,bytes,ASPACE_PROT_USER,FALSE,TRUE);
}
#endif

BOOL aspace_protect_lazy(ASPACE *as, UQUAD va, UQUAD bytes, UWORD prot)
{
    return protect_private(as, va, bytes, prot, TRUE, FALSE);
}

/*
 * Private segment of a program image: fresh zeroed pages with the segment's
 * own permissions, then the file bytes copied in through the direct map --
 * which, unlike aspace_copy_to_user(), does not need the page to be
 * user-writable, so a read-only or executable segment can be filled.  The
 * map_private() contract (all-or-nothing, no leftovers) carries over: the
 * copy cannot fail once the pages are mapped.
 */
BOOL aspace_load_private(ASPACE *as, UQUAD va, UQUAD memsz,
                                UWORD prot, const void *src, ULONG filesz)
{
    const UBYTE *from = src;
    UQUAD base = va & ~(X86_64_PAGE_SIZE - 1);
    UQUAD at = va;

    if (filesz > memsz || !memsz || memsz > USER_VA_LIMIT)
        return FALSE;
    if (!aspace_map_private(as, base, memsz + (va - base), prot))
        return FALSE;
    while (filesz) {
        UQUAD phys;
        ULONG chunk = (ULONG)(X86_64_PAGE_SIZE - (at & (X86_64_PAGE_SIZE - 1)));

        if (chunk > filesz)
            chunk = filesz;
        aspace_translate(as, at, &phys, NULL);
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
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
static ULONG live_kstacks;
ULONG kstack_test_live(void) { return live_kstacks; }
#endif
PADDR kstack_alloc(uintptr_t *top)
{
    UQUAD phys = x86_64_pmem_try_alloc_pages(X86_64_KSTACK_PAGES, 0);

    if (phys == X86_64_PMEM_NONE)
        return 0;
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
    live_kstacks++;
#endif
    *top = X86_64_PHYS_MAP_BASE + phys + X86_64_KSTACK_PAGES * X86_64_PAGE_SIZE - 8;
    return phys;
}

void kstack_free(PADDR phys)
{
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
    if (phys) live_kstacks--;
#endif
    if (phys)
        x86_64_pmem_free_pages(phys, X86_64_KSTACK_PAGES);
}
#if CONF_WITH_FORK_RETURN_TEST
BOOL kstack_contains(PADDR phys, uintptr_t address)
{
    uintptr_t base = X86_64_PHYS_MAP_BASE + phys;
    return phys && address >= base && address - base < X86_64_KSTACK_PAGES * X86_64_PAGE_SIZE;
}
#endif

#if CONF_WITH_X86_64_MEMTEST
BOOL x86_64_memtest_kstack_contains(UQUAD phys, UQUAD address)
{
    UQUAD base = X86_64_PHYS_MAP_BASE + phys;
    return address >= base && address < base + X86_64_KSTACK_PAGES * X86_64_PAGE_SIZE;
}
UQUAD x86_64_memtest_kstack_bytes(void)
{
    return X86_64_KSTACK_PAGES * X86_64_PAGE_SIZE;
}
#endif

/*
 * Supervisor-only access to someone else's process blocks, for exactly as long
 * as ring 0 needs it.  Pexec() from a ring-3 process builds the child's
 * basepage, environment and program under the launcher's page tables, in
 * blocks allocated after that address space exists; each is borrowed into it
 * (writable, no user bit, pinned so it cannot be freed from under the
 * mapping) when it is allocated and returned when it is freed.  Nothing else
 * in the window is reachable.
 */
BOOL aspace_borrow(ASPACE *as,UQUAD va,UQUAD bytes)
{
    return aspace_map_procmem(as,va,bytes,ASPACE_PROT_WRITE);
}

static void drop_borrow_pin(ASPACE *as, UQUAD va, UQUAD bytes, BOOL retired)
{
    UQUAD page, end;
    ULONG i,j;
    BOOL pinned=FALSE;
    for (i = as->npins; i-- > 0; )
        if (as->pins[i].va == va && as->pins[i].bytes == bytes && as->pins[i].retired == retired) {
            as->pins[i] = as->pins[--as->npins];
            pinned = TRUE;
            break;
        }
#if CONF_WITH_PRIVATE_CLONE
    if (!pinned) return; /* never unmap a copied/private former borrow */

#endif
    page = va & ~(X86_64_PAGE_SIZE - 1);
    end = (va + bytes + X86_64_PAGE_SIZE - 1) & ~(X86_64_PAGE_SIZE - 1);
    for (; page < end; page += X86_64_PAGE_SIZE) {
        /* Another exact borrow pin may still own this same leaf. */
        for (j=0;j<as->npins;j++)
            if (page<as->pins[j].va+as->pins[j].bytes && as->pins[j].va<page+X86_64_PAGE_SIZE) break;
        if (j==as->npins) x86_64_unmap_user_page(as->pml4_phys, page);
    }
    if (x86_64_read_cr3() == as->pml4_phys)
        x86_64_write_cr3(as->pml4_phys);        /* flush stale translations */
    if (pinned) procmem_pin(va, bytes, -1);
}

BOOL aspace_defer_unborrow(ASPACE *as,VADDR va,VADDR bytes)
{
    ULONG i;
    for (i=as->npins;i-- >0;)
        if (as->pins[i].va==va && as->pins[i].bytes==bytes && !as->pins[i].retired) {
            as->pins[i].retired=TRUE;return TRUE;
        }
    /* A PRIVATE_VM snapshot can have copied a former borrow into owned RAM. */
#if CONF_WITH_PRIVATE_CLONE
    return TRUE;
#else
    return FALSE;
#endif
}

void aspace_unborrow(ASPACE *as,UQUAD va,UQUAD bytes)
{
    if (!kaspace_mapping_change(as,va,bytes)) panic("unborrow without mutable ownership");
    drop_borrow_pin(as,va,bytes,FALSE);
}
void aspace_unborrow_retired(ASPACE *as,VADDR va,VADDR bytes)
{
    ULONG i;
    if (!kaspace_retirement_change(as,va,bytes,FALSE)) panic("unborrow without retirement ownership");
    /* The fence is a page union; each existing pin keeps its exact dimensions.
     * Detach each pin once, before procmem notification can reenter teardown. */
    for (;;) {
        UQUAD address=0,length=0;
        for (i=0;i<as->npins;i++)
            if (as->pins[i].retired && as->pins[i].va>=va &&
                as->pins[i].va-va<bytes && as->pins[i].bytes<=bytes-(as->pins[i].va-va)) {
                address=as->pins[i].va;length=as->pins[i].bytes;break;
            }
        if (!length) break;
        drop_borrow_pin(as,address,length,TRUE);
    }
}

/*
 * Gives the caller access to a block it borrowed: the same pages, now with
 * the user bit and the given permissions (ASPACE_PROT_*), e.g. the basepage and
 * TPA of a child that Pexec(PE_BASEPAGE) is about to hand to a ring-3 caller.
 * The block stays pinned and recorded as a borrow, so it goes back exactly as
 * before (aspace_unborrow()).  FALSE only if a page table could not be
 * written, which cannot happen for pages already mapped.
 */
BOOL aspace_regrant(ASPACE *as, UQUAD va, UQUAD bytes, UWORD prot)
{
    UQUAD page = va & ~(X86_64_PAGE_SIZE - 1);
    UQUAD end = (va + bytes + X86_64_PAGE_SIZE - 1) & ~(X86_64_PAGE_SIZE - 1);
    BOOL ok = TRUE;

    if (!kaspace_mapping_change(as, va, bytes)) return FALSE;
    for (; page < end; page += X86_64_PAGE_SIZE)
        if (!aspace_translate(as, page, NULL, NULL) ||
            !aspace_map_page(as, page, procmem_phys_of(page), prot))
            ok = FALSE;
    if (x86_64_read_cr3() == as->pml4_phys)
        x86_64_write_cr3(as->pml4_phys);        /* flush stale translations */
    return ok;
}

#if CONF_WITH_PRIVATE_CLONE
#define COW_FREE(p) x86_64_pmem_free_pages((p), 1)
#define COW_ALLOC() (pmem_free_bytes() < ASPACE_FAULT_RESERVE + ASPACE_FAULT_COST ? X86_64_PMEM_NONE : x86_64_pmem_try_alloc_pages(1, 0))
#define COW_ALIAS(p) ((void *)(uintptr_t)(X86_64_PHYS_MAP_BASE + (p)))
#define COW_RAM(p) x86_64_pmem_is_allocated(p)
#define COW_NONE X86_64_PMEM_NONE
#define COW_CODE_CHANGED() ((void)0)
static void cow_flush(const ASPACE *as)
{
    if (aspace_is_current(as)) x86_64_write_cr3(as->pml4_phys);
}
static BOOL cow_replace(ASPACE *as, VADDR va, PADDR physical, UWORD prot)
{
    UQUAD *pte = x86_64_user_pte(as->pml4_phys, va), value;
    if (!pte || !(*pte & PTE_PRESENT) || !(*pte & X86_64_PTE_OWNED)) return FALSE;
    value = physical | PTE_PRESENT | X86_64_PTE_OWNED;
    if ((*pte & PTE_ADDR) == physical) value |= *pte & 0x60; /* per-root accessed/dirty */
    if (prot & ASPACE_PROT_WRITE) value |= PTE_WRITABLE;
    if (prot & ASPACE_PROT_USER) value |= PTE_USER;
    if (!(prot & ASPACE_PROT_EXEC)) value |= PTE_NX;
    *pte = value;
    return TRUE;
}
static BOOL cow_map(ASPACE *as, VADDR va, PADDR physical, UWORD prot, BOOL owned)
{
    if (!aspace_map_page(as, va, physical, prot)) return FALSE;
    if (owned) {
        *x86_64_user_pte(as->pml4_phys, va) |= X86_64_PTE_OWNED;
        as->nowned++;
    }
    return TRUE;
}
static LONG cow_file_leaf(ASPACE *child, VADDR va, PADDR physical, UWORD protection)
{
#if CONF_WITH_MMAP
    FILE_PAGE *page = file_page_find(physical);
    if (page) {
        if (page->references == ~(ULONG)0 || file_page_refs == ~(ULONG)0) return EAGAIN;
        if (!cow_map(child, va, physical, protection & ~ASPACE_PROT_WRITE, TRUE)) return ENSMEM;
        *x86_64_user_pte(child->pml4_phys, va) |= PTE_FILE;
        page->references++; file_page_refs++;
        return E_OK;
    }
#else
    UNUSED(child); UNUSED(va); UNUSED(physical); UNUSED(protection);
#endif
    return 1;
}
#include "aspace_cow.h"
LONG aspace_clone(ASPACE *source, ASPACE **out)
{
    ASPACE *child;
    UQUAD *l4, *l3, *l2, *l1;
    ULONG i3, i2, i1;
    LONG error = E_OK;
    if (!source || !out || *out) return EINVAL;
    if (!kaspace_cow_change(source, 0, USER_VA_LIMIT)) return EACCDN;
    if (clone_injected()) return ENSMEM;
    child = aspace_create();
    if (!child) return ENSMEM;
    child->clone_building = TRUE;
    l4 = COW_ALIAS(source->pml4_phys);
    if (!(l4[0] & PTE_PRESENT)) goto ready;
    l3 = COW_ALIAS(l4[0] & PTE_ADDR);
    for (i3 = 0; i3 < USER_VA_LIMIT / 0x40000000ULL; i3++) {
        if (!(l3[i3] & PTE_PRESENT)) continue;
        if (l3[i3] & PTE_PS) { if (l3[i3] & PTE_USER) { error = EINVFN; goto failed; } continue; }
        l2 = COW_ALIAS(l3[i3] & PTE_ADDR);
        for (i2 = 0; i2 < 512; i2++) {
            if (!(l2[i2] & PTE_PRESENT)) continue;
            if (l2[i2] & PTE_PS) { if (l2[i2] & PTE_USER) { error = EINVFN; goto failed; } continue; }
            l1 = COW_ALIAS(l2[i2] & PTE_ADDR);
            for (i1 = 0; i1 < 512; i1++) if (l1[i1] & PTE_PRESENT) {
                VADDR va = ((UQUAD)i3 << 30) | ((UQUAD)i2 << 21) | ((UQUAD)i1 << 12);
                UWORD prot;
                aspace_translate(source, va, NULL, &prot);
                error = cow_clone_leaf(source, child, va, l1[i1] & PTE_ADDR, prot,
                                      (l1[i1] & X86_64_PTE_OWNED) != 0,
                                      (l1[i1] & PTE_FILE) != 0);
                if (error) goto failed;
            }
        }
    }
ready:
    cow_finish_source(source, TRUE); child->clone_building = FALSE;
    *out = child; return E_OK;
failed:
    aspace_destroy(child); cow_finish_source(source, FALSE);
    return error;
}
#endif

#if CONF_WITH_SHARED_VM_PEXEC_TEST && CONF_WITH_PRIVATE_CLONE_TEST
#include "shared_pexec_test.h"
void aspace_shared_snapshot(const ASPACE *as,SHARED_PEXEC_SNAPSHOT *out)
{
    ULONG i;
    out->count[SP_TABLE_PAGES]+=aspace_table_pages(as);
    out->count[SP_PRIVATE_PAGES]+=aspace_private_pages(as);
    out->count[SP_MAP_PINS]+=as->npins;
    for(i=0;i<as->npins;i++) {
        out->count[SP_MAP_PIN_BYTES]+=as->pins[i].bytes;
        if(as->pins[i].retired) out->count[SP_RETIRED_PINS]++;
    }
    if(as->pages) out->count[SP_VECTOR_BLOCKS]++;
    if(as->pins) out->count[SP_VECTOR_BLOCKS]++;
    out->count[SP_VECTOR_BYTES]+=(uint64_t)as->capacity*sizeof(*as->pages)+
                                (uint64_t)as->pin_capacity*sizeof(*as->pins);
}
#endif
