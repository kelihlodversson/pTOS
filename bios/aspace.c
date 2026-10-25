/*
 * aspace.c - tracked process address spaces (32-bit MMU configurations)
 *
 * See include/procmem.h for the contract and include/aspace32.h for what the
 * page-table backend of the architecture (bios/arch/<arch>/aspace_hw.c)
 * provides.  An address space owns its root and every table page the backend
 * asks for, recorded in a kheap vector as they are allocated.  Destruction
 * walks that vector, so a mapping that failed half way, a process that exited
 * and a setup that was abandoned all release exactly the same set of pages.
 * Private pages (aspace_map_private()) are found by the OWNED flag of their
 * leaf and freed with the space; borrowed pages (procmem blocks, device
 * pages) are never freed with it.
 *
 * (The x86-64 kernel has its own, bios/machine/pc-x86_64/aspace.c: found
 * first through vpath.)
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#if CONF_WITH_USER_ASPACE && !defined(__x86_64__)

#include "portab.h"
#include "string.h"
#include "kheap.h"
#include "procmem.h"
#include "pmem32.h"
#include "aspace32.h"

#define ASPACE_MAGIC    0x41535033UL            /* "ASP3" */
#define FIRST_CAPACITY  16
#define PAGE            PMEM32_PAGE

/* pages of a per-process kernel stack (kstack_alloc()) */
#define KSTACK_PAGES    4

/* The kernel keeps this much free when a process asks for more memory (its
 * own page tables, kernel stacks and heap come first), and each table page
 * covers 512 pages of 4 KiB. */
#define ASPACE_RESERVE  (32 * PAGE)
#define ASPACE_COST     (8 * PAGE)

struct aspace {
    ULONG magic;
    PADDR root;
    ULONG root_pages;
    PADDR *pages;               /* kheap vector: the table pages (not the root) */
    ULONG count;
    ULONG capacity;
    struct pin {                /* procmem ranges mapped (and so pinned) */
        VADDR va, bytes;
    } *pins;
    ULONG npins, pin_capacity;
    ULONG nowned;               /* private backing pages: ours alone, freed with us */
};

static PADDR kernel_root;       /* the address space of the kernel itself */

/* Grows the vector and records a page; false (and nothing recorded) if the
 * vector could not grow. */
static BOOL record_page(struct aspace *as, PADDR phys)
{
    if (as->count == as->capacity) {
        ULONG cap = as->capacity ? as->capacity * 2 : FIRST_CAPACITY;
        PADDR *grown = kalloc(cap * sizeof(PADDR));

        if (!grown)
            return FALSE;
        if (as->pages) {
            memcpy(grown, as->pages, as->count * sizeof(PADDR));
            kfree(as->pages);
        }
        as->pages = grown;
        as->capacity = cap;
    }
    as->pages[as->count++] = phys;
    return TRUE;
}

/* the backend's table-page allocator: a zeroed page, recorded before it is
 * handed over so it cannot be forgotten */
static PADDR alloc_table_page(void *ctx)
{
    PADDR phys = pmem32_alloc(1, 1);

    if (phys == PMEM32_NONE)
        return PMEM32_NONE;
    if (!record_page(ctx, phys)) {
        pmem32_free(phys, 1);
        return PMEM32_NONE;
    }
    memset(pmem32_virt(phys), 0, PAGE);
    return phys;
}

ASPACE *aspace_create(void)
{
    struct aspace *as = kalloc(sizeof(*as));
    ULONG pages = aspace_hw_root_pages();
    PADDR root;

    if (!as)
        return NULL;
    root = pmem32_alloc(pages, aspace_hw_root_align());
    if (root == PMEM32_NONE) {
        kfree(as);
        return NULL;
    }
    memset(pmem32_virt(root), 0, pages * PAGE);
    aspace_hw_init_root(root);
    as->magic = ASPACE_MAGIC;
    as->root = root;
    as->root_pages = pages;
    return as;
}

static void free_owned_leaf(VADDR va, PADDR phys, UWORD flags, void *ctx)
{
    struct aspace *as = ctx;

    (void)va;
    if (flags & ASPACE_LEAF_OWNED) {
        pmem32_free(phys, 1);
        as->nowned--;
    }
}

ASPACE_ROOT aspace_enter(ASPACE *as)
{
    ASPACE_ROOT previous = aspace_hw_current();

    aspace_hw_switch(as->root);
    return previous;
}

void aspace_leave(ASPACE_ROOT previous)
{
    aspace_hw_switch(previous);
}

