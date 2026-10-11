/*
 * procmem.h - tracked process memory and address spaces (x86-64)
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef PROCMEM_H
#define PROCMEM_H

#include "portab.h"

#ifdef __x86_64__

/*
 * Process allocations
 * -------------------
 * Everything an x32 process owns in user memory -- its environment, its
 * basepage and TPA -- comes from one low virtual window that lies entirely
 * below 4 GiB, so any address in it fits the 32-bit GEMDOS ABI fields
 * (p_env, p_tbase, p_hitpa, ...).  That is a VIRTUAL constraint, met by
 * construction: procmem_alloc() can only return window addresses.
 * The PHYSICAL pages backing the window are chosen separately and may lie
 * anywhere in RAM, above 4 GiB included, because the MMU maps them;
 * nothing here assumes a low physical address.
 *
 * Each allocation is a whole number of 4 KiB pages and is tracked: the
 * window records which pages are in use, and each live allocation has a
 * record (a kheap block) naming its base, size and owner.  An allocation
 * is released exactly once -- by procmem_free() (Mfree), by
 * procmem_free_owned() (process teardown), or by the caller's
 * failure path -- and a second release is refused and counted, never
 * applied.  These blocks are user memory: they are never kheap blocks, and
 * kfree() rejects them.
 *
 * procmem_alloc() returns NULL, with no side effect, when the
 * window has no free run of pages or the tracking record cannot be
 * allocated.
 */
#define PROCMEM_ZERO    0x0001  /* fill with zeros before returning; without
                                 * it the pages hold whatever a previous
                                 * owner left behind */

void *procmem_alloc(ULONG bytes, UWORD flags);
BOOL procmem_free(void *p);

/* Size in bytes (a whole number of pages) of the live allocation whose
 * base is exactly p; 0 if p is not the base of one. */
ULONG procmem_size(const void *p);

/* Identity of the live allocation whose base is exactly p: a number never
 * given to any other allocation, even one later placed at the same address.
 * 0 if p is not the base of one.  Lets a holder of an address tell "still my
 * block" from "someone else's block that reused the space". */
UQUAD procmem_gen(const void *p);

/*
 * For tests that make memory permanent on purpose (Ptermres): the generation
 * the next allocation will get, and a release of every unpinned allocation
 * with no owner made at or after a generation, so the test can give back what
 * it made resident.  Returns how many blocks it freed.  Nothing else should
 * use this: ownerless memory is permanent by design.
 */
UQUAD procmem_next_gen(void);
ULONG procmem_free_ownerless_since(UQUAD gen);

/* TRUE iff p lies in the window at all (live or not). */
BOOL procmem_contains(const void *p);

/* TRUE iff every page of [va, va + bytes) belongs to a live allocation. */
BOOL procmem_range_live(UQUAD va, UQUAD bytes);

/*
 * A block mapped into a live address space (aspace_map_procmem()) is
 * pinned: it cannot be released while the mapping exists, because the pages
 * would be handed to a new owner while the old process could still reach
 * them.  procmem_free() returns FALSE for a pinned block and leaves
 * it allocated; procmem_free_owned() skips it.  The address space
 * unpins when it is destroyed.
 */
BOOL procmem_pinned(const void *p);
ULONG procmem_pins(const void *p);      /* the pin count itself */
void procmem_pin(UQUAD va, UQUAD bytes, int delta);   /* +1 / -1 */

/* Physical address backing window address va (which must be inside it). */
UQUAD procmem_phys_of(UQUAD va);

/*
 * Ownership, mirroring the GEMDOS memory descriptor owner: set_owner()
 * names the process that dies with the block, NULL (the initial state)
 * means permanent.  free_owned() releases every block owned by `owner`,
 * calling pre_free(base) first for each (so the caller can drop anything
 * keyed by that address, such as a KPROC record).  keep() is Ptermres: the
 * owner's blocks become permanent, and the owner's own block -- the one
 * starting at the address `owner` -- is cut back to keep_bytes (rounded up
 * to whole pages, 0 meaning "all of it") when no live address space maps
 * it; every other owned block is passed to pre_keep(base) first, for the
 * same kind of per-address cleanup.  `owner` is only an identity, never
 * dereferenced.
 */
void procmem_set_owner(const void *p, const void *owner);
const void *procmem_owner(const void *p);   /* NULL: none, or not an allocation */
void procmem_free_owned(const void *owner, void (*pre_free)(void *base));
void procmem_keep(const void *owner, ULONG keep_bytes, void (*pre_keep)(void *base));

