/*
 * mmu.h - portable pMMU page-table maintenance abstraction
 *
 * Design: docs/superpowers/specs/2026-08-15-portable-pmmu-page-table-design.md
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef MMU_H
#define MMU_H

#include "portab.h"

/*
 * Address types.
 *
 * virt_addr_type is pointer-sized: it tracks the target's VA/pointer
 * width (32 bits everywhere pTOS runs today).  phys_addr_type is sized
 * independently, via CONF_MMU_PHYS_64, using standard fixed-width types
 * rather than the inherited ULONG/UQUAD aliases -- this is new
 * architecture-neutral infrastructure with no TOS/GEM ABI meaning to
 * preserve.
 *
 * A phys_addr_type is never safe to treat as a C pointer, even on a
 * target where identity mapping currently makes the numeric values
 * coincide.  Converting one to a pointer is only valid at a boundary
 * that has separately established the target is actually identity-mapped
 * there, and must be an explicit, narrow, commented cast -- never
 * implicit.
 */
typedef uintptr_t virt_addr_type;

#if CONF_MMU_PHYS_64
typedef uint64_t phys_addr_type;
#else
typedef uint32_t phys_addr_type;
#endif

/*
 * Attribute bitmask, cross-architecture.  Each backend maps these to its
 * own hardware bits; see MMU_ARCH_HAS_* capability defines in the backend
 * header for what a given backend can represent.
 */
typedef uint32_t mmu_attr_type;

/* memory type -- mutually exclusive, strictest to least strict */
#define MMU_ATTR_STRONGLY_ORDERED  ((mmu_attr_type)0x00000001UL)
#define MMU_ATTR_DEVICE            ((mmu_attr_type)0x00000002UL)
#define MMU_ATTR_WRITE_THROUGH     ((mmu_attr_type)0x00000004UL)
#define MMU_ATTR_WRITE_BACK        ((mmu_attr_type)0x00000008UL)

/* permissions */
#define MMU_ATTR_READ              ((mmu_attr_type)0x00000010UL)
#define MMU_ATTR_WRITE             ((mmu_attr_type)0x00000020UL)
#define MMU_ATTR_EXEC              ((mmu_attr_type)0x00000040UL)
#define MMU_ATTR_USER              ((mmu_attr_type)0x00000080UL)

/* misc */
#define MMU_ATTR_GLOBAL            ((mmu_attr_type)0x00000100UL)
#define MMU_ATTR_SHAREABLE         ((mmu_attr_type)0x00000200UL)

/*
 * Error codes.  MMU_ERR_OVERLAP and MMU_ERR_NOTMAPPED are the two sides
 * of the same rule: the layer never guesses at partial ranges, whether
 * the surprise is an existing mapping (mmu_map_range()) or a hole
 * (mmu_unmap_range()/mmu_protect_range()).
 */
#define MMU_OK                0
#define MMU_ERR_NOMEM         1   /* table or registry slot allocation failed */
#define MMU_ERR_INVAL         2   /* misaligned va/pa, zero-length size, or malformed attrs */
#define MMU_ERR_OVERLAP       3   /* mmu_map_range(): part of the range is already mapped */
#define MMU_ERR_NOTMAPPED     4   /* mmu_unmap_range()/mmu_protect_range(): part of the range is not mapped */
#define MMU_ERR_UNSUPPORTED   5   /* a requested attribute is not representable on this backend */

/*
 * A table the walker can write through (virt) and encode into a parent
 * descriptor (phys).  Filled in by mmu_table_alloc_type on allocation, and
 * by machine code before mmu_table_adopt() for a pre-existing table.
 */
struct mmu_table_ref {
    void *virt;             /* address software uses to write the table     */
    phys_addr_type phys;    /* address to encode into the parent descriptor */
};

/*
 * Table allocator contract.  "level" and the geometry-derived size/align
 * tell the allocator what it's being asked for; it fills in *table on
 * success.  mmu_table_free_type is the inverse, and -- per the table
 * registry described in the design doc -- is only ever called on tables
 * this layer allocated itself.
 */
typedef int  (*mmu_table_alloc_type)(unsigned level, size_t size, size_t align,
                                     struct mmu_table_ref *table, void *cookie);
typedef void (*mmu_table_free_type)(const struct mmu_table_ref *table,
                                    unsigned level, void *cookie);

/*
 * Installs the allocator machine code uses for new intermediate tables.
 * Must be called before the first mmu_map_range()/mmu_protect_range()/
 * mmu_unmap_range() call that could need to grow or split the tree.
 */
void mmu_set_table_allocator(mmu_table_alloc_type alloc,
                             mmu_table_free_type free, void *cookie);

/*
 * Registers a table that was not allocated through mmu_table_alloc_type
 * (a pre-existing/static table spliced directly into the tree below
 * root) so the walker can resolve and edit it. The table is never freed
 * by mmu_unmap_range(), however empty it becomes.
 */
int  mmu_table_adopt(const struct mmu_table_ref *table, unsigned level);

/*
 * Range API.  root is an opaque pointer to the root table, always
 * caller-owned; the layer only ever edits its contents.
 *
 * va must be aligned to the backend's smallest page size; pa (for
 * mmu_map_range()) must likewise be aligned to the smallest page size.
 * size may be any positive length and is rounded up internally to a
 * whole number of smallest pages; size == 0 is rejected with
 * MMU_ERR_INVAL.
 *
 * Each call runs as a validate+reserve pass followed by a commit pass:
 * on failure the address space is left exactly as it was before the
 * call, including any leaf demotion the call would have performed.
 */
int  mmu_map_range(void *root, virt_addr_type va, phys_addr_type pa,
                   size_t size, mmu_attr_type attrs);
int  mmu_unmap_range(void *root, virt_addr_type va, size_t size);
int  mmu_protect_range(void *root, virt_addr_type va, size_t size,
                       mmu_attr_type attrs);

/*
 * Coarse synchronization for boundary cases outside the range API's own
 * bookkeeping -- e.g. right after a statically-built table is handed to
 * this layer, or after mmu_table_adopt() of a pre-existing table.  Not
 * needed after an ordinary successful mmu_map_range()/mmu_unmap_range()/
 * mmu_protect_range() call, which already synchronizes internally.
 */
void mmu_sync_all(void *root);

#endif /* MMU_H */