void aspace_destroy(ASPACE *as)
{
    ULONG i;

    if (!as || as->magic != ASPACE_MAGIC)
        return;                 /* already destroyed (or never created) */

    /* The tables may not be freed while the CPU still walks them: the
     * Pterm()/error paths run in the dying process's own address space. */
    if (aspace_hw_current() == as->root)
        aspace_hw_switch(kernel_root);

    as->magic = 0;
    for (i = 0; i < as->npins; i++)
        procmem_pin(as->pins[i].va, as->pins[i].bytes, -1);
    kfree(as->pins);
    aspace_hw_walk(as->root, free_owned_leaf, as);
    for (i = 0; i < as->count; i++)
        pmem32_free(as->pages[i], 1);
    kfree(as->pages);
    pmem32_free(as->root, as->root_pages);
    kfree(as);
}

ASPACE_ROOT aspace_root(const ASPACE *as)
{
    return as->root;
}

ULONG aspace_table_pages(const ASPACE *as)
{
    return as->count + as->root_pages;
}

PADDR aspace_kernel_root(void)
{
    return kernel_root;
}

/* flushes the translations if `as` is the address space in use */
static void flush_if_current(const struct aspace *as)
{
    if (aspace_hw_current() == as->root)
        aspace_hw_flush();
}

BOOL aspace_map_page(ASPACE *as, VADDR va, PADDR phys, UWORD prot)
{
    if ((va | phys) & (PAGE - 1))
        return FALSE;
    /* the whole page must lie in the user half */
    if (va >= USER_VA_LIMIT || USER_VA_LIMIT - va < PAGE)
        return FALSE;
    return aspace_hw_map(as->root, va, phys, prot, 0, alloc_table_page, as);
}

BOOL aspace_map_procmem(ASPACE *as, VADDR va, VADDR bytes, UWORD prot)
{
    VADDR page, end;

    if (!bytes || va + bytes < va || !procmem_range_live(va, bytes))
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
    procmem_pin(va, bytes, +1);

    page = va & ~(PAGE - 1);
    end = (va + bytes + PAGE - 1) & ~(PAGE - 1);
    for (; page < end; page += PAGE)
        if (!aspace_map_page(as, page, procmem_phys_of(page), prot))
            return FALSE;
    flush_if_current(as);
    return TRUE;
}

void aspace_shrink_procmem(ASPACE *as, VADDR va, VADDR bytes)
{
    VADDR keep_end = va + ((bytes + PAGE - 1) & ~(PAGE - 1));
    ULONG i;

    for (i = 0; i < as->npins; i++) {
        VADDR old_end, page;

        if (as->pins[i].va != va || as->pins[i].bytes <= keep_end - va)
            continue;
        old_end = (va + as->pins[i].bytes + PAGE - 1) & ~(PAGE - 1);
        for (page = keep_end; page < old_end; page += PAGE)
            aspace_hw_unmap(as->root, page);
        as->pins[i].bytes = keep_end - va;
    }
    flush_if_current(as);
}

/* ---- walking, validation and private memory ------------------------- */

BOOL aspace_translate(const ASPACE *as, VADDR va, PADDR *phys, UWORD *prot)
{
    PADDR p;
    UWORD pr, fl;

    if (va >= USER_VA_LIMIT)            /* the kernel half is not in the tables of a process */
        return FALSE;
    if (!aspace_hw_lookup(as->root, va, &p, &pr, &fl))
        return FALSE;
    if (phys)
        *phys = p + (va & (PAGE - 1));
    if (prot)
        *prot = pr;
    return TRUE;
}

BOOL aspace_user_range_ok(const ASPACE *as, VADDR va, VADDR bytes, BOOL write)
{
    VADDR page, last;

    if (!bytes || va >= USER_VA_LIMIT || bytes > USER_VA_LIMIT - va)
        return FALSE;
    last = (va + bytes - 1) & ~(PAGE - 1);
    for (page = va & ~(PAGE - 1); ; page += PAGE) {
        UWORD prot;

        if (!aspace_translate(as, page, NULL, &prot) ||
            !(prot & ASPACE_PROT_USER) || (write && !(prot & ASPACE_PROT_WRITE)))
            return FALSE;
        if (page == last)
            break;
    }
    return TRUE;
}

/* Copies page by page through the window; the range was validated. */
static void copy_user(const ASPACE *as, UBYTE *kernel, VADDR va, ULONG bytes, BOOL to_user)
{
    while (bytes) {
        PADDR phys = 0;
        ULONG chunk = (ULONG)(PAGE - (va & (PAGE - 1)));
        UBYTE *user;

        if (chunk > bytes)
            chunk = bytes;
        aspace_translate(as, va, &phys, NULL);
        user = (UBYTE *)pmem32_virt(phys);
        if (to_user)
            memcpy(user, kernel, chunk);
        else
            memcpy(kernel, user, chunk);
        kernel += chunk;
        va += chunk;
        bytes -= chunk;
    }
}