typedef struct {
    ULONG live_allocs;      /* allocations not yet freed */
    ULONG total_pages;      /* size of the window */
    ULONG free_pages;       /* pages not in any allocation */
    ULONG bad_frees;        /* rejected releases (double/foreign free) */
} PROCMEM_STATS;

void procmem_stats(PROCMEM_STATS *stats);

/*
 * Address spaces
 * --------------
 * One ring-3 process's page tables.  The object owns the PML4 and every
 * page-table page built under it.  Of the leaf pages it maps, two kinds are
 * distinguished: private pages (aspace_map_private()) are allocated
 * for it, owned by it and freed with it, while borrowed pages -- procmem
 * blocks (aspace_map_procmem(), pinned while mapped) and
 * device/test pages (aspace_map_page()) -- belong to someone else
 * and are never freed with the address space.  Creation and mapping can fail
 * for lack of memory; destruction cannot, and frees each table page and each
 * private page exactly once.
 *
 * The kernel half (PML4 slots 256-511) is shared.  The low half starts with
 * the kernel's own low data, all of it supervisor-only (no user bit): the
 * system-vector area, the kernel-data pool and the framebuffer (see
 * doc/x86_64-address-space.txt, "Kernel low mappings"), because ring 0 runs
 * under a process's page tables while it services that process's system
 * calls.  The process window is empty except for what is mapped for the
 * process: its own blocks (user) and the blocks of a child it is launching
 * (borrowed, supervisor-only).
 */
typedef struct x86_64_aspace ASPACE;

/* Explicit leaf permissions.  Execution is denied unless PROT_EXEC is given
 * (NX), and the page is reachable from ring 3 only with PROT_USER. */
#define ASPACE_PROT_WRITE   0x1
#define ASPACE_PROT_EXEC    0x2
#define ASPACE_PROT_USER    0x4

/* The size of a page of a process's memory (the heap is a whole number of them). */
#define USER_PAGE_SIZE 0x1000ULL

/* The 32-bit ABI limit: no user mapping may reach or pass this address. */
#define USER_VA_LIMIT 0x100000000ULL

/*
 * The minimum user virtual layout for the built-in x32 EmuCON image and its
 * process data (doc/x86_64-address-space.txt).  Every process has its own
 * page tables, so each can use these same addresses for its own private
 * pages.
 *
 *   0x00000000 - 0x001fffff   unmapped (null guard, 2 MiB)
 *   0x00200000 - 0x003fffff   basepage, environment and TPA blocks
 *                             (procmem, X86_64_LOW_TPA_*; shared kernel view)
 *   0x00400000 - 0x007fffff   program image: text, data, bss (4 MiB), then,
 *                             for a program loaded from a file, the startup
 *                             area USER_IMAGE_SLACK long after it
 *   0x0fff0000 - 0x0fff0fff   ancestors' basepages, read-only (see below)
 *   0x10000000 - 0x7efeffff   Malloc() memory of a process, backed by private
 *                             pages as they are first touched (see below)
 *   0x7efff000 - 0x7effffff   guard page: neither heap nor stack, never mapped
 *   0x7f000000 - 0x7fffffff   user stack (16 MiB by default), growing down,
 *                             likewise; Psetlimit() can make it bigger, and
 *                             the Malloc() memory then ends lower
 *
 * Everything stays below 0x80000000, so that every address of a process is a
 * positive signed long (the old TOS habits of sign-extending pointers and of
 * returning negative GEMDOS error codes in the same register as addresses).
 *
 * The heap and the stack are lazy: Malloc() hands out address space, and a page
 * of it gets its (zeroed) physical page when the process first touches it, in
 * the page fault handler (aspace_fault_in()).  Untouched address space
 * costs nothing, so the sum of what the processes may use is more than there is
 * physical memory (overcommit); the process that touches a page when none is
 * left is terminated, as Pterm(-1).  Malloc(-1) reports the largest free
 * stretch of address space, as the TPA size does on the other platforms.
 *
 * The ancestors page holds scrubbed copies of the basepages of the process's
 * launcher, that one's launcher and so on (USER_ANCESTORS levels at
 * most), 256 bytes each, chained through their p_parent fields (the last one's
 * is 0).  The process's own p_parent points at the first.  Every copy's p_env
 * is the process's own environment.
 */
