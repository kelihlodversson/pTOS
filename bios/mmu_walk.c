/*
 * mmu_walk.c - generic pMMU page-table maintenance walker
 *
 * Shared across architectures: splits a range into the largest fitting
 * granules per level, walks down creating or removing intermediate
 * tables, demotes large leaves when an operation only covers part of
 * one, and installs or clears leaf entries.  It never touches hardware
 * descriptor bits itself -- that is the backend's job (mmu_shortdesc.c
 * for this issue).
 *
 * Design: docs/superpowers/specs/2026-08-15-portable-pmmu-page-table-design.md
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#if CONF_WITH_MMU_MAINT

#include "portab.h"
#include "mmu.h"
#include "mmu_shortdesc.h"
#include "string.h"

/* Phase 1 backends (short-descriptor formats) use 32-bit descriptors. */
#define MMU_ENTRY_SIZE  ((size_t)sizeof(uint32_t))

#define MMU_ATTR_MEMTYPE_MASK \
    (MMU_ATTR_STRONGLY_ORDERED | MMU_ATTR_DEVICE | \
     MMU_ATTR_WRITE_THROUGH | MMU_ATTR_WRITE_BACK)

#define MMU_ATTR_ALL_DEFINED \
    (MMU_ATTR_MEMTYPE_MASK | MMU_ATTR_READ | MMU_ATTR_WRITE | \
     MMU_ATTR_EXEC | MMU_ATTR_USER | MMU_ATTR_GLOBAL | MMU_ATTR_SHAREABLE)

/*
 * At most one demotion happens at the leading edge of a request and one
 * at the trailing edge (see the design doc's Synchronization section);
 * every section strictly inside the range is either fully covered or
 * a hole.  4 is generous headroom for a 2-level backend.
 */
#define MMU_MAX_PENDING  4

/* A single call touches a small, bounded number of table regions on a
 * 2-level backend; see mmu_record_touched() for what happens if this is
 * ever exceeded. */
#define MMU_MAX_TOUCHED  8

/* Bounded by the number of live intermediate tables, which is small at
 * BIOS/kernel time -- see the design doc's Risks section. */
#define MMU_MAX_TABLES   32

enum mmu_op {
    MMU_OP_MAP,
    MMU_OP_UNMAP,
    MMU_OP_PROTECT
};

struct mmu_pending_entry {
    struct mmu_table_ref ref;
    unsigned level;
};

struct mmu_touch {
    void *start;
    size_t len;
};

struct mmu_call_ctx {
    enum mmu_op op;
    virt_addr_type req_lo;    /* request start; MAP's pa progression base */
    phys_addr_type pa_base;   /* request's physical base, MAP only */
    mmu_attr_type attrs;
    BOOL dry_run;

    struct mmu_pending_entry pending[MMU_MAX_PENDING];
    unsigned pending_count;
    unsigned pending_cursor;

    struct mmu_touch touched[MMU_MAX_TOUCHED];
    unsigned touched_count;
};

struct mmu_registry_entry {
    BOOL in_use;
    phys_addr_type phys;
    void *virt;
    unsigned level;
    BOOL allocator_owned;
};

static mmu_table_alloc_type mmu_alloc_fn;
static mmu_table_free_type mmu_free_fn;
static void *mmu_alloc_cookie;

static struct mmu_registry_entry mmu_registry[MMU_MAX_TABLES];

static int mmu_walk_level(unsigned level, void *table, virt_addr_type table_base_va,
                          virt_addr_type seg_lo, virt_addr_type seg_hi,
                          struct mmu_call_ctx *ctx);

/* ---- small helpers ---- */

static virt_addr_type mmu_va_max(virt_addr_type a, virt_addr_type b)
{
    return (a > b) ? a : b;
}

static virt_addr_type mmu_va_min(virt_addr_type a, virt_addr_type b)
{
    return (a < b) ? a : b;
}

static void *mmu_entry_slot(void *table, unsigned index)
{
    return (UBYTE *)table + ((size_t)index * MMU_ENTRY_SIZE);
}

