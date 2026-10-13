/*
 * mmu_shortdesc.c - VMSAv6/v7 short-descriptor backend for the generic
 * pMMU walker (bios/mmu_walk.c)
 *
 * Design: docs/superpowers/specs/2026-08-15-portable-pmmu-page-table-design.md
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "mmu_shortdesc.h"
#include "raspi_mmu.h"
#include "processor.h"
#include "asm.h"

const struct mmu_level_desc mmu_levels[MMU_ARCH_LEVELS] = {
    /* root: 1 MiB sections, or a table descriptor to a page table */
    { 20, 4096, 0x4000UL, 0x4000UL, TRUE, SECTION_SIZE },
    /* page: 4 KiB small pages (coarse page table, 256 entries) */
    { 12, ARMV6MMU_LEVEL2_COARSE_PAGE_TABLE_SIZE / 4UL,
      ARMV6MMU_LEVEL2_COARSE_PAGE_TABLE_SIZE,
      ARMV6MMU_LEVEL2_COARSE_PAGE_TABLE_SIZE, TRUE, SMALL_PAGE_SIZE }
};

/*
 * Root (level 0) descriptor format bits [1:0]: 00 fault, 01 table
 * (coarse page table), 10 section.  Field positions below mirror
 * bios/raspi_mmu.h's TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR and
 * TARMV6MMU_LEVEL1_COARSE_PAGE_TABLE_DESCRIPTOR; raw words are used
 * here instead of those packed structs so one pair of functions can be
 * shared across both formats, keyed on the format bits every short
 * descriptor starts with.
 */
#define L0_FMT_MASK          0x00000003UL
#define L0_FMT_TABLE         0x00000001UL
#define L0_FMT_SECTION       0x00000002UL
#define L0_SECTION_B         (1UL << 2)
#define L0_SECTION_C         (1UL << 3)
#define L0_SECTION_XN        (1UL << 4)
#define L0_SECTION_AP_SHIFT  10
#define L0_SECTION_TEX_SHIFT 12
#define L0_SECTION_APX       (1UL << 15)
#define L0_SECTION_S         (1UL << 16)
#define L0_SECTION_NG        (1UL << 17)
#define L0_SECTION_BASE_MASK 0xfff00000UL
#define L0_TABLE_BASE_MASK   0xfffffc00UL

/*
 * Page (level 1) descriptor format bits [1:0]: 00 fault, 1x small page
 * (bit 0 is XN for the small-page format).  Mirrors
 * TARMV6MMU_LEVEL2_EXT_SMALL_PAGE_DESCRIPTOR.
 */
#define L1_FMT_SMALLPAGE     0x00000002UL
#define L1_SMALLPAGE_XN      (1UL << 0)
#define L1_SMALLPAGE_B       (1UL << 2)
#define L1_SMALLPAGE_C       (1UL << 3)
#define L1_SMALLPAGE_AP_SHIFT  4
#define L1_SMALLPAGE_TEX_SHIFT 6
#define L1_SMALLPAGE_APX     (1UL << 9)
#define L1_SMALLPAGE_S       (1UL << 10)
#define L1_SMALLPAGE_NG      (1UL << 11)
#define L1_SMALLPAGE_BASE_MASK 0xfffff000UL

static ULONG mmu_shortdesc_read(const void *table, unsigned index)
{
    return ((const ULONG *)table)[index];
}

static void mmu_shortdesc_write(void *table, unsigned index, ULONG value)
{
    ((ULONG *)table)[index] = value;
}

BOOL mmu_backend_attrs_supported(mmu_attr_type attrs)
{
    /* Every attribute this design defines has a representation on this
     * backend (MMU_ARCH_HAS_EXEC / MMU_ARCH_HAS_USER are both set); the
     * walker's own generic checks already reject malformed combinations
     * (e.g. no memory-type bit set) before this is ever consulted. */
    UNUSED(attrs);
    return TRUE;
}

