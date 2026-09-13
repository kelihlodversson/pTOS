/*
 * virt_mmu_selftest.c - boot-time functional validation of the portable
 * pMMU page-table maintenance abstraction, run against the table
 * virt_mmu_bootstrap() already built for QEMU's ARM 'virt' machine.
 *
 * See docs/superpowers/specs/2026-08-15-portable-pmmu-page-table-design.md's
 * Testing And Validation section for the sequence this follows.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#if CONF_DEBUG_MMU_MAINT_SELFTEST

#include "portab.h"
#include "mmu.h"
#include "mmu_shortdesc.h"
#include "raspi_mmu.h"
#include "virt_memmap.h"
#include "virt_mmu.h"
#include "kprint.h"
#include "biosext.h"

/*
 * A dedicated, section-sized, section-aligned scratch area: this is
 * what makes it safe to demote, unmap and fully remap for this test
 * without disturbing anything else. It is real RAM (unlike arbitrary
 * "unused" device MMIO space elsewhere in virt_mmu_bootstrap()'s map --
 * QEMU's virt machine actively rejects a load from a truly unassigned
 * physical address, it does not just read as zero), and its size and
 * alignment guarantee no other kernel data shares its section, so nothing
 * else is affected when the whole section is torn down and rebuilt.
 */
static UBYTE mmu_selftest_section[SECTION_SIZE]
    __attribute__((aligned(SECTION_SIZE)));

#define MMU_SELFTEST_POOL_TABLES  2

static UBYTE mmu_selftest_pool[MMU_SELFTEST_POOL_TABLES][ARMV6MMU_LEVEL2_COARSE_PAGE_TABLE_SIZE]
    __attribute__((aligned(ARMV6MMU_LEVEL2_COARSE_PAGE_TABLE_SIZE)));
static unsigned mmu_selftest_pool_used;

static unsigned mmu_selftest_pass;
static unsigned mmu_selftest_fail;

static int mmu_selftest_alloc(unsigned level, size_t size, size_t align,
                              struct mmu_table_ref *table, void *cookie)
{
    UNUSED(level);
    UNUSED(cookie);
    if (size > sizeof(mmu_selftest_pool[0]) || align > sizeof(mmu_selftest_pool[0]))
        return MMU_ERR_NOMEM;
    if (mmu_selftest_pool_used >= MMU_SELFTEST_POOL_TABLES)
        return MMU_ERR_NOMEM;

    table->virt = mmu_selftest_pool[mmu_selftest_pool_used];
    /* mmu_selftest_pool is an ordinary kernel global, reached through
     * the ARM virt low-window mapping; virt_to_phys() is this port's
     * existing, documented way to get the real physical address for
     * such a pointer (see virt_mmu.h). */
    table->phys = (phys_addr_type)virt_to_phys(mmu_selftest_pool[mmu_selftest_pool_used]);
    mmu_selftest_pool_used++;
    return MMU_OK;
}

static void mmu_selftest_free(const struct mmu_table_ref *table, unsigned level, void *cookie)
{
    /* Static pool, one-shot self-test: nothing to reclaim. */
    UNUSED(table);
    UNUSED(level);
    UNUSED(cookie);
}

static void mmu_selftest_check(const char *what, BOOL ok)
{
    if (ok) {
        mmu_selftest_pass++;
        KINFO(("mmu_selftest: PASS: %s\n", what));
    } else {
        mmu_selftest_fail++;
        KINFO(("mmu_selftest: FAIL: %s\n", what));
    }
}

