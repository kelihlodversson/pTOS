# Portable pMMU Page-Table Maintenance Abstraction Design

## Context

Every pTOS target that needs an MMU sets up its page tables with machine- and CPU-specific code, none of which is usable from shared C code:

- **68030** (`bios/pmmu030.c`): a static, compile-time tree (`struct pmmutable`) copied into protected RAM via `memcpy`. No runtime maintenance.
- **68040/68060** (`bios/arch/m68k/68040_pmmu.S`): hand-written assembler that walks root/pointer/page tables and allocates tables from a `next_free` pool (`create_table(logical, physical, size, mode)`).
- **ARMv6/v7 Raspberry Pi** (`bios/machine/raspi/memory.c`): an L1 section table plus a coarse page table used only for first-megabyte text protection. `raspi_mmu_protect_range()` is the only runtime update.
- **ARMv7 QEMU virt** (`bios/machine/virt-arm/virt_mmu.c`): a static short-descriptor bootstrap run before BSS is cleared.

There is no `phys_addr_type`/`virt_addr_type` (addresses are plain `ULONG`), no page-table-page allocator, and the PCI layer already hand-rolls `bus_to_phys()`/`phys_to_bus()` translation. Issue #76 (ARM32 LPAE and high-address MMIO) needs physical addresses above 4 GiB on a 32-bit ARM build, and needs dynamic device mappings (`ioremap`/`iounmap`).