#define USER_ANCESTORS_VA 0x0fff0000ULL
#define USER_ANCESTORS    16
#define USER_IMAGE_BASE  0x00400000ULL
#define USER_IMAGE_SIZE  0x00400000ULL
/* Zeroed read/write memory mapped right after the last page of a program
 * loaded from a file: where the C startup code of the TOS tradition puts the
 * argv and environ arrays it builds (libcmini's parseargs() uses the space
 * after the bss).  Sized for the worst a ring-3 caller may hand over: an
 * environment of 32766 bytes (bdos/uaccess.c) is at most 16383 strings, each
 * with a 4-byte environ slot (64 KiB), and the command line, its copy and
 * argv add a few hundred bytes. */
#define USER_IMAGE_SLACK 0x00020000ULL
#define USER_HEAP_BASE   0x10000000ULL
/* (the end of the heap with the default stack, a guard page below the stack's
 * first; a process whose stack limit was raised has less) */
#define USER_HEAP_LIMIT  0x7efff000ULL
#define USER_STACK_TOP   0x80000000ULL
/* the default size of the stack: address space, backed when touched */
#define USER_STACK_SIZE  0x01000000ULL
#define USER_STACK_MIN   0x00004000ULL   /* the least Psetlimit() allows */

ASPACE *aspace_create(void);
void aspace_destroy(ASPACE *as);
/* Makes `as` the address space in use (returns the one that was, a CR3 value for
 * aspace_leave()): ring 0 can then address its user pages by their user
 * addresses, as it does under a process's own system calls.  Nothing in the
 * kernel may rely on the previous address space's user half meanwhile. */
UQUAD aspace_enter(ASPACE *as);
void aspace_leave(UQUAD previous);
UQUAD aspace_root(const ASPACE *as);

/*
 * Maps one 4 KiB page: user virtual address va (page-aligned, and the
 * whole page below USER_VA_LIMIT) to the physical page phys
 * (page-aligned; ANY physical address, including above 4 GiB).  FALSE if
 * a constraint is violated or table memory ran out; the page is then not
 * mapped.
 */
BOOL aspace_map_page(ASPACE *as, UQUAD va, UQUAD phys, UWORD prot);

/*
 * Maps [va, va + bytes) -- rounded out to whole pages -- of live procmem
 * allocations at the same addresses.  FALSE if any page is not live, a
 * constraint is violated or memory ran out; pages mapped before the
 * failure stay in the address space and go away with it.
 */
BOOL aspace_map_procmem(ASPACE *as, UQUAD va, UQUAD bytes, UWORD prot);

/*
 * Borrowing: maps [va, va + bytes) of live procmem blocks into `as`
 * supervisor-only and writable (pinned), so ring 0 can build a child process
 * under this address space during a ring-3 Pexec(); aspace_unborrow()
 * takes it away again (and unpins), exactly once per borrow.  See aspace.c.
 */
BOOL aspace_borrow(ASPACE *as, UQUAD va, UQUAD bytes);
void aspace_unborrow(ASPACE *as, UQUAD va, UQUAD bytes);
/* Re-maps an already borrowed block with other permissions (the user bit
 * included): the block a ring-3 caller is handed. Still released by unborrow. */
BOOL aspace_regrant(ASPACE *as, UQUAD va, UQUAD bytes, UWORD prot);

/* Number of table pages (PML4 included) the address space currently owns. */
ULONG aspace_table_pages(const ASPACE *as);

/*
 * Private memory.  Maps [va, va + bytes) -- va page-aligned, bytes rounded
 * up to whole pages, the whole range below USER_VA_LIMIT -- to fresh,
 * zeroed backing pages that belong to this address space alone: another
 * address space can map the very same virtual addresses to its own pages,
 * and neither sees the other's writes.  The pages are owned by `as` and
 * freed with it, exactly once.  Fails (FALSE, nothing mapped or allocated
 * left behind) if any page of the range is already mapped, a constraint is
 * violated or memory runs out.  "Nothing left behind" means no mapping,
 * backing page or page-table page: the address space's internal bookkeeping
 * vectors may keep extra capacity until it is destroyed.
 */
BOOL aspace_map_private(ASPACE *as, UQUAD va, UQUAD bytes, UWORD prot);

/*
 * Gives back [va, va + bytes) -- va page-aligned, bytes rounded up to whole
 * pages -- of private memory: unmaps it and frees the backing pages, which
 * must all have come from aspace_map_private() of this address space.
 * FALSE, with nothing changed, if any page of the range is not such a page.
 * The page-table pages that held the mappings stay until the address space
 * is destroyed.
 */
BOOL aspace_unmap_private(ASPACE *as, UQUAD va, UQUAD bytes);