static BOOL mmu_attrs_well_formed(mmu_attr_type attrs)
{
    unsigned memtype_count = 0;

    if (attrs & ~MMU_ATTR_ALL_DEFINED)
        return FALSE;
    if (attrs & MMU_ATTR_STRONGLY_ORDERED) memtype_count++;
    if (attrs & MMU_ATTR_DEVICE)           memtype_count++;
    if (attrs & MMU_ATTR_WRITE_THROUGH)    memtype_count++;
    if (attrs & MMU_ATTR_WRITE_BACK)       memtype_count++;
    return memtype_count == 1;
}

/* ---- table registry: phys -> {virt, level, allocator_owned} ---- */

static int mmu_registry_find_index(phys_addr_type phys)
{
    unsigned i;

    for (i = 0; i < MMU_MAX_TABLES; i++) {
        if (mmu_registry[i].in_use && mmu_registry[i].phys == phys)
            return (int)i;
    }
    return -1;
}

static BOOL mmu_registry_add(phys_addr_type phys, void *virt, unsigned level, BOOL owned)
{
    unsigned i;

    if (mmu_registry_find_index(phys) >= 0)
        return TRUE;   /* already registered; re-adopting is harmless */
    for (i = 0; i < MMU_MAX_TABLES; i++) {
        if (!mmu_registry[i].in_use) {
            mmu_registry[i].in_use = TRUE;
            mmu_registry[i].phys = phys;
            mmu_registry[i].virt = virt;
            mmu_registry[i].level = level;
            mmu_registry[i].allocator_owned = owned;
            return TRUE;
        }
    }
    return FALSE;
}

static void mmu_registry_remove(phys_addr_type phys)
{
    int idx = mmu_registry_find_index(phys);

    if (idx >= 0)
        mmu_registry[idx].in_use = FALSE;
}

static BOOL mmu_registry_resolve(phys_addr_type phys, void **virt, BOOL *owned)
{
    int idx = mmu_registry_find_index(phys);

    if (idx < 0)
        return FALSE;
    if (virt)
        *virt = mmu_registry[idx].virt;
    if (owned)
        *owned = mmu_registry[idx].allocator_owned;
    return TRUE;
}

void mmu_set_table_allocator(mmu_table_alloc_type alloc, mmu_table_free_type free,
                             void *cookie)
{
    mmu_alloc_fn = alloc;
    mmu_free_fn = free;
    mmu_alloc_cookie = cookie;
}

int mmu_table_adopt(const struct mmu_table_ref *table, unsigned level)
{
    if (table == NULL || table->virt == NULL)
        return MMU_ERR_INVAL;
    if (!mmu_registry_add(table->phys, table->virt, level, FALSE))
        return MMU_ERR_NOMEM;
    return MMU_OK;
}

/* ---- per-call bookkeeping ---- */

static void mmu_record_touched(struct mmu_call_ctx *ctx, void *addr, size_t len)
{
    struct mmu_touch *last;

    if (ctx->touched_count > 0) {
        last = &ctx->touched[ctx->touched_count - 1];
        if ((UBYTE *)last->start + last->len == (UBYTE *)addr) {
            last->len += len;
            return;
        }
    }
    if (ctx->touched_count < MMU_MAX_TOUCHED) {
        ctx->touched[ctx->touched_count].start = addr;
        ctx->touched[ctx->touched_count].len = len;
        ctx->touched_count++;
        return;
    }
    /* Not expected given Phase 1's level count; degrade to a broader
     * flush rather than silently dropping a range. */
    last = &ctx->touched[MMU_MAX_TOUCHED - 1];
    {
        UBYTE *lo = ((UBYTE *)last->start < (UBYTE *)addr) ? (UBYTE *)last->start : (UBYTE *)addr;
        UBYTE *hi_last = (UBYTE *)last->start + last->len;
        UBYTE *hi_addr = (UBYTE *)addr + len;
        UBYTE *hi = (hi_last > hi_addr) ? hi_last : hi_addr;

        last->start = lo;
        last->len = (size_t)(hi - lo);
    }
}

static void mmu_sync_touched(struct mmu_call_ctx *ctx)
{
    unsigned i;

    for (i = 0; i < ctx->touched_count; i++)
        mmu_backend_sync(ctx->touched[i].start, ctx->touched[i].len);
}

static void mmu_rollback_pending(struct mmu_call_ctx *ctx)
{
    unsigned i;

    for (i = 0; i < ctx->pending_count; i++) {
        mmu_registry_remove(ctx->pending[i].ref.phys);
        if (mmu_free_fn)
            mmu_free_fn(&ctx->pending[i].ref, ctx->pending[i].level, mmu_alloc_cookie);
    }
    ctx->pending_count = 0;
}