This issue (tracked as #201) designs and builds a small C-level abstraction for page-table *maintenance* that is shared across the architectures pTOS targets: 68030/68040/68060, ARM32, and later AArch64 and x86/x86-64.

## Goals

- Provide a range-based C API for mapping, unmapping, protecting and syncing virtual address ranges.
- Back the API with a generic walker that selects the largest fitting page level automatically.
- Isolate hardware descriptor formats per architecture behind a small header contract selected through the existing `arch/`/`machine/` vpath mechanism.
- Gate feature differences (page-table level count, page sizes, pointer width, supported attributes) via typedefs and `MMU_ARCH_*` defines.
- Support physical addresses wider than the virtual address space on ARM32 (highmem/LPAE), while keeping virtual addresses — and `virt_addr_type`, which tracks pointer width — 32-bit there.
- Provide the foundation for dynamic device mappings (`ioremap`), replacing the existing per-machine MMU setup code, and future process virtual memory.

## Scope (Phase 1)

- The abstraction layer: `include/mmu.h`, `bios/mmu_walk.c`.
- The ARM32 short-descriptor backend: `bios/arch/arm/mmu_shortdesc.h` + `.c`.
- Port Raspberry Pi's `init_mmu()` and `raspi_mmu_protect_range()` in
  `bios/machine/raspi/memory.c` onto the new API.
- Boot-time functional validation on QEMU ARM `virt`.

`virt_mmu_bootstrap()` stays untouched; m68k, LPAE, AArch64 and x86-64
backends are follow-up phases.

## Non-Goals

- Do not design a full Linux-style folded PGD/PUD/PMD/PTE walk with per-level helper proliferation.
- Do not implement address-space or process memory management in this issue.
- Do not build a generalized VM object model (mapped file objects, copy-on-write, reference-counted physical pages); this layer only maintains page-table descriptors for ranges the caller already knows the physical backing of.
- Do not remove the pre-MMU `virt_mmu_bootstrap()`; it runs before BSS and C code exist and its job is to make the layer runnable, and it stays architecture-specific and outside the maintenance layer.
- Do not port the 68030/68040 setup code in this issue; that is follow-up phase 2 (m68k short-format backend).
- Do not implement ARM LPAE, AArch64 or x86-64 backends in this issue; the abstraction must leave room for them, not build them.

## Architecture

Three component kinds, following existing pTOS conventions (`arch/`/`machine/` subdirectories, vpath selection, unique object basenames):

1. **Shared API** (`include/mmu.h`): types, attribute flags, error codes, and the range-based API.
2. **Generic walker** (`bios/mmu_walk.c`): shared C that splits a range into the largest fitting granules per level, walks down creating or removing intermediate tables, demotes large leaves when an operation only covers part of one, and installs or clears leaf entries. It never touches hardware descriptor bits itself.
3. **Architecture backends**: one header contract plus one implementation per descriptor format, selected by vpath:
   - `bios/arch/arm/mmu_shortdesc.h` + `.c` — VMSAv6/v7 short-descriptor (reference backend, this issue)
   - `bios/arch/m68k/mmu_short.h` + `.c` — m68k short-format (follow-up phase 2; the walker and geometry-as-data approach are shared, but whether 68030 and 68040/68060 end up as one backend or two variants is decided during that phase — see Follow-Up Integration)
   - later: LPAE, AArch64, x86-64 backends

The backend provides (a) the page-table geometry as a `const` array of level descriptors and (b) small entry operations as static inline functions in the header. The walker includes the arch header and calls these operations directly; there are no function-pointer tables in the hot path. Only one descriptor format compiles per configuration, so this stays cheap and C90-friendly.

### Level geometry as data

The geometry is data, so one entry-format implementation covers multiple CPUs. Each level descriptor records:

- the virtual-address shift for the level,
- the number of entries per table,
- the table size in bytes,
- whether the level may hold leaf descriptors,
- the granule size a leaf at that level covers.

Examples of the resulting level tables:

| Backend | Levels | Granules | Leaf sizes |
| --- | --- | --- | --- |
| ARM short-desc | 2 | 1 MiB, 4 KiB | 1 MiB section, 4 KiB small page |
| 68030 | 3 | 256 MiB, 16 MiB, 1 MiB | 256 MiB, 16 MiB, 1 MiB (any level may hold a page descriptor) |
| 68040/68060 | 3 | 32 MiB, 256 KiB, 4 KiB | 4 KiB (root and pointer levels are table-only in the current assembler `create_table`) |
| LPAE / AArch64 / x86-64 | 3-4 | ... | 1 GiB, 2 MiB, 4 KiB |

The walker needs no knowledge of which CPU it is on beyond the level descriptors and the entry ops.

The 68030 and 68040/68060 rows are taken directly from the existing inherited code, not estimated: `bios/pmmu030.c`'s `tia`/`tib1`/`tib2`/`tic` tables each have 16 entries over their respective VA span (4 GiB/16, 256 MiB/16, 16 MiB/16), and `PMMU_SF_PAGE()` leaf descriptors appear at all three levels (`tia`, `tib1` and `tic`), not only the finest one. `bios/arch/m68k/68040_pmmu.S`'s `create_table` extracts a 7-bit root index from VA bits 25-31 (`rol.l #7,d4` then a 7-bit mask — 32 MiB per root entry), a 7-bit pointer index from VA bits 18-24 (`swap d5` then a 9-bit mask starting at bit 2 — 256 KiB per pointer entry), and a 6-bit page index from VA bits 12-17 (4 KiB pages); it only ever writes page-level descriptors, so root and pointer levels are table-only today.

### Leaf demotion (table splitting)

`mmu_map_range()` always picks the largest leaf size that fits a sub-range, so a single large leaf — a 1 MiB ARM section, a 68030 256 MiB/16 MiB/1 MiB page descriptor, or a future LPAE/AArch64/x86-64 block — can end up covering address ranges a later call only wants to touch part of; for example, marking one 4 KiB page inside a 1 MiB kernel text section read-only. `mmu_protect_range()` and `mmu_unmap_range()` handle this by demoting the leaf before touching it:

1. If the requested sub-range covers an existing leaf entirely, the walker operates on that leaf directly (clears it, or rewrites its attributes) — no demotion needed.
2. If the requested sub-range covers only part of an existing large leaf, the walker allocates a next-level table, sized and aligned per that level's geometry entry, through `mmu_table_alloc_type`.
3. The walker populates every entry of that new table with a leaf mapping equivalent to the corresponding slice of the original large leaf: physical address advancing by the child level's granule size starting from the original leaf's base PA, and the same `mmu_attr_type` attributes the original leaf carried.
4. Only once the child table is fully populated does the walker register it as allocator-owned and overwrite the parent's leaf descriptor with a table descriptor pointing at the new child (see Table Ownership And Resolution); this single write is what makes the split visible.
5. The walker then continues the requested protect or unmap at the finer level. If the requested sub-range is still smaller than the new level's leaf size, this recurses into another demotion — this applies on any backend with more than two useful leaf levels (68030, LPAE, AArch64, x86-64), not just a single split.

Demotion is one-directional in Phase 1: once split, a range is never automatically re-coalesced back into a large leaf. A future coalescing operation, if ever needed, is out of scope here.

## Address Model And Attributes

`include/mmu.h` defines:

```c
typedef uintptr_t virt_addr_type;   /* pointer-sized: matches the target's VA/pointer width */

#if CONF_MMU_PHYS_64
typedef uint64_t phys_addr_type;
#else
typedef uint32_t phys_addr_type;
#endif
```

Both typedefs use standard C fixed-width/pointer-width types (`uintptr_t`, `uint32_t`, `uint64_t` from `<stdint.h>`, already pulled in by `include/portab.h`) rather than the inherited all-caps aliases (`ULONG`, `UQUAD`): this is new architecture-neutral infrastructure with no TOS/GEM ABI meaning to preserve, so it follows the general pTOS convention of preferring standard types for pure integer/pointer width over the legacy aliases, which stay in place for existing EmuTOS-inherited and ABI-facing code. `virt_addr_type` is never `ULONG`: `ULONG` is a fixed 32-bit type in this tree (`uint32_t` everywhere, including on 64-bit targets), which would silently truncate AArch64/x86-64 pointers. `uintptr_t` tracks whatever the target's C pointer width actually is, so this header needs no per-architecture `#ifdef` for it.

`phys_addr_type` is sized independently of pointer width, gated by `CONF_MMU_PHYS_64`. This is what lets ARM32 LPAE keep 32-bit virtual addresses/pointers while physical addresses widen to 64 bits: `CONF_MMU_PHYS_64` toggles `phys_addr_type` alone and never touches `virt_addr_type`.

Per pTOS's type-naming convention, `virt_addr_type`, `phys_addr_type`, `mmu_attr_type` and any later pTOS-defined semantic address type (for example a future `bus_addr_type`) all take the `_type` suffix, never `_t`: `_t`-suffixed names are reserved for types the C implementation or a system header provides (`uint32_t`, `uintptr_t`, `size_t`, ...), and pTOS does not mint new typedefs that could be mistaken for one. This also applies to the allocator function-pointer typedefs below (`mmu_table_alloc_type`, `mmu_table_free_type`).

The two types are deliberately not interchangeable:

- A `virt_addr_type` is always safe to use as a C pointer's numeric value on that target — they share width by construction.
- A `phys_addr_type` is **never** safe to treat as a C pointer, even on a target where identity mapping currently makes the numeric values coincide (e.g. today's ARM32 boot-time identity map, or the 68030 static tree). Physical addresses reach C code only as `phys_addr_type` values — passed to/from `mmu_map_range()`, carried in `struct mmu_table_ref.phys`, and handled by backend descriptor encoders. Converting one to a pointer is only ever valid at a boundary that has separately established the target is actually identity-mapped there (for example, early bootstrap code walking the identity map it just built); such a conversion must be an explicit, narrow, commented cast at that boundary, never implicit and never assumed generally true.

Attributes are a small cross-architecture bitmask:

```c
typedef uint32_t mmu_attr_type;
```

that each backend maps to hardware bits:

- memory type: `MMU_ATTR_STRONGLY_ORDERED`, `MMU_ATTR_DEVICE`, `MMU_ATTR_WRITE_THROUGH`, `MMU_ATTR_WRITE_BACK`
- permissions: `MMU_ATTR_READ`, `MMU_ATTR_WRITE`, `MMU_ATTR_EXEC`, `MMU_ATTR_USER`
- `MMU_ATTR_GLOBAL`, `MMU_ATTR_SHAREABLE`

`MMU_ATTR_STRONGLY_ORDERED` exists because Phase 1 has to preserve a memory type distinction the current Raspberry Pi `init_mmu()` already makes and is not otherwise representable: the reserved top-of-RAM megabyte holding page tables and cache-coherent buffers is mapped `TEX=0,C=0,B=0` ("strongly ordered": no reordering, no buffering, the strictest ARM memory type), which is stricter than the `TEX=0,C=0,B=1` ("shared device") mapping `init_mmu()` uses for the MMIO region above it. Folding both into a single generic `MMU_ATTR_DEVICE` would either lose that distinction or force the ARM backend to guess; a backend without a dedicated encoding for it may normalize `MMU_ATTR_STRONGLY_ORDERED` to its strictest available device-like type, per the normalization rule below.

Feature gating lives in the backend header as `MMU_ARCH_*` defines: `MMU_ARCH_LEVELS`, `MMU_ARCH_HAS_EXEC`, `MMU_ARCH_HAS_USER`, `MMU_ARCH_HAS_BIG_PAGES`, `MMU_ARCH_PHYS_BITS`, `MMU_ARCH_TABLE_ALIGN`, plus any further per-memory-type/shareability capability bits a backend needs. A backend simply omits a capability define it lacks; see Attributes under API Shape for how the walker uses these.

## API Shape

```c
struct mmu_table_ref {
    void *virt;             /* address software uses to write the table     */
    phys_addr_type phys;    /* address to encode into the parent descriptor */
};

typedef int  (*mmu_table_alloc_type)(unsigned level, size_t size, size_t align,
                                     struct mmu_table_ref *table, void *cookie);
typedef void (*mmu_table_free_type)(const struct mmu_table_ref *table,
                                    unsigned level, void *cookie);

void mmu_set_table_allocator(mmu_table_alloc_type alloc,
                             mmu_table_free_type free, void *cookie);

int  mmu_table_adopt(const struct mmu_table_ref *table, unsigned level);

int  mmu_map_range(void *root, virt_addr_type va, phys_addr_type pa,
                   size_t size, mmu_attr_type attrs);
int  mmu_unmap_range(void *root, virt_addr_type va, size_t size);
int  mmu_protect_range(void *root, virt_addr_type va, size_t size,
                       mmu_attr_type attrs);
void mmu_sync_all(void *root);
```

- `root` is an opaque pointer to the root table, always caller-owned (see Table Ownership And Resolution); the layer only ever edits its contents.
- `mmu_table_alloc_type` receives the level being populated and that level's table size/alignment (from its geometry entry — see Level Geometry As Data), and must fill in both the `virt` address the walker will use to write entries and the `phys` address to encode into the parent descriptor. It returns `MMU_OK` on success or `MMU_ERR_NOMEM` on failure. `mmu_table_free_type` is the inverse, given the same pair plus the level, and — per Table Ownership And Resolution — is only ever called on tables the layer itself allocated.
- `mmu_table_adopt()` registers a table the caller built outside the allocator (a pre-existing/static table spliced directly into the tree) so the walker can resolve and walk into it; see Table Ownership And Resolution.
- `mmu_map_range()` maps at the largest fitting level for each sub-range.
- `mmu_unmap_range()` clears leaf entries and frees now-empty allocator-owned intermediate tables through the free hook (see Table Ownership And Resolution).
- `mmu_protect_range()` changes attributes of existing mappings (this is what Raspberry Pi's `raspi_mmu_protect_range()` generalizes).
- `mmu_protect_range()` and `mmu_unmap_range()` demote (split) a large leaf when the requested range covers only part of it; see Leaf Demotion.
- All three range operations synchronize page-table memory and TLB/instruction state internally as part of a successful call (see Synchronization); callers do not need a separate sync call in the common path.

This is the semantic contract; the exact spelling of `mmu_table_alloc_type`/`mmu_table_ref`/`mmu_table_adopt` may change during implementation if a cleaner C90-compatible form fits the tree better, but the level/size/align input and virt+phys output the allocator carries, and the resolution mechanism below, must not be dropped.

### Table ownership and resolution

Once a table is linked into the tree, its parent descriptor only ever holds the child's `phys` address — that is what hardware descriptor formats encode. A later call that walks back down through that descriptor, to map/unmap/protect something inside it or below it, needs a `virt` pointer to actually read and write the child table's entries in C. The `{virt, phys}` pair the allocator returned at creation time is not, by itself, still reachable at that point, so the walker needs a way to get from a descriptor's `phys` back to a usable `virt`.

The walker solves this with an internal **table registry**: a private lookup structure, keyed by `phys`, that records `{virt, level, allocator_owned}` for every intermediate table currently reachable below any root — `root` itself is excluded, since its `virt` is passed directly on every call and nothing points *to* it. The registry is walker-internal state, not part of the public contract; an array, sorted list, or small hash table are all valid implementations, and none of this is hot-path (see Risks).

- The **root table** is always caller-owned: it is set up by boot code (a static tree, or a table pre-populated before `mmu_set_table_allocator()` is even called) and handed to every API call through the `root` parameter directly, so it never needs a registry entry. The walker never allocates, replaces, or frees the root table object itself — only its entries.
- Every **intermediate table below the root** that the walker itself creates — while `mmu_map_range()` grows the tree, or during leaf demotion — is **MMU-layer owned**. When such a table is linked into the tree (the parent descriptor write in pass 2 of Transactional Semantics), the walker registers it with `allocator_owned = true`. Only registry entries with `allocator_owned = true` may ever be passed to `mmu_table_free_type`.
- `mmu_unmap_range()` frees an intermediate table only when (a) its registry entry has `allocator_owned = true`, and (b) the unmap just removed its last remaining entry, making it fully empty; freeing it also removes its registry entry. The walker must never free a table it cannot find as an allocator-owned registry entry.
- Pre-existing/static tables that a machine port splices directly into the tree without going through `mmu_table_alloc_type` (e.g. Raspberry Pi's boot-time coarse table, or the 68030's compiled-in tree) are not automatically resolvable — nothing recorded their `virt` — so before any range call needs to walk through one, the port registers it once with `mmu_table_adopt(&ref, level)`, where `ref.virt`/`ref.phys` are the table's already-known addresses. `mmu_table_adopt()` inserts a registry entry with `allocator_owned = false`: the walker can now resolve and edit the table's entries via `mmu_unmap_range()`/`mmu_protect_range()`, but will never call `mmu_table_free_type` on it, however empty it becomes. A port that wants a formerly-static table fully absorbed into layer ownership should rebuild it once through `mmu_map_range()` after installing the allocator, instead of adopting it.
- A `phys` value the walker extracts from a descriptor while walking, but cannot find in the registry, indicates a table reachable from `root` that was never allocated or adopted — a configuration bug in the port, not a runtime condition the range API is expected to recover from.

### Alignment and range semantics

- `va` must be aligned to the backend's smallest page size; `pa` (for `mmu_map_range()`) must likewise be aligned to the smallest page size.
- `size` may be any positive length and is rounded up internally to a whole number of smallest pages; it does not need to be pre-aligned.
- `size == 0` is rejected with `MMU_ERR_INVAL` rather than treated as a no-op, so a caller cannot silently mis-call the API with a range that was truncated to nothing.
- These rules are identical for `mmu_map_range()`, `mmu_unmap_range()` and `mmu_protect_range()`.

### Partial coverage and holes

- `mmu_map_range()` fails the whole call with `MMU_ERR_OVERLAP` on the first part of the requested range it finds already mapped; it never partially maps a range.
- `mmu_unmap_range()` and `mmu_protect_range()` fail the whole call with `MMU_ERR_NOTMAPPED` if any part of the requested range is unmapped; they never operate on just the mapped subset of a range that has holes. A caller that expects holes must query or split the range itself — the layer does not guess.

### Transactional semantics

Every range operation runs in two passes over the same requested range, using the same walker/backend code paths in a "dry" and a "commit" mode:

1. **Validate + reserve.** Walk the range without installing anything: check every sub-range against the partial-coverage rule (`MMU_ERR_OVERLAP` / `MMU_ERR_NOTMAPPED`), check every requested attribute against the backend's capability bits (`MMU_ERR_UNSUPPORTED`; see Attributes), and allocate — but do not yet link in — every intermediate table the operation could need, including tables needed for leaf demotion. This also reserves a table-registry slot (see Table Ownership And Resolution) for each such table, so pass 2's registration of newly linked tables cannot fail either. If this pass fails for any reason (bad range, unsupported attribute, `MMU_ERR_NOMEM` from a table allocation, or no free registry slot), everything allocated during this pass is freed immediately through the free hook and the call returns the error; the address space is left exactly as it was before the call.
2. **Commit.** Using only the resources reserved in pass 1, install every table descriptor, leaf mapping, and demotion split, and register each newly linked table. Because all memory and registry slots were already reserved and all checks already passed, this pass cannot fail with `MMU_ERR_NOMEM`, `MMU_ERR_OVERLAP`, `MMU_ERR_NOTMAPPED` or `MMU_ERR_UNSUPPORTED` — there is nothing left to unwind.

The "no partial mapping on failure" guarantee is therefore a direct consequence of the two-pass structure rather than a per-step undo log: a failing call never reaches pass 2, and a call that reaches pass 2 always finishes. After any failing call — including one that would have required a leaf demotion, newly allocated intermediate tables, or newly installed leaf mappings — the address space (mappings, attributes, and existing table structure) is observationally equivalent to its state immediately before the call. This is the Phase 1 contract for `mmu_map_range()`, `mmu_unmap_range()` and `mmu_protect_range()`.

### Synchronization

Two distinct pieces of hardware state must be brought up to date after a successful pass 2, and they cover different address ranges:

1. **Page-table memory visibility.** The table entries the walker just wrote live in ordinary memory, addressed through each table's `virt` pointer (the root, or a `struct mmu_table_ref.virt`). Before hardware can walk them, that memory must be made visible per the target's cache/ordering rules. This operates on the *table storage addresses*, not on the mapped range.
2. **TLB / instruction-stream invalidation.** Any translation — and, for executable mappings, any cached instruction fetch — for the *mapped virtual-address range* the caller passed to the API call must be invalidated so subsequent accesses observe the new mapping.

The existing ARM helper `mmu_page_table_flush(start, stop)` in `bios/arch/arm/cache_armv7.c` already performs (1) for the byte range `[start, stop)` of page-table memory (`flush_dcache_range()`), followed by a whole-TLB invalidate (`v7_inval_tlb()`) for (2). Its `start`/`stop` arguments are page-table *storage* addresses, not the mapped VA range being changed, and its TLB invalidation is currently whole-TLB rather than range-based; the walker calls it accordingly — with the table-memory range it just wrote — and must not be described, or extended, as though `start`/`stop` were the mapped range.

The generic walker performs both kinds of synchronization itself, once per successful call rather than once per leaf write, using: the set of table-memory byte ranges it wrote during pass 2, and the single top-level `va`/`size` the caller requested. It hands both to backend-provided synchronization operations as part of the entry-op contract; a backend that can only invalidate the whole TLB (like the current ARMv7 backend) is free to do so, while a backend with range-based TLB invalidation can do less work. Because synchronization happens inside the range calls, callers never need a separate sync call in the common path.

`mmu_sync_all(void *root)` is the one remaining public synchronization entry point, and is deliberately coarse: given the root of a tree, it cleans/flushes all page-table memory reachable from that `root` (walking through registered and adopted tables — see Table Ownership And Resolution) and performs a full TLB (and, where applicable, I-cache/branch-predictor) invalidate for the whole address space. It takes `root` explicitly, like every other range function, rather than assuming a single active tree: the layer has no global "current root" concept. It exists only for boundary cases outside the range API's own bookkeeping — for example, immediately after `virt_mmu_bootstrap()` hands off a statically-built table to the layer, or after a caller splices a pre-existing/static table into the tree directly instead of going through `mmu_map_range()`. It is not needed, and should not be called, after an ordinary successful `mmu_map_range()`/`mmu_unmap_range()`/`mmu_protect_range()` call. `mmu_sync_range()` is not part of the public API: every use case it would have served is now covered by the synchronization built into the range calls themselves.

### Attributes

Backends declare what they can represent through the `MMU_ARCH_HAS_*` capability defines in the arch header (see Address Model And Attributes). The walker's validate pass (pass 1, above) checks every requested attribute bit against the current backend's capability bits before pass 2 runs:

- A requested attribute the backend cannot represent at all fails the whole call with `MMU_ERR_UNSUPPORTED`. The layer never silently drops or weakens a security- or correctness-relevant attribute — execute permission, user/supervisor access, memory type, shareability — to make a request "succeed".
- A backend may still *normalize* a request internally — for example, folding a requested memory type onto the closest hardware type it implements — but only where the two are genuinely equivalent for that backend; that is a backend implementation detail, not attribute-dropping, and must not change caller-visible permission or memory-type semantics.
- Different m68k MMUs expose different protection capabilities: the 68030 and 68040/68060 descriptor formats do not share one bit set, so backends must not be lumped together as "m68k has no execute/user bits". Each backend defines its own `MMU_ARCH_HAS_*` set from its actual descriptor format, and callers that need a cross-backend guarantee test the capability bits rather than assuming anything from the CPU family name.

## Error Codes

```c
#define MMU_OK              0
#define MMU_ERR_NOMEM        1   /* table allocation failed */
#define MMU_ERR_INVAL         2   /* misaligned va/pa, zero-length size, or malformed attrs */
#define MMU_ERR_OVERLAP       3   /* mmu_map_range(): part of the range is already mapped */
#define MMU_ERR_NOTMAPPED     4   /* mmu_unmap_range()/mmu_protect_range(): part of the range is not mapped */
#define MMU_ERR_UNSUPPORTED   5   /* a requested attribute is not representable on this backend */
```

`MMU_ERR_OVERLAP` and `MMU_ERR_NOTMAPPED` are the two sides of the same rule: the layer never guesses at partial ranges, whether the surprise is an existing mapping (`mmu_map_range()`) or a hole (`mmu_unmap_range()`/`mmu_protect_range()`). Boot-time callers may treat `MMU_ERR_NOMEM` as fatal (panic); `ioremap`-style callers propagate all of these errors. See Transactional Semantics for exactly what state failure leaves behind.

## Configuration And Build

Add `CONF_WITH_MMU_MAINT` under `Kconfig.machine`, defaulting to enabled where a backend exists. For this issue: `CONF_WITH_MMU_MAINT` is `y` when `CONF_WITH_ARM_PMMU && (MACHINE_RPI || MACHINE_VIRT_ARM)`.

Add `CONF_MMU_PHYS_64` under `Kconfig.machine`, always defined, default `n`; it widens `phys_addr_type` to 64 bits and is what the future LPAE/highmem option will turn on.

Add the ARM short-descriptor backend selection. Build `bios/mmu_walk.o` only when the option is set; build the backend object when its CPU format is selected. Shared code is compiled only when the option is on, so existing non-MMU targets are unaffected.

Do not add per-target defaults to `include/config.h`; the feature options follow the existing convention (always defined, `0` or `1`, tested with `#if`).

## Initialization Flow

1. Machine boot code sets the table allocator with `mmu_set_table_allocator()` (static pool now, real page allocator later).
2. If boot code hands the layer a table built outside the range API — the statically-built result of `virt_mmu_bootstrap()`, or a spliced-in static table registered with `mmu_table_adopt()` — it calls `mmu_sync_all(root)` once to bring hardware state into agreement with that table before relying on the API's own per-call synchronization for anything after.
3. Boot-time mapping code expresses the identity map and any text-protection ranges through `mmu_map_range()`/`mmu_protect_range()`.
4. Dynamic callers (later: PCI/ioremap) call `mmu_map_range()`/`mmu_unmap_range()` at runtime; synchronization happens automatically as part of each successful call (see Synchronization).

The pre-MMU `virt_mmu_bootstrap()` stays exactly as it is: it is the enabling step that lets C code and this layer run, and it remains architecture-specific and outside the maintenance layer.

## Testing And Validation

Reference validation is QEMU ARM `virt`:

- `make virt-arm_defconfig && make` builds.
- Boot to the GEM desktop under QEMU (per the ptos-smoketest skill) to confirm the layer is boot-neutral — this remains the general regression test.
- A dedicated runtime check exercises the operations the abstraction actually needs to prove. `virt_mmu_bootstrap()` already establishes valid short-descriptor section mappings across the whole 4 GiB VA space, so a test that simply maps an arbitrary "free" region is likely to collide with an existing mapping rather than testing anything new. Instead:
  1. Choose a known-safe existing section mapping from the `virt-arm` bootstrap table — one not required for the boot path past this point (for example an unused RAM or scratch MMIO section) — and confirm via the backend's own accessors that it starts as a single large leaf (section).
  2. Call `mmu_protect_range()` (or `mmu_unmap_range()`) on a sub-range small enough to force section-to-page demotion, and confirm the call returns `MMU_OK`.
  3. Verify the pages *outside* the requested sub-range still read back with the original mapping and attributes, proving the demotion populated the child table correctly (Leaf Demotion, step 3).
  4. Verify the pages *inside* the requested sub-range now carry the new attributes (or are unmapped, for the unmap variant).
  5. Access memory through both the changed and unchanged pages after each mutation, not only at the end, to prove TLB/cache synchronization actually took effect — a stale TLB entry would otherwise let this check pass despite a wrong descriptor.
  6. Remap or restore the original attributes over the demoted sub-range through `mmu_map_range()`/`mmu_protect_range()`.
  7. Where the sequence leaves a now-empty allocator-owned child table (for example after unmapping the whole demoted range instead of restoring it), confirm `mmu_unmap_range()` freed it through the free hook.
- Exercise `mmu_protect_range()` on the low RAM window through the shared API (the same operation Raspberry Pi's text-protect performs).

Raspberry Pi migration validation is a build check:

- `make rpi2_defconfig && make` builds with `init_mmu()`/`raspi_mmu_protect_range()` rewritten on the new API, with text-protect behaviour unchanged: `init_mmu()` keeps building `text_protect_l2` and its coarse descriptor exactly as today, then registers it once with `mmu_table_adopt()` so `mmu_protect_range()` can resolve and edit it (see Table Ownership And Resolution); the reserved top-of-RAM megabyte keeps its distinct `MMU_ATTR_STRONGLY_ORDERED` mapping (see Address Model And Attributes). Raspberry Pi is not QEMU-boot-tested here (QEMU's Pi support is not the primary reference environment per readme.md), so runtime verification of the migrated code is the shared protect path exercised on virt-arm.

Other build checks:

- `make gitready` passes.

m68k verification belongs to phase 2 (virt-m68k / Hatari). Unit-style tests are not established for BIOS code in this tree, so validation is build checks plus QEMU runtime smoke tests and diagnostic logging where it fits existing `KDEBUG`/`KINFO` patterns.

## Follow-Up Integration

- **Phase 2 (m68k):** the generic walker and its geometry-driven leaf/demotion logic are reused as-is; page-table geometry (level count, granule sizes) is expressed as data per Level Geometry As Data. Descriptor encoding helpers may be shared between 68030 and 68040/68060 where their hardware formats genuinely permit it, but this is not committed to up front — the two CPUs' descriptor formats differ in more than level geometry (available cache/protection bits and descriptor capabilities also differ). `bios/arch/m68k/mmu_short.h` + `.c` may therefore end up as one shared backend or as separate 68030 / 68040-68060 variants; port 68040's assembler `create_table` to C on the new layer, and express the 68030 static tree through the layer, whichever backend split that phase settles on.
- **Phase 3 (LPAE/highmem, issue #76):** LPAE backend with 64-bit `phys_addr_type`, dynamic device mappings, and `ioremap()`/`iounmap()`; move `pci_phys_to_virt()` and PCI resource mapping onto `mmu_map_range()`.
- Later: AArch64 and x86-64 backends behind the same contract.

## Risks

- The geometry-driven walker must handle level counts from 2 (ARM short-desc) to 4 (x86-64) without per-level code explosion; the level-descriptor array plus entry ops is the mitigation.
- Overlap and hole handling are strict (`MMU_ERR_OVERLAP` for map, `MMU_ERR_NOTMAPPED` for unmap/protect) to keep the walker deterministic; callers must unmap before re-mapping with different attributes, and must not assume best-effort behaviour over a range containing holes.
- The two-pass (validate+reserve, then commit) walker structure that gives the transactional guarantee doubles the walk cost of every range call; this is accepted for Phase 1 given BIOS/kernel-time call frequency, since this is not a hot path.
- Table PA-to-VA resolution and ownership tracking both live in the walker's internal registry (see Table Ownership And Resolution), not in hardware descriptor bits — pTOS's own ARM short-descriptor coarse-table descriptor, for example, has only SBZ ("should be zero") and one implementation-defined bit, neither a general-purpose software field, so the registry approach avoids relying on a spare bit that may not exist in every backend's format. The registry has bounded capacity, sized for the number of live intermediate tables (small at BIOS/kernel time); pass 1 reserving a slot per new table means a full registry surfaces as `MMU_ERR_NOMEM` during validate + reserve, never as an unwind failure during commit.
- Any pre-existing/static table a port splices into the tree must be registered once via `mmu_table_adopt()` before a range call walks through it; forgetting to adopt one is a port bug the walker cannot detect in advance (see the last bullet of Table Ownership And Resolution) — this is a discipline the Raspberry Pi and 68030 migrations need to get right.
- Attribute semantics differ across architectures, and even within one CPU family (68030 vs. 68040/68060 protection bits differ); backends expose what they can represent via `MMU_ARCH_HAS_*` capability bits, and the walker rejects unrepresentable security-relevant attributes with `MMU_ERR_UNSUPPORTED` rather than silently weakening a request.
- The pre-MMU bootstrap window is architecture-specific by nature and must stay out of the shared layer; the spec deliberately keeps `virt_mmu_bootstrap()` untouched.
- `int` is 16 bits on m68k and 32 bits on ARM; the API must use `portab.h` fixed-width types and suffix constants that must survive on m68k.
