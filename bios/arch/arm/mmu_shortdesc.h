/*
 * mmu_shortdesc.h - VMSAv6/v7 short-descriptor backend contract for the
 * generic pMMU walker (bios/mmu_walk.c)
 *
 * Design: docs/superpowers/specs/2026-08-15-portable-pmmu-page-table-design.md
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef MMU_SHORTDESC_H
#define MMU_SHORTDESC_H

#include "mmu.h"

/*
 * Two levels: root (1 MiB sections, or a table descriptor pointing at a
 * page table) and page (4 KiB small pages).  Both levels are leaf
 * capable; the page level is also the last level, so an entry reached
 * there is always a leaf.
 */
#define MMU_ARCH_LEVELS   2
#define MMU_LEVEL_ROOT    0
#define MMU_LEVEL_PAGE    1

/* Every attribute this design defines is representable on this backend. */
#define MMU_ARCH_HAS_EXEC 1
#define MMU_ARCH_HAS_USER 1

struct mmu_level_desc {
    unsigned shift;        /* VA bit shift selecting this level's index    */
    size_t   entries;      /* entries in a table at this level             */
    size_t   table_size;   /* size in bytes of a table at this level       */
    size_t   table_align;  /* required alignment of a table at this level  */
    BOOL     leaf_capable; /* may an entry at this level be a leaf?        */
    size_t   granule;      /* bytes covered by one entry at this level     */
};

extern const struct mmu_level_desc mmu_levels[MMU_ARCH_LEVELS];

/* Whether attrs is representable at all on this backend (capability gate,
 * independent of the generic malformed-input checks the walker already
 * does itself). */
BOOL mmu_backend_attrs_supported(mmu_attr_type attrs);

/* Decode/encode a raw 32-bit descriptor word.  "table" points at the
 * table containing the entry; "index" selects the entry within it. */
BOOL mmu_backend_entry_valid(unsigned level, const void *table, unsigned index);
BOOL mmu_backend_entry_is_table(unsigned level, const void *table, unsigned index);
phys_addr_type mmu_backend_entry_child_phys(unsigned level, const void *table, unsigned index);
phys_addr_type mmu_backend_entry_leaf_phys(unsigned level, const void *table, unsigned index);
mmu_attr_type  mmu_backend_entry_leaf_attrs(unsigned level, const void *table, unsigned index);

void mmu_backend_clear_entry(unsigned level, void *table, unsigned index);
void mmu_backend_install_table_entry(unsigned level, void *table, unsigned index,
                                     phys_addr_type child_phys);
void mmu_backend_install_leaf_entry(unsigned level, void *table, unsigned index,
                                    phys_addr_type pa, mmu_attr_type attrs);

/*
 * Page-table memory visibility plus TLB/instruction synchronization for
 * the byte range [start, start+len) of table memory that just changed.
 * May be called more than once per range API call (once per contiguous
 * table region touched); each call is self-contained (clean+invalidate
 * of that range, full TLB invalidate) via the existing
 * mmu_page_table_flush(), so calling it a bounded, small number of times
 * per call is a deliberate Phase 1 simplification, not a bug -- see the
 * design doc's Synchronization section and Risks.
 */
void mmu_backend_sync(void *start, size_t len);

#endif /* MMU_SHORTDESC_H */
