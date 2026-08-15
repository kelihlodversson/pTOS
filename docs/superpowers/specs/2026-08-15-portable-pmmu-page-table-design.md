# Portable pMMU Page-Table Maintenance Abstraction Design

## Context

Every pTOS target that needs an MMU sets up its page tables with machine- and CPU-specific code, none of which is usable from shared C code:

- **68030** (`bios/pmmu030.c`): a static, compile-time tree (`struct pmmutable`) copied into protected RAM via `memcpy`. No runtime maintenance.
- **68040/68060** (`bios/arch/m68k/68040_pmmu.S`): hand-written assembler that walks root/pointer/page tables and allocates tables from a `next_free` pool (`create_table(logical, physical, size, mode)`).
- **ARMv6/v7 Raspberry Pi** (`bios/machine/raspi/memory.c`): an L1 section table plus a coarse page table used only for first-megabyte text protection. `raspi_mmu_protect_range()` is the only runtime update.
- **ARMv7 QEMU virt** (`bios/machine/virt-arm/virt_mmu.c`): a static short-descriptor bootstrap run before BSS is cleared.

There is no `phys_addr_t`/`virt_addr_t` (addresses are plain `ULONG`), no page-table-page allocator, and the PCI layer already hand-rolls `bus_to_phys()`/`phys_to_bus()` translation. Issue #76 (ARM32 LPAE and high-address MMIO) needs physical addresses above 4 GiB on a 32-bit ARM build, and needs dynamic device mappings (`ioremap`/`iounmap`).