/*
 * Obtains a child table for the given level: on a dry run, actually
 * allocates and registers it (added to the pending list for pass 2 to
 * consume in the same order); during commit, pops the next entry the
 * dry run already reserved.  "demote" tables are fully populated with
 * orig_pa/orig_attrs so the leaf they replace is preserved before the
 * caller applies the requested operation to the finer level; "growth"
 * tables (demote == FALSE) are zeroed instead.  Either way, this only
 * ever writes to the NEW table's own memory, never to the live,
 * already-reachable tree -- linking it in is the caller's job.
 */
static int mmu_obtain_child(unsigned child_level, struct mmu_call_ctx *ctx,
                            struct mmu_table_ref *out,
                            BOOL demote, phys_addr_type orig_pa, mmu_attr_type orig_attrs)
{
    const struct mmu_level_desc *ld = &mmu_levels[child_level];
    struct mmu_table_ref ref;

    if (ctx->dry_run) {
        int rc;

        if (ctx->pending_count >= MMU_MAX_PENDING)
            return MMU_ERR_NOMEM;
        if (!mmu_alloc_fn)
            return MMU_ERR_NOMEM;
        rc = mmu_alloc_fn(child_level, ld->table_size, ld->table_align, &ref, mmu_alloc_cookie);
        if (rc != MMU_OK)
            return rc;
        if (!mmu_registry_add(ref.phys, ref.virt, child_level, TRUE)) {
            if (mmu_free_fn)
                mmu_free_fn(&ref, child_level, mmu_alloc_cookie);
            return MMU_ERR_NOMEM;
        }
        ctx->pending[ctx->pending_count].ref = ref;
        ctx->pending[ctx->pending_count].level = child_level;
        ctx->pending_count++;
    } else {
        if (ctx->pending_cursor >= ctx->pending_count)
            return MMU_ERR_INVAL;   /* pass 2 diverged from pass 1 */
        ref = ctx->pending[ctx->pending_cursor].ref;
        ctx->pending_cursor++;
    }

    if (demote) {
        unsigned j;

        for (j = 0; j < ld->entries; j++) {
            phys_addr_type child_pa = orig_pa +
                (phys_addr_type)((size_t)j * ld->granule);
            mmu_backend_install_leaf_entry(child_level, ref.virt, j, child_pa, orig_attrs);
        }
    } else {
        bzero(ref.virt, ld->table_size);
    }

    *out = ref;
    return MMU_OK;
}

static BOOL mmu_table_is_empty(unsigned level, void *table)
{
    const struct mmu_level_desc *ld = &mmu_levels[level];
    unsigned i;

    for (i = 0; i < ld->entries; i++) {
        if (mmu_backend_entry_valid(level, table, i))
            return FALSE;
    }
    return TRUE;
}

/* ---- per-entry handlers, one per operation ---- */

static int mmu_walk_map_entry(unsigned level, void *table, unsigned index,
                              virt_addr_type entry_va, virt_addr_type seg_lo,
                              virt_addr_type seg_hi, BOOL full,
                              BOOL valid, BOOL is_table, BOOL last_level,
                              struct mmu_call_ctx *ctx)
{
    const struct mmu_level_desc *ld = &mmu_levels[level];

    if (valid) {
        if (is_table) {
            void *child;
            phys_addr_type child_phys = mmu_backend_entry_child_phys(level, table, index);

            if (!mmu_registry_resolve(child_phys, &child, NULL))
                return MMU_ERR_INVAL;
            return mmu_walk_level(level + 1, child, entry_va, seg_lo, seg_hi, ctx);
        }
        return MMU_ERR_OVERLAP;
    }

    if (full && ld->leaf_capable) {
        if (!ctx->dry_run) {
            phys_addr_type pa = ctx->pa_base +
                (phys_addr_type)(entry_va - ctx->req_lo);

            mmu_backend_install_leaf_entry(level, table, index, pa, ctx->attrs);
            mmu_record_touched(ctx, mmu_entry_slot(table, index), MMU_ENTRY_SIZE);
        }
        return MMU_OK;
    }

    if (last_level)
        return MMU_ERR_INVAL;   /* alignment validation should prevent this */