BOOL mmu_backend_entry_valid(unsigned level, const void *table, unsigned index)
{
    ULONG e = mmu_shortdesc_read(table, index);

    if (level == MMU_LEVEL_ROOT)
        return (e & L0_FMT_MASK) != 0;
    return (e & L1_FMT_SMALLPAGE) != 0;
}

BOOL mmu_backend_entry_is_table(unsigned level, const void *table, unsigned index)
{
    ULONG e = mmu_shortdesc_read(table, index);

    if (level != MMU_LEVEL_ROOT)
        return FALSE;   /* the page level is always the last level */
    return (e & L0_FMT_MASK) == L0_FMT_TABLE;
}

phys_addr_type mmu_backend_entry_child_phys(unsigned level, const void *table, unsigned index)
{
    ULONG e = mmu_shortdesc_read(table, index);

    UNUSED(level);
    return (phys_addr_type)(e & L0_TABLE_BASE_MASK);
}

phys_addr_type mmu_backend_entry_leaf_phys(unsigned level, const void *table, unsigned index)
{
    ULONG e = mmu_shortdesc_read(table, index);

    if (level == MMU_LEVEL_ROOT)
        return (phys_addr_type)(e & L0_SECTION_BASE_MASK);
    return (phys_addr_type)(e & L1_SMALLPAGE_BASE_MASK);
}

mmu_attr_type mmu_backend_entry_leaf_attrs(unsigned level, const void *table, unsigned index)
{
    ULONG e = mmu_shortdesc_read(table, index);
    mmu_attr_type attrs = 0;
    ULONG tex, b, c, ap, apx;
    BOOL xn, s, ng;

    if (level == MMU_LEVEL_ROOT) {
        xn  = (e & L0_SECTION_XN) != 0;
        b   = (e & L0_SECTION_B) != 0;
        c   = (e & L0_SECTION_C) != 0;
        tex = (e >> L0_SECTION_TEX_SHIFT) & 0x7UL;
        ap  = (e >> L0_SECTION_AP_SHIFT) & 0x3UL;
        apx = (e & L0_SECTION_APX) != 0;
        s   = (e & L0_SECTION_S) != 0;
        ng  = (e & L0_SECTION_NG) != 0;
    } else {
        xn  = (e & L1_SMALLPAGE_XN) != 0;
        b   = (e & L1_SMALLPAGE_B) != 0;
        c   = (e & L1_SMALLPAGE_C) != 0;
        tex = (e >> L1_SMALLPAGE_TEX_SHIFT) & 0x7UL;
        ap  = (e >> L1_SMALLPAGE_AP_SHIFT) & 0x3UL;
        apx = (e & L1_SMALLPAGE_APX) != 0;
        s   = (e & L1_SMALLPAGE_S) != 0;
        ng  = (e & L1_SMALLPAGE_NG) != 0;
    }

    /* TEX=0 memory-type remap: B,C jointly select strongly ordered,
     * shared device, write-through or write-back -- see
     * bios/machine/virt-arm/virt_mmu.c's comment on the same encoding. */
    if (tex == 0 && !c && !b)
        attrs |= MMU_ATTR_STRONGLY_ORDERED;
    else if (tex == 0 && !c && b)
        attrs |= MMU_ATTR_DEVICE;
    else if (tex == 0 && c && !b)
        attrs |= MMU_ATTR_WRITE_THROUGH;
    else
        attrs |= MMU_ATTR_WRITE_BACK;

    /* Inverse of mmu_backend_install_leaf_entry()'s ap/apx encoding:
     * ap is AP_ALL_ACCESS for a user-accessible mapping, AP_SYSTEM_ACCESS
     * otherwise; apx (the raw APX bit) equals APX_RO_ACCESS (1) for a
     * read-only mapping and APX_RW_ACCESS (0) for a writable one. */
    attrs |= MMU_ATTR_READ;
    if (!apx)
        attrs |= MMU_ATTR_WRITE;
    if (ap == AP_ALL_ACCESS || ap == AP_USER_RO_ACCESS)
        attrs |= MMU_ATTR_USER;
    if (!xn)
        attrs |= MMU_ATTR_EXEC;
    if (!ng)
        attrs |= MMU_ATTR_GLOBAL;
    if (s)
        attrs |= MMU_ATTR_SHAREABLE;

    return attrs;
}