This issue (tracked as #201) designs and builds a small C-level abstraction for page-table *maintenance* that is shared across the architectures pTOS targets: 68030/68040/68060, ARM32, and later AArch64 and x86/x86-64.

## Goals

- Provide a range-based C API for mapping, unmapping, protecting and syncing virtual address ranges.
- Back the API with a generic walker that selects the largest fitting page level automatically.
- Isolate hardware descriptor formats per architecture behind a small header contract selected through the existing `arch/`/`machine/` vpath mechanism.
- Gate feature differences (page-table level count, page sizes, pointer width, supported attributes) via typedefs and `MMU_ARCH_*` defines.
- Support physical addresses wider than the virtual address space on ARM32 (highmem/LPAE), while keeping virtual addresses 32-bit there.
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
- Do not remove the pre-MMU `virt_mmu_bootstrap()`; it runs before BSS and C code exist and its job is to make the layer runnable.
- Do not port the 68030/68040 setup code in this issue; that is follow-up phase 2 (m68k short-format backend).
- Do not implement ARM LPAE, AArch64 or x86-64 backends in this issue; the abstraction must leave room for them, not build them.

## Architecture

Three component kinds, following existing pTOS conventions (`arch/`/`machine/` subdirectories, vpath selection, unique object basenames):

1. **Shared API** (`include/mmu.h`): types, attribute flags, error codes, and the range-based API.
2. **Generic walker** (`bios/mmu_walk.c`): shared C that splits a range into the largest fitting granules per level, walks down creating or removing intermediate tables, and installs or clears leaf entries. It never touches hardware descriptor bits itself.
3. **Architecture backends**: one header contract plus one implementation per descriptor format, selected by vpath:
   - `bios/arch/arm/mmu_shortdesc.h` + `.c` — VMSAv6/v7 short-descriptor (reference backend, this issue)
   - `bios/arch/m68k/mmu_short.h` + `.c` — m68k short-format (follow-up phase 2; covers 68030 *and* 68040/68060, because the entry format is the same and only the level geometry differs)
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
| 68030 | 3 | 256 MiB, 16 MiB, 1 MiB | 1 MiB (page descriptor) |
| 68040/68060 | 3 | 256 MiB, 2 MiB, 4 KiB | 4 KiB |
| LPAE / AArch64 / x86-64 | 3-4 | ... | 1 GiB, 2 MiB, 4 KiB |

The walker needs no knowledge of which CPU it is on beyond the level descriptors and the entry ops.

## Address Model And Attributes

`include/mmu.h` defines:

```c
typedef ULONG virt_addr_t;          /* becomes 64-bit on AArch64/x86-64 */
#if CONF_MMU_PHYS_64
typedef unsigned long long phys_addr_t;
#else
typedef ULONG phys_addr_t;
#endif
```

`phys_addr_t` grows to 64 bits when the configuration needs it (`CONF_MMU_PHYS_64`, set by the future LPAE/highmem option), so ARM32 can represent physical addresses above 4 GiB now even though virtual pointers stay 32-bit.

Attributes are a small cross-architecture bitmask (`mmu_attr_t`) that each backend maps to hardware bits:

- memory type: `MMU_ATTR_DEVICE`, `MMU_ATTR_WRITE_THROUGH`, `MMU_ATTR_WRITE_BACK`
- permissions: `MMU_ATTR_READ`, `MMU_ATTR_WRITE`, `MMU_ATTR_EXEC`, `MMU_ATTR_USER`
- `MMU_ATTR_GLOBAL`, `MMU_ATTR_SHAREABLE`

Feature gating lives in the backend header as `MMU_ARCH_*` defines: `MMU_ARCH_LEVELS`, `MMU_ARCH_HAS_EXEC`, `MMU_ARCH_HAS_USER`, `MMU_ARCH_HAS_BIG_PAGES`, `MMU_ARCH_PHYS_BITS`, `MMU_ARCH_TABLE_ALIGN`. Attributes a backend cannot express (for example m68k has no execute bit) are ignored silently by that backend, so callers can share mapping code across architectures.

## API Shape

```c
typedef void *(*mmu_table_alloc_t)(void *cookie);
typedef void (*mmu_table_free_t)(void *table, void *cookie);

void mmu_set_table_allocator(mmu_table_alloc_t alloc, mmu_table_free_t free,
                             void *cookie);

int mmu_map_range(void *root, virt_addr_t va, phys_addr_t pa,
                  size_t size, mmu_attr_t attrs);
int mmu_unmap_range(void *root, virt_addr_t va, size_t size);
int mmu_protect_range(void *root, virt_addr_t va, size_t size,
                      mmu_attr_t attrs);
void mmu_sync_range(virt_addr_t va, size_t size);
void mmu_sync_all(void);
```

- `size` rounds up to the smallest page size; `va` and `pa` must be aligned to the smallest page size.
- `root` is an opaque pointer to the root table, owned by whoever set it up (boot code keeps owning the table memory; the layer only edits its contents).
- `mmu_map_range()` maps at the largest fitting level for each sub-range.
- `mmu_unmap_range()` clears entries and frees now-empty intermediate tables through the free hook.
- `mmu_protect_range()` changes attributes of existing mappings (this is what Raspberry Pi's `raspi_mmu_protect_range()` generalizes).
- `mmu_sync_range()`/`mmu_sync_all()` perform the per-architecture cache and TLB maintenance after table writes (ARM: clean range plus TLB invalidation, building on the existing `mmu_page_table_flush()` in `bios/arch/arm/cache_armv7.c`; m68k: `pflusha`/flush of affected entries).

## Error Codes

```c
#define MMU_OK            0
#define MMU_ERR_NOMEM     1   /* table page allocation failed */
#define MMU_ERR_INVAL     2   /* misaligned va/pa/size, unknown attribute */
#define MMU_ERR_OVERLAP   3   /* an existing mapping is in the way */
```

- `MMU_ERR_OVERLAP` treats overlapping mappings as a caller bug; the layer does not guess.
- On allocation failure mid-walk, the call unwinds atomically: tables created during the call are freed again, so no partial mapping is ever left behind.
- Boot-time callers may treat `MMU_ERR_NOMEM` as fatal (panic); ioremap-style callers propagate the error.

## Configuration And Build

Add `CONF_WITH_MMU_MAINT` under `Kconfig.machine`, defaulting to enabled where a backend exists. For this issue: `CONF_WITH_MMU_MAINT` is `y` when `CONF_WITH_ARM_PMMU && (MACHINE_RPI || MACHINE_VIRT_ARM)`.

Add `CONF_MMU_PHYS_64` under `Kconfig.machine`, always defined, default `n`; it widens `phys_addr_t` to 64 bits and is what the future LPAE/highmem option will turn on.

Add the ARM short-descriptor backend selection. Build `bios/mmu_walk.o` only when the option is set; build the backend object when its CPU format is selected. Shared code is compiled only when the option is on, so existing non-MMU targets are unaffected.

Do not add per-target defaults to `include/config.h`; the feature options follow the existing convention (always defined, `0` or `1`, tested with `#if`).

## Initialization Flow

1. Machine boot code sets the table allocator with `mmu_set_table_allocator()` (static pool now, real page allocator later).
2. Boot-time mapping code expresses the identity map and any text-protection ranges through `mmu_map_range()`/`mmu_protect_range()`.
3. Dynamic callers (later: PCI/ioremap) call `mmu_map_range()`/`mmu_unmap_range()` at runtime; `mmu_sync_range()` is called automatically after each mutation.

The pre-MMU `virt_mmu_bootstrap()` stays exactly as it is: it is the enabling step that lets C code and this layer run.

## Testing And Validation

Reference validation is QEMU ARM `virt`:

- `make virt-arm_defconfig && make` builds.
- Boot to the GEM desktop under QEMU (per the ptos-smoketest skill) to confirm the layer is boot-neutral.
- At boot, map a small device region (for example the virtio PIO/MMIO window) with `mmu_map_range()`, read it back, unmap it, and re-map it, exercising walker create/delete and the sync path.
- Exercise `mmu_protect_range()` on the low RAM window through the shared API (the same operation Raspberry Pi's text-protect performs).

Raspberry Pi migration validation is a build check:

- `make rpi2_defconfig && make` builds with `init_mmu()`/`raspi_mmu_protect_range()` rewritten on the new API, with text-protect behaviour unchanged. Raspberry Pi is not QEMU-boot-tested here (QEMU's Pi support is not the primary reference environment per readme.md), so runtime verification of the migrated code is the shared protect path exercised on virt-arm.

Other build checks:

- `make gitready` passes.

m68k verification belongs to phase 2 (virt-m68k / Hatari). Unit-style tests are not established for BIOS code in this tree, so validation is build checks plus QEMU runtime smoke tests and diagnostic logging where it fits existing `KDEBUG`/`KINFO` patterns.

## Follow-Up Integration

- **Phase 2 (m68k):** `bios/arch/m68k/mmu_short.h` + `.c` short-format backend; port 68040's assembler `create_table` to C on the new layer; express the 68030 static tree through the layer.
- **Phase 3 (LPAE/highmem, issue #76):** LPAE backend with 64-bit `phys_addr_t`, dynamic device mappings, and `ioremap()`/`iounmap()`; move `pci_phys_to_virt()` and PCI resource mapping onto `mmu_map_range()`.
- Later: AArch64 and x86-64 backends behind the same contract.

## Risks

- The geometry-driven walker must handle level counts from 2 (ARM short-desc) to 4 (x86-64) without per-level code explosion; the level-descriptor array plus entry ops is the mitigation.
- Overlap handling is strict (`MMU_ERR_OVERLAP`) to keep the walker deterministic; callers must unmap before re-mapping with different attributes.
- Attribute semantics differ across architectures (e.g. m68k lacks execute and user bits); backends must ignore unrepresentable attributes consistently so shared callers behave identically.
- The pre-MMU bootstrap window is architecture-specific by nature and must stay out of the shared layer; the spec deliberately keeps `virt_mmu_bootstrap()` untouched.
- `int` is 16 bits on m68k and 32 bits on ARM; the API must use `portab.h` fixed-width types and suffix constants that must survive on m68k.
