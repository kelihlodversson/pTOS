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
};

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