/* The kernel copies through the window, so every page must be RAM the
 * allocator owns: a page that is mapped for the process but is device memory,
 * or not RAM at all, is valid for user mode yet cannot be copied here -- refuse
 * it rather than dereference an alias that may fault. */
static BOOL range_copyable(const ASPACE *as, VADDR va, VADDR bytes, BOOL write)
{
    VADDR page, last;

    if (!aspace_user_range_ok(as, va, bytes, write))
        return FALSE;
    last = (va + bytes - 1) & ~(PAGE - 1);
    for (page = va & ~(PAGE - 1); ; page += PAGE) {
        PADDR phys;

        if (!aspace_translate(as, page, &phys, NULL) || !pmem32_is_allocated(phys))
            return FALSE;
        if (page == last)
            break;
    }
    return TRUE;
}

BOOL aspace_copy_from_user(const ASPACE *as, void *dst, VADDR va, ULONG bytes)
{
    if (!range_copyable(as, va, bytes, FALSE))
        return FALSE;
    copy_user(as, dst, va, bytes, FALSE);
    return TRUE;
}

BOOL aspace_copy_to_user(const ASPACE *as, VADDR va, const void *src, ULONG bytes)
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

/*
 * The most aspace_map_private() can map now: the free physical memory less
 * the reserve and the cost it checks for below (a table page per 512 pages,
 * two more), and a few pages for the bookkeeping a caller may grow first.
 */
VADDR aspace_mappable_bytes(void)
{
    ULONG free = pmem32_free_bytes(), keep, pages;

    keep = ASPACE_RESERVE + ASPACE_COST + 6 * PAGE;
    if (free <= keep)
        return 0;
    pages = (free - keep) / PAGE;
    pages -= pages / 513 + 1;           /* pages + pages / 512 tables fit */
    return pages * PAGE;
}

/* undoes a failed mapping: the table pages recorded since `first_tables` go */
static void drop_new_tables(struct aspace *as, ULONG first_tables)
{
    if (as->count > first_tables) {
        aspace_hw_drop_tables(as->root, &as->pages[first_tables], as->count - first_tables);
        flush_if_current(as);
        while (as->count > first_tables)
            pmem32_free(as->pages[--as->count], 1);
    }
}

BOOL aspace_map_private(ASPACE *as, VADDR va, VADDR bytes, UWORD prot)
{
    VADDR pages, i, mapped = 0;
    PADDR backing;
    ULONG first_tables = as->count;

    if ((va & (PAGE - 1)) || !bytes || va >= USER_VA_LIMIT ||
        bytes > USER_VA_LIMIT - va)
        return FALSE;
    pages = (bytes + PAGE - 1) / PAGE;
    if (va + pages * PAGE > USER_VA_LIMIT)
        return FALSE;

    /* all-or-nothing: refuse an overlap before touching anything */
    for (i = 0; i < pages; i++)
        if (aspace_translate(as, va + i * PAGE, NULL, NULL))
            return FALSE;

    /* the pages, the tables they need (one per 2 MiB) and the bookkeeping must
     * leave the kernel's reserve alone */
    if (pmem32_free_bytes() < ASPACE_RESERVE + ASPACE_COST + (pages + pages / 512 + 2) * PAGE)
        return FALSE;

    for (i = 0; i < pages; i++) {
        backing = pmem32_alloc(1, 1);
        if (backing == PMEM32_NONE)
            break;
        memset(pmem32_virt(backing), 0, PAGE);
        if (!aspace_hw_map(as->root, va + i * PAGE, backing, prot, ASPACE_LEAF_OWNED,
                           alloc_table_page, as)) {
            pmem32_free(backing, 1);
            break;
        }
        as->nowned++;
        mapped++;
    }
    if (mapped == pages) {
        flush_if_current(as);
        return TRUE;
    }

    /* Roll back so the failed call leaves nothing: unmap what it mapped and
     * free the backing, then cut off and free the table pages it took. */
    for (i = 0; i < mapped; i++) {
        PADDR phys;
        UWORD pr, fl;

        if (aspace_hw_lookup(as->root, va + i * PAGE, &phys, &pr, &fl)) {
            pmem32_free(phys, 1);
            as->nowned--;
        }
        aspace_hw_unmap(as->root, va + i * PAGE);
    }
    drop_new_tables(as, first_tables);
    flush_if_current(as);
    return FALSE;
}