void virt_mmu_selftest(void)
{
    /* VIRT_MMU_TABLE_PHYS is also directly usable as a virtual pointer
     * here: physical [VIRT_RAM_BASE, VIRT_RAM_BASE+ram_size) is
     * identity-mapped (see virt_mmu.c's "ram_identity" case), and the
     * reserved top megabyte holding this table falls within that
     * range. This is exactly the kind of boundary the design's address
     * model (see mmu.h) requires being explicit about -- a
     * phys_addr_type is never assumed to double as a pointer anywhere
     * else in this file: mmu_selftest_section below is an ordinary
     * low-window kernel global, and its physical address is obtained
     * through virt_to_phys(), not by casting its pointer. */
    void *root = (void *)(virt_addr_type)VIRT_MMU_TABLE_PHYS;
    virt_addr_type section_va = (virt_addr_type)mmu_selftest_section;
    phys_addr_type section_pa = (phys_addr_type)virt_to_phys(mmu_selftest_section);
    unsigned index = (unsigned)(section_va >> mmu_levels[MMU_LEVEL_ROOT].shift);
    size_t page_size = mmu_levels[MMU_LEVEL_PAGE].granule;
    virt_addr_type target_page = section_va + (virt_addr_type)page_size;
    mmu_attr_type original_attrs = MMU_ATTR_WRITE_BACK | MMU_ATTR_READ | MMU_ATTR_WRITE |
                                   MMU_ATTR_EXEC | MMU_ATTR_USER | MMU_ATTR_GLOBAL |
                                   MMU_ATTR_SHAREABLE;
    mmu_attr_type readonly_attrs = MMU_ATTR_WRITE_BACK | MMU_ATTR_READ | MMU_ATTR_EXEC |
                                   MMU_ATTR_USER | MMU_ATTR_GLOBAL | MMU_ATTR_SHAREABLE;
    volatile UBYTE *probe;
    int rc;

    mmu_selftest_pass = 0;
    mmu_selftest_fail = 0;
    mmu_selftest_pool_used = 0;

    KINFO(("mmu_selftest: starting (target section VA=0x%lx)\n", (ULONG)section_va));

    mmu_selftest_check("target section starts valid",
                       mmu_backend_entry_valid(MMU_LEVEL_ROOT, root, index));
    mmu_selftest_check("target section starts as a leaf, not a table",
                       !mmu_backend_entry_is_table(MMU_LEVEL_ROOT, root, index));

    probe = (volatile UBYTE *)section_va;
    (void)*probe;   /* pre-mutation access: must not fault */
    KINFO(("mmu_selftest: pre-mutation access OK\n"));

    mmu_set_table_allocator(mmu_selftest_alloc, mmu_selftest_free, NULL);

    /* A one-page protect inside a 1 MiB section only mmu_map_range()
     * has ever touched as a whole leaf: covers only part of it, so this
     * forces section-to-page demotion (see the design doc's Leaf
     * Demotion section). */
    rc = mmu_protect_range(root, target_page, page_size, readonly_attrs);
    mmu_selftest_check("protect() forcing demotion returns MMU_OK", rc == MMU_OK);

    mmu_selftest_check("section is now a table (demoted)",
                       mmu_backend_entry_is_table(MMU_LEVEL_ROOT, root, index));

    if (mmu_backend_entry_is_table(MMU_LEVEL_ROOT, root, index)) {
        /* mmu_selftest_alloc() is the only source of tables here, so the
         * child it handed out for this demotion is known directly --
         * no need to resolve it back from the phys address the walker
         * encoded into the parent descriptor. */
        void *child = mmu_selftest_pool[0];
        mmu_attr_type unaffected_attrs = mmu_backend_entry_leaf_attrs(MMU_LEVEL_PAGE, child, 0);
        mmu_attr_type targeted_attrs = mmu_backend_entry_leaf_attrs(MMU_LEVEL_PAGE, child, 1);

        mmu_selftest_check("unaffected page kept its original (writable) attributes",
                           (unaffected_attrs & MMU_ATTR_WRITE) != 0);
        mmu_selftest_check("targeted page lost write permission as requested",
                           (targeted_attrs & MMU_ATTR_WRITE) == 0);
    }

    probe = (volatile UBYTE *)section_va;   /* unaffected page */
    (void)*probe;
    probe = (volatile UBYTE *)target_page;  /* targeted, now read-only page */
    (void)*probe;
    KINFO(("mmu_selftest: post-protect access OK (both pages still readable)\n"));

    rc = mmu_protect_range(root, target_page, page_size, original_attrs);
    mmu_selftest_check("restoring the targeted page's attributes returns MMU_OK", rc == MMU_OK);

    /* Unmapping the whole section clears every page of the demoted
     * child table, leaving it empty -- exercising the free-now-empty-
     * child-table path (see the design doc's Table Ownership And
     * Resolution section). Safe here specifically because
     * mmu_selftest_section is section-sized/aligned and holds nothing
     * else. */
    rc = mmu_unmap_range(root, section_va, SECTION_SIZE);
    mmu_selftest_check("unmapping the whole demoted section returns MMU_OK", rc == MMU_OK);
    mmu_selftest_check("child table was freed and the section entry cleared",
                       !mmu_backend_entry_valid(MMU_LEVEL_ROOT, root, index));

    rc = mmu_map_range(root, section_va, section_pa, SECTION_SIZE, original_attrs);
    mmu_selftest_check("remapping the section returns MMU_OK", rc == MMU_OK);
    mmu_selftest_check("section is a leaf again after remapping",
                       mmu_backend_entry_valid(MMU_LEVEL_ROOT, root, index) &&
                       !mmu_backend_entry_is_table(MMU_LEVEL_ROOT, root, index));

    probe = (volatile UBYTE *)section_va;
    (void)*probe;   /* post-restore access: must not fault */
    KINFO(("mmu_selftest: post-restore access OK\n"));

    KINFO(("mmu_selftest: %u passed, %u failed\n", mmu_selftest_pass, mmu_selftest_fail));
    if (mmu_selftest_fail != 0)
        panic("mmu_selftest: %u check(s) failed\n", mmu_selftest_fail);
}

#endif /* CONF_DEBUG_MMU_MAINT_SELFTEST */