    {
        struct mmu_table_ref child;
        int rc = mmu_obtain_child(level + 1, ctx, &child, FALSE, 0, 0);

        if (rc != MMU_OK)
            return rc;
        if (!ctx->dry_run) {
            mmu_backend_install_table_entry(level, table, index, child.phys);
            mmu_record_touched(ctx, mmu_entry_slot(table, index), MMU_ENTRY_SIZE);
        }
        return mmu_walk_level(level + 1, child.virt, entry_va, seg_lo, seg_hi, ctx);
    }
}

static int mmu_walk_unmap_entry(unsigned level, void *table, unsigned index,
                                virt_addr_type entry_va, virt_addr_type seg_lo,
                                virt_addr_type seg_hi, BOOL full,
                                BOOL valid, BOOL is_table, BOOL last_level,
                                struct mmu_call_ctx *ctx)
{
    UNUSED(last_level);

    if (!valid)
        return MMU_ERR_NOTMAPPED;

    if (is_table) {
        void *child;
        BOOL owned;
        phys_addr_type child_phys = mmu_backend_entry_child_phys(level, table, index);
        int rc;

        if (!mmu_registry_resolve(child_phys, &child, &owned))
            return MMU_ERR_INVAL;
        rc = mmu_walk_level(level + 1, child, entry_va, seg_lo, seg_hi, ctx);
        if (rc != MMU_OK)
            return rc;
        if (!ctx->dry_run && owned && mmu_table_is_empty(level + 1, child)) {
            struct mmu_table_ref ref;

            ref.virt = child;
            ref.phys = child_phys;
            if (mmu_free_fn)
                mmu_free_fn(&ref, level + 1, mmu_alloc_cookie);
            mmu_registry_remove(child_phys);
            mmu_backend_clear_entry(level, table, index);
            mmu_record_touched(ctx, mmu_entry_slot(table, index), MMU_ENTRY_SIZE);
        }
        return MMU_OK;
    }

    if (full) {
        if (!ctx->dry_run) {
            mmu_backend_clear_entry(level, table, index);
            mmu_record_touched(ctx, mmu_entry_slot(table, index), MMU_ENTRY_SIZE);
        }
        return MMU_OK;
    }

    /* partial coverage of a leaf: demote, then clear the requested
     * sub-range at the finer level */
    {
        phys_addr_type orig_pa = mmu_backend_entry_leaf_phys(level, table, index);
        mmu_attr_type orig_attrs = mmu_backend_entry_leaf_attrs(level, table, index);
        struct mmu_table_ref child;
        int rc = mmu_obtain_child(level + 1, ctx, &child, TRUE, orig_pa, orig_attrs);

        if (rc != MMU_OK)
            return rc;
        if (!ctx->dry_run) {
            mmu_backend_install_table_entry(level, table, index, child.phys);
            mmu_record_touched(ctx, mmu_entry_slot(table, index), MMU_ENTRY_SIZE);
        }
        return mmu_walk_level(level + 1, child.virt, entry_va, seg_lo, seg_hi, ctx);
    }
}

static int mmu_walk_protect_entry(unsigned level, void *table, unsigned index,
                                  virt_addr_type entry_va, virt_addr_type seg_lo,
                                  virt_addr_type seg_hi, BOOL full,
                                  BOOL valid, BOOL is_table, BOOL last_level,
                                  struct mmu_call_ctx *ctx)
{
    UNUSED(last_level);

    if (!valid)
        return MMU_ERR_NOTMAPPED;

    if (is_table) {
        void *child;
        phys_addr_type child_phys = mmu_backend_entry_child_phys(level, table, index);

        if (!mmu_registry_resolve(child_phys, &child, NULL))
            return MMU_ERR_INVAL;
        return mmu_walk_level(level + 1, child, entry_va, seg_lo, seg_hi, ctx);
    }

    if (full) {
        if (!ctx->dry_run) {
            phys_addr_type pa = mmu_backend_entry_leaf_phys(level, table, index);

            mmu_backend_install_leaf_entry(level, table, index, pa, ctx->attrs);
            mmu_record_touched(ctx, mmu_entry_slot(table, index), MMU_ENTRY_SIZE);
        }
        return MMU_OK;
    }