void mmu_backend_clear_entry(unsigned level, void *table, unsigned index)
{
    UNUSED(level);
    mmu_shortdesc_write(table, index, 0);
}

void mmu_backend_install_table_entry(unsigned level, void *table, unsigned index,
                                     phys_addr_type child_phys)
{
    UNUSED(level);
    mmu_shortdesc_write(table, index,
        ((ULONG)child_phys & L0_TABLE_BASE_MASK) | L0_FMT_TABLE);
}

void mmu_backend_install_leaf_entry(unsigned level, void *table, unsigned index,
                                    phys_addr_type pa, mmu_attr_type attrs)
{
    ULONG e;
    BOOL want_write = (attrs & MMU_ATTR_WRITE) != 0;
    BOOL want_user  = (attrs & MMU_ATTR_USER) != 0;
    ULONG ap  = want_user ? AP_ALL_ACCESS : AP_SYSTEM_ACCESS;
    ULONG apx = want_write ? APX_RW_ACCESS : APX_RO_ACCESS;
    ULONG tex = 0;
    ULONG b, c;

    if (attrs & MMU_ATTR_STRONGLY_ORDERED) {
        b = 0; c = 0;
    } else if (attrs & MMU_ATTR_DEVICE) {
        b = 1; c = 0;
    } else if (attrs & MMU_ATTR_WRITE_THROUGH) {
        b = 0; c = 1;
    } else {
        b = 1; c = 1; /* MMU_ATTR_WRITE_BACK */
    }

    if (level == MMU_LEVEL_ROOT) {
        e = L0_FMT_SECTION | ((ULONG)pa & L0_SECTION_BASE_MASK);
        if (b) e |= L0_SECTION_B;
        if (c) e |= L0_SECTION_C;
        if (!(attrs & MMU_ATTR_EXEC)) e |= L0_SECTION_XN;
        if (apx) e |= L0_SECTION_APX;
        if (attrs & MMU_ATTR_SHAREABLE) e |= L0_SECTION_S;
        if (!(attrs & MMU_ATTR_GLOBAL)) e |= L0_SECTION_NG;
        e |= ap << L0_SECTION_AP_SHIFT;
        e |= tex << L0_SECTION_TEX_SHIFT;
    } else {
        e = L1_FMT_SMALLPAGE | ((ULONG)pa & L1_SMALLPAGE_BASE_MASK);
        if (b) e |= L1_SMALLPAGE_B;
        if (c) e |= L1_SMALLPAGE_C;
        if (!(attrs & MMU_ATTR_EXEC)) e |= L1_SMALLPAGE_XN;
        if (apx) e |= L1_SMALLPAGE_APX;
        if (attrs & MMU_ATTR_SHAREABLE) e |= L1_SMALLPAGE_S;
        if (!(attrs & MMU_ATTR_GLOBAL)) e |= L1_SMALLPAGE_NG;
        e |= ap << L1_SMALLPAGE_AP_SHIFT;
        e |= tex << L1_SMALLPAGE_TEX_SHIFT;
    }

    mmu_shortdesc_write(table, index, e);
}

void mmu_backend_sync(void *start, size_t len)
{
    /* This short-descriptor format is common to ARMv6 (RPi1) and ARMv7+
     * (RPi2/3/4, virt-arm), so this sticks to cache/TLB operations
     * available on both, rather than bios/arch/arm/cache_armv7.c's
     * ARMv7-only, range-based mmu_page_table_flush(): a portable
     * whole-cache flush plus the CP15 TLBIALL encoding common to both
     * architecture versions. start/len are unused as a result -- see
     * the design doc's Synchronization section on this being an
     * accepted Phase 1 simplification, not a bug. */
    UNUSED(start);
    UNUSED(len);
    flush_data_cache_all();
    asm volatile ("mcr p15, 0, %0, c8, c7, 0" : : "r" (0));
    data_sync_barrier();
    flush_prefetch_buffer();
}