BOOL aspace_unmap_private(ASPACE *as, VADDR va, VADDR bytes)
{
    VADDR pages, i;

    if ((va & (PAGE - 1)) || !bytes || va >= USER_VA_LIMIT ||
        bytes > USER_VA_LIMIT - va)
        return FALSE;
    pages = (bytes + PAGE - 1) / PAGE;

    /* all-or-nothing: every page must be one of ours before any is touched */
    for (i = 0; i < pages; i++) {
        PADDR phys;
        UWORD pr, fl;

        if (!aspace_hw_lookup(as->root, va + i * PAGE, &phys, &pr, &fl) ||
            !(fl & ASPACE_LEAF_OWNED))
            return FALSE;
    }
    for (i = 0; i < pages; i++) {
        PADDR phys;
        UWORD pr, fl;

        aspace_hw_lookup(as->root, va + i * PAGE, &phys, &pr, &fl);
        aspace_hw_unmap(as->root, va + i * PAGE);
        pmem32_free(phys, 1);
        as->nowned--;
    }
    flush_if_current(as);
    return TRUE;
}

/* TRUE iff every page of [va, va + pages) is one of the private pages of `as` */
static BOOL range_is_private(const struct aspace *as, VADDR va, VADDR pages)
{
    VADDR i;

    for (i = 0; i < pages; i++) {
        PADDR phys;
        UWORD pr, fl;

        if (!aspace_hw_lookup(as->root, va + i * PAGE, &phys, &pr, &fl) ||
            !(fl & ASPACE_LEAF_OWNED))
            return FALSE;
    }
    return TRUE;
}

BOOL aspace_move_private(ASPACE *from, ASPACE *to, VADDR va, VADDR bytes, UWORD prot)
{
    VADDR pages, i, mapped = 0;
    ULONG first_tables = to->count;

    if (from == to || (va & (PAGE - 1)) || !bytes || va >= USER_VA_LIMIT ||
        bytes > USER_VA_LIMIT - va)
        return FALSE;
    pages = (bytes + PAGE - 1) / PAGE;
    if (!range_is_private(from, va, pages))
        return FALSE;
    for (i = 0; i < pages; i++)
        if (aspace_translate(to, va + i * PAGE, NULL, NULL))
            return FALSE;

    for (i = 0; i < pages; i++) {
        PADDR phys;
        UWORD pr, fl;

        aspace_hw_lookup(from->root, va + i * PAGE, &phys, &pr, &fl);
        if (!aspace_hw_map(to->root, va + i * PAGE, phys, prot, ASPACE_LEAF_OWNED,
                           alloc_table_page, to))
            break;
        mapped++;
    }
    if (mapped != pages) {
        /* give back what `to` took: its mappings and the table pages it needed;
         * the pages themselves are still `from`'s */
        for (i = 0; i < mapped; i++)
            aspace_hw_unmap(to->root, va + i * PAGE);
        drop_new_tables(to, first_tables);
        flush_if_current(to);
        return FALSE;
    }
    for (i = 0; i < pages; i++) {
        aspace_hw_unmap(from->root, va + i * PAGE);
        from->nowned--;
        to->nowned++;
    }
    flush_if_current(from);
    flush_if_current(to);
    return TRUE;
}

BOOL aspace_protect_private(ASPACE *as, VADDR va, VADDR bytes, UWORD prot)
{
    VADDR pages, i;

    if ((va & (PAGE - 1)) || !bytes || va >= USER_VA_LIMIT ||
        bytes > USER_VA_LIMIT - va)
        return FALSE;
    pages = (bytes + PAGE - 1) / PAGE;
    if (!range_is_private(as, va, pages))
        return FALSE;
    for (i = 0; i < pages; i++)
        aspace_hw_update(as->root, va + i * PAGE, prot, ASPACE_LEAF_OWNED);
    flush_if_current(as);
    return TRUE;
}

/*
 * Private segment of a program image: fresh zeroed pages with the segment's
 * own permissions, then the file bytes copied in through the window -- which,
 * unlike aspace_copy_to_user(), does not need the page to be user-writable,
 * so a read-only or executable segment can be filled.  The map_private()
 * contract (all-or-nothing, no leftovers) carries over: the copy cannot fail
 * once the pages are mapped.
 */