    /* partial coverage of a leaf: demote preserving the original
     * attributes, then re-protect the requested sub-range at the finer
     * level */
    {
        phys_addr_type orig_pa = mmu_backend_entry_leaf_phys(level, table, index);
        mmu_attr_type orig_attrs = mmu_backend_entry_leaf_attrs(level, table, index);
        struct mmu_table_ref child;
        int rc = mmu_obtain_child(level + 1, ctx, &child, TRUE, orig_pa, orig_attrs);

        if (rc != MMU_OK)
            return rc;
        if (!ctx->dry_run) {
            mmu_backend_install_table_entry(level, table, index, child.phys);
            mmu_record_touched(ctx, mmu_entry_slot(table, index), MMU_ENTRY_SIZE);
        }
        return mmu_walk_level(level + 1, child.virt, entry_va, seg_lo, seg_hi, ctx);
    }
}

/*
 * seg_lo/seg_hi (and the this_lo/this_hi derived from them below) are an
 * inclusive [lo, hi] range -- the last byte requested, not one past it.
 * A half-open exclusive bound cannot represent a request that reaches
 * the very top of the address space (va + size wraps to exactly 0), and
 * this walker must: mmu_map_range()/etc. hand this level the whole
 * requested range in that same inclusive form (see va + size - 1 at
 * each public entry point below).
 */
static int mmu_walk_level(unsigned level, void *table, virt_addr_type table_base_va,
                          virt_addr_type seg_lo, virt_addr_type seg_hi,
                          struct mmu_call_ctx *ctx)
{
    const struct mmu_level_desc *ld = &mmu_levels[level];
    BOOL last_level = (level + 1 >= MMU_ARCH_LEVELS);
    unsigned first = (unsigned)((seg_lo - table_base_va) >> ld->shift);
    unsigned last = (unsigned)((seg_hi - table_base_va) >> ld->shift);
    unsigned i;

    for (i = first; i <= last; i++) {
        virt_addr_type entry_va = table_base_va + ((virt_addr_type)i << ld->shift);
        virt_addr_type entry_last = entry_va + (virt_addr_type)ld->granule - 1;
        virt_addr_type this_lo = mmu_va_max(entry_va, seg_lo);
        virt_addr_type this_hi = mmu_va_min(entry_last, seg_hi);
        BOOL full = (this_lo == entry_va) && (this_hi == entry_last);
        BOOL valid = mmu_backend_entry_valid(level, table, i);
        BOOL is_table = valid && mmu_backend_entry_is_table(level, table, i);
        int rc;

        switch (ctx->op) {
        case MMU_OP_MAP:
            rc = mmu_walk_map_entry(level, table, i, entry_va, this_lo, this_hi,
                                    full, valid, is_table, last_level, ctx);
            break;
        case MMU_OP_UNMAP:
            rc = mmu_walk_unmap_entry(level, table, i, entry_va, this_lo, this_hi,
                                      full, valid, is_table, last_level, ctx);
            break;
        default:
            rc = mmu_walk_protect_entry(level, table, i, entry_va, this_lo, this_hi,
                                        full, valid, is_table, last_level, ctx);
            break;
        }
        if (rc != MMU_OK)
            return rc;
    }
    return MMU_OK;
}

/* ---- alignment / rounding ---- */

static size_t mmu_smallest_page(void)
{
    return mmu_levels[MMU_ARCH_LEVELS - 1].granule;
}

static size_t mmu_round_up(size_t size, size_t granule)
{
    return (size + granule - 1) & ~(granule - 1);
}

/* ---- public API ---- */

int mmu_map_range(void *root, virt_addr_type va, phys_addr_type pa,
                  size_t size, mmu_attr_type attrs)
{
    struct mmu_call_ctx ctx;
    size_t page = mmu_smallest_page();
    int rc;

    if (size == 0)
        return MMU_ERR_INVAL;
    if ((va & (virt_addr_type)(page - 1)) != 0)
        return MMU_ERR_INVAL;
    if ((pa & (phys_addr_type)(page - 1)) != 0)
        return MMU_ERR_INVAL;
    if (!mmu_attrs_well_formed(attrs))
        return MMU_ERR_INVAL;
    if (!mmu_backend_attrs_supported(attrs))
        return MMU_ERR_UNSUPPORTED;

    size = mmu_round_up(size, page);

    ctx.op = MMU_OP_MAP;
    ctx.req_lo = va;
    ctx.pa_base = pa;
    ctx.attrs = attrs;
    ctx.pending_count = 0;
    ctx.pending_cursor = 0;
    ctx.touched_count = 0;

    ctx.dry_run = TRUE;
    rc = mmu_walk_level(0, root, 0, va, va + (virt_addr_type)size - 1, &ctx);
    if (rc != MMU_OK) {
        mmu_rollback_pending(&ctx);
        return rc;
    }

    ctx.dry_run = FALSE;
    ctx.pending_cursor = 0;
    ctx.touched_count = 0;
    rc = mmu_walk_level(0, root, 0, va, va + (virt_addr_type)size - 1, &ctx);
    if (rc == MMU_OK)
        mmu_sync_touched(&ctx);
    return rc;
}