/*
 * Lazy memory.  aspace_fault_in() backs the page containing `va` with a
 * new zeroed private page, with `prot`, if there is no mapping there yet (TRUE
 * also if there is one already); FALSE if a physical page or a page table
 * could not be had, or less than ASPACE_FAULT_RESERVE of physical memory would
 * be left (the kernel's own needs come first: the reserve is never dipped into,
 * the tables the page may need counted).  aspace_map_private() keeps the
 * same reserve, for the pages of a program image and the like.  The caller has decided that the
 * address belongs to a lazy area.  aspace_release_lazy() is
 * aspace_unmap_private() for such an area: pages never touched are
 * simply not there, and are skipped; FALSE, with nothing changed, if a page
 * that is there is not one of its private pages.
 */
#define ASPACE_FAULT_RESERVE 0x00400000ULL
#define ASPACE_FAULT_COST    0x00008000ULL      /* the most one page costs: it, 3 tables, bookkeeping */
BOOL aspace_fault_in(ASPACE *as, UQUAD va, UWORD prot);
BOOL aspace_release_lazy(ASPACE *as, UQUAD va, UQUAD bytes);

/*
 * Hands private pages from one address space to another: [va, va + bytes) of
 * `from` (every page must be one of its private pages) is mapped at the same
 * addresses in `to` with `prot`, and unmapped in `from`, the pages themselves
 * staying allocated and now owned by `to`.  FALSE, with nothing changed, if
 * a page is not private to `from`, the range is mapped in `to` already or
 * memory ran out.
 */
BOOL aspace_move_private(ASPACE *from, ASPACE *to,
                                UQUAD va, UQUAD bytes, UWORD prot);

/*
 * Changes the permissions of private pages [va, va + bytes) of `as` (the
 * page and its ownership stay).  FALSE, with nothing changed, if a page is
 * not one of its private pages.
 */
BOOL aspace_protect_private(ASPACE *as, UQUAD va, UQUAD bytes, UWORD prot);

/*
 * Software walk of the address space's own page tables, through the physical
 * direct map (so it works for any address space, loaded or not, and can
 * never fault).  Reports the physical page va maps to and the effective
 * permissions -- the AND of every level, as the MMU computes them.  FALSE if
 * va is not mapped.  A kernel-half address reports prot without
 * ASPACE_PROT_USER: supervisor-only mappings are visible to the walk but not
 * to ring 3.
 */
BOOL aspace_translate(const ASPACE *as, UQUAD va, UQUAD *phys, UWORD *prot);

/*
 * True iff every page of [va, va + bytes) is mapped in this address space,
 * reachable from ring 3 and -- if `write` -- writable.  This, not a numeric
 * address range, is what makes a user pointer valid for this process.
 */
BOOL aspace_user_range_ok(const ASPACE *as, UQUAD va, UQUAD bytes, BOOL write);

/*
 * Kernel copies to and from a process's memory through the direct map, after
 * the same check; they work whichever address space is loaded and cannot
 * fault.  FALSE, with nothing copied, if the range is not valid or any page
 * of it is not RAM the physical allocator owns (a device or bogus physical
 * page mapped with aspace_map_page() is valid for ring 3 but has no
 * safe direct-map alias; it needs a separate access mechanism).
 */
BOOL aspace_copy_from_user(const ASPACE *as, void *dst, UQUAD va, ULONG bytes);
BOOL aspace_copy_to_user(const ASPACE *as, UQUAD va, const void *src, ULONG bytes);

/*
 * aspace_map_private() of the pages covering [va, va + memsz) -- va
 * need not be page-aligned -- with the first filesz bytes at va then filled
 * from src, whatever the permissions (a read-only text segment included);
 * everything else stays zero.  filesz > memsz is refused.  Same
 * all-or-nothing contract as map_private().
 */
BOOL aspace_load_private(ASPACE *as, UQUAD va, UQUAD memsz,
                                UWORD prot, const void *src, ULONG filesz);

/*
 * Per-process kernel stacks: the stack a ring-3 process's system calls run
 * on (the syscall entry stub switches to it), so that a process suspended
 * inside a call -- Pexec() of a child -- keeps its frames while the child
 * makes calls of its own.  x86_64_kstack_alloc() returns the physical base
 * (0 if memory ran out) and the initial stack pointer; the stack is freed
 * with x86_64_kstack_free(), never while it is still in use.
 */
#define X86_64_KSTACK_PAGES 4
UQUAD x86_64_kstack_alloc(UQUAD *top);
void x86_64_kstack_free(UQUAD phys);

/* Private backing pages the address space owns (diagnostics, tests). */
ULONG aspace_private_pages(const ASPACE *as);

#endif /* __x86_64__ */

#endif /* PROCMEM_H */