BOOL aspace_load_private(ASPACE *as, VADDR va, VADDR memsz,
                         UWORD prot, const void *src, ULONG filesz)
{
    const UBYTE *from = src;
    VADDR base = va & ~(PAGE - 1);
    VADDR at = va;

    if (filesz > memsz || !memsz || memsz > USER_VA_LIMIT)
        return FALSE;
    if (!aspace_map_private(as, base, memsz + (va - base), prot))
        return FALSE;
    while (filesz) {
        PADDR phys = 0;
        ULONG chunk = (ULONG)(PAGE - (at & (PAGE - 1)));

        if (chunk > filesz)
            chunk = filesz;
        aspace_translate(as, at, &phys, NULL);
        memcpy(pmem32_virt(phys), from, chunk);
        from += chunk;
        at += chunk;
        filesz -= chunk;
    }
    aspace_hw_code_changed();
    return TRUE;
}

/*
 * Kernel stacks.  A page run from the physical allocator, reached through
 * the window (so the same address in every address space); the stack pointer
 * starts at the top.
 */
PADDR kstack_alloc(uintptr_t *top)
{
    PADDR phys = pmem32_alloc(KSTACK_PAGES, 1);

    if (phys == PMEM32_NONE)
        return 0;
    *top = (uintptr_t)pmem32_virt(phys) + KSTACK_PAGES * PAGE;
    return phys;
}

void kstack_free(PADDR phys)
{
    if (phys)
        pmem32_free(phys, KSTACK_PAGES);
}

/*
 * Supervisor-only access to someone else's process blocks, for exactly as long
 * as the kernel needs it.  Pexec() from a process builds the child's
 * basepage, environment and program under the launcher's page tables, in
 * blocks allocated after that address space exists; each is borrowed into it
 * (writable, no user bit, pinned so it cannot be freed from under the
 * mapping) when it is allocated and returned when it is freed.  Nothing else
 * in the window is reachable.
 */
BOOL aspace_borrow(ASPACE *as, VADDR va, VADDR bytes)
{
    if (aspace_map_procmem(as, va, bytes, ASPACE_PROT_WRITE))
        return TRUE;
    aspace_unborrow(as, va, bytes);      /* a half-finished borrow */
    return FALSE;
}

void aspace_unborrow(ASPACE *as, VADDR va, VADDR bytes)
{
    VADDR page, end;
    ULONG i;

    for (i = as->npins; i-- > 0; )
        if (as->pins[i].va == va && as->pins[i].bytes == bytes) {
            as->pins[i] = as->pins[--as->npins];
            procmem_pin(va, bytes, -1);
            break;
        }
    page = va & ~(PAGE - 1);
    end = (va + bytes + PAGE - 1) & ~(PAGE - 1);
    for (; page < end; page += PAGE)
        aspace_hw_unmap(as->root, page);
    flush_if_current(as);
}

/*
 * Gives the caller access to a block it borrowed: the same pages, now with
 * the user bit and the given permissions (ASPACE_PROT_*), e.g. the basepage and
 * TPA of a child that Pexec(PE_BASEPAGE) is about to hand to a user caller.
 * The block stays pinned and recorded as a borrow, so it goes back exactly as
 * before (aspace_unborrow()).
 */
BOOL aspace_regrant(ASPACE *as, VADDR va, VADDR bytes, UWORD prot)
{
    VADDR page = va & ~(PAGE - 1);
    VADDR end = (va + bytes + PAGE - 1) & ~(PAGE - 1);
    BOOL ok = TRUE;

    for (; page < end; page += PAGE) {
        PADDR phys;
        UWORD pr, fl;

        if (!aspace_hw_lookup(as->root, page, &phys, &pr, &fl) ||
            !aspace_hw_update(as->root, page, prot, 0))
            ok = FALSE;
    }
    flush_if_current(as);
    return ok;
}

/*
 * The address space of the kernel itself, which is what is loaded while no
 * process runs (the boot code, a launcher that is the kernel): the process
 * window, supervisor-only.  The kernel reaches the pages of a process through
 * the physical-memory window whatever address space is loaded.
 */
BOOL aspace_kernel_init(void)
{
    ASPACE *as;
    PADDR window;
    VADDR va;

    if (!aspace_hw_init() || !procmem_window_init(&window))
        return FALSE;
    as = aspace_create();
    if (!as)
        return FALSE;
    for (va = 0; va < PROCMEM_WINDOW_BYTES; va += PAGE)
        if (!aspace_hw_map(as->root, PROCMEM_WINDOW_BASE + va, window + va,
                           ASPACE_PROT_WRITE, 0, alloc_table_page, as))
            return FALSE;
    kernel_root = as->root;
    aspace_hw_switch(kernel_root);
    return TRUE;
}

#endif /* CONF_WITH_USER_ASPACE */