int mmu_unmap_range(void *root, virt_addr_type va, size_t size)
{
    struct mmu_call_ctx ctx;
    size_t page = mmu_smallest_page();
    int rc;

    if (size == 0)
        return MMU_ERR_INVAL;
    if ((va & (virt_addr_type)(page - 1)) != 0)
        return MMU_ERR_INVAL;

    size = mmu_round_up(size, page);

    ctx.op = MMU_OP_UNMAP;
    ctx.req_lo = va;
    ctx.pa_base = 0;
    ctx.attrs = 0;
    ctx.pending_count = 0;
    ctx.pending_cursor = 0;
    ctx.touched_count = 0;

    ctx.dry_run = TRUE;
    rc = mmu_walk_level(0, root, 0, va, va + (virt_addr_type)size - 1, &ctx);
    if (rc != MMU_OK) {
        mmu_rollback_pending(&ctx);
        return rc;
    }

    ctx.dry_run = FALSE;
    ctx.pending_cursor = 0;
    ctx.touched_count = 0;
    rc = mmu_walk_level(0, root, 0, va, va + (virt_addr_type)size - 1, &ctx);
    if (rc == MMU_OK)
        mmu_sync_touched(&ctx);
    return rc;
}

int mmu_protect_range(void *root, virt_addr_type va, size_t size, mmu_attr_type attrs)
{
    struct mmu_call_ctx ctx;
    size_t page = mmu_smallest_page();
    int rc;

    if (size == 0)
        return MMU_ERR_INVAL;
    if ((va & (virt_addr_type)(page - 1)) != 0)
        return MMU_ERR_INVAL;
    if (!mmu_attrs_well_formed(attrs))
        return MMU_ERR_INVAL;
    if (!mmu_backend_attrs_supported(attrs))
        return MMU_ERR_UNSUPPORTED;

    size = mmu_round_up(size, page);

    ctx.op = MMU_OP_PROTECT;
    ctx.req_lo = va;
    ctx.pa_base = 0;
    ctx.attrs = attrs;
    ctx.pending_count = 0;
    ctx.pending_cursor = 0;
    ctx.touched_count = 0;

    ctx.dry_run = TRUE;
    rc = mmu_walk_level(0, root, 0, va, va + (virt_addr_type)size - 1, &ctx);
    if (rc != MMU_OK) {
        mmu_rollback_pending(&ctx);
        return rc;
    }

    ctx.dry_run = FALSE;
    ctx.pending_cursor = 0;
    ctx.touched_count = 0;
    rc = mmu_walk_level(0, root, 0, va, va + (virt_addr_type)size - 1, &ctx);
    if (rc == MMU_OK)
        mmu_sync_touched(&ctx);
    return rc;
}

static void mmu_sync_all_level(unsigned level, void *table)
{
    const struct mmu_level_desc *ld = &mmu_levels[level];

    mmu_backend_sync(table, ld->table_size);
    if (level + 1 < MMU_ARCH_LEVELS) {
        unsigned i;

        for (i = 0; i < ld->entries; i++) {
            if (mmu_backend_entry_is_table(level, table, i)) {
                phys_addr_type child_phys = mmu_backend_entry_child_phys(level, table, i);
                void *child;

                if (mmu_registry_resolve(child_phys, &child, NULL))
                    mmu_sync_all_level(level + 1, child);
            }
        }
    }
}

void mmu_sync_all(void *root)
{
    mmu_sync_all_level(0, root);
}

#endif /* CONF_WITH_MMU_MAINT */
