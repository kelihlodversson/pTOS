/*
 * startup.c - x86-64 EFI entry point: boot services, higher-half relocation
 *
 * Milestones 1-2 of the x86-64 port (issues #330, #331): get from UEFI's
 * own boot environment to code running at this kernel's higher-half
 * virtual address, with just enough diagnostics over COM1 to prove it
 * happened, then give the CPU a real GDT/TSS/IDT so exceptions are
 * caught and reported instead of triple-faulting. This does not yet call
 * into the shared bios/bdos/fs/util pipeline every other machine's
 * startup.S hands off to (see the ARCH_X86_64 branch of the top level
 * Makefile's $(EMUTOS_IMG) rule for why) -- that is still later work.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "portab.h"
#include "efi.h"
#include "earlycon.h"
#include "io.h"
#include "pgtable.h"
#include "gdt.h"
#include "idt.h"
#include "pmem.h"

/* This image's own boot-time stack. Used only from the higher-half jump
 * onward, replacing whatever transient stack UEFI itself was using; 64 KiB
 * is generous for a boot stub that does not yet call into anything deep. */
#define BOOT_STACK_BYTES 65536
static UBYTE boot_stack[BOOT_STACK_BYTES] __attribute__((aligned(16)));

/* Upper bound on this image's own size (code, data, bss -- including the
 * stack above and the page tables pgtable.c allocates), used to size the
 * identity/higher-half page table window x86_64_build_page_tables() sets
 * up. Checked against EFI_LOADED_IMAGE_PROTOCOL.ImageSize below rather
 * than trusted blindly. */
#define IMAGE_SPAN_BYTES (4 * 1024 * 1024)

/*
 * A copy of the EFI memory map GetMemoryMap() returned, taken just before
 * ExitBootServices() -- the buffer GetMemoryMap() itself filled in is
 * EFI_LOADER_DATA pool memory that, while it happens to remain valid after
 * ExitBootServices() too, has no promise from the spec that it will; this
 * static copy (part of the image's own bss, always mapped) is what gets
 * handed to x86_64_pmem_init() a few lines further down efi_main().
 * Sized from X86_64_EFI_MAP_BYTES (pmem.h), which pmem.c's MAX_REGIONS is
 * in turn derived from -- see that constant's own comment for why both
 * need to agree on one bound rather than each guess independently.
 */
static UBYTE saved_memory_map[X86_64_EFI_MAP_BYTES] __attribute__((aligned(8)));
static UQUAD saved_map_size;
static UQUAD saved_descriptor_size;

extern void x86_64_relocate_higher_half(UQUAD high_half_target, UQUAD cr3_value,
                                         void *new_stack_top) NORETURN;

void NORETURN x86_64_higher_half_main(void);

/* No libc, no util/string.c linked into this standalone boot object list
 * yet (see the top level Makefile's ARCH_X86_64 branch) -- a small local
 * byte copy loop rather than relying on GCC recognizing and inlining a
 * memcpy() idiom under -ffreestanding. */
static void copy_bytes(void *dst, const void *src, UQUAD n)
{
    UBYTE *d = (UBYTE *)dst;
    const UBYTE *s = (const UBYTE *)src;
    UQUAD i;

    for (i = 0; i < n; i++)
        d[i] = s[i];
}

static NORETURN void hang(void)
{
    for (;;) {
        x86_64_cli();
        x86_64_halt();
    }
}

static NORETURN void panic(const char *msg)
{
    earlycon_puts("panic: ");
    earlycon_puts(msg);
    earlycon_puts("\n");
    hang();
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable);

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image;
    EFI_GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_STATUS status;
    UQUAD image_base;
    UQUAD mapped_base;
    UQUAD cr3;
    UQUAD entry_high;
    UQUAD stack_top_high;
    void *map_buffer;
    UQUAD map_size;
    UQUAD map_key;
    UQUAD descriptor_size;
    ULONG descriptor_version;
    int retry;

    earlycon_init();
    earlycon_puts("pTOS x86-64: EFI entry reached\n");

    /*
     * Checked up front, while EFI's own IDT (and this early, its own
     * earlycon-independent COM1-agnostic firmware diagnostics) can still
     * catch a mistake in this check itself: x86_64_build_physmap() further
     * down needs 1 GiB pages, and by the time it runs neither our IDT nor
     * EFI's is in a good position to report why it just faulted (see
     * pgtable.h). Real hardware has had this since ~2010; only some
     * conservative default virtual CPU models still lack it.
     */
    if (!x86_64_cpu_has_1g_pages())
        panic("CPU lacks 1 GiB page support (CPUID.80000001H:EDX.Page1GB)");

    status = bs->HandleProtocol(ImageHandle, &loaded_image_guid, (void **)&loaded_image);
    if (status & EFI_ERROR_BIT)
        panic("HandleProtocol(LoadedImage) failed");

    image_base = (UQUAD)(uintptr_t)loaded_image->ImageBase;
    if (loaded_image->ImageSize > IMAGE_SPAN_BYTES)
        panic("image larger than the mapped boot window");
    earlycon_puts("pTOS x86-64: image base obtained\n");

    /*
     * GetMemoryMap() with a too-small (here, zero) buffer always returns
     * EFI_BUFFER_TOO_SMALL and fills in the size actually needed -- a
     * normal, expected result, not a failure. AllocatePool() an
     * appropriately sized buffer before fetching the real map: the map can
     * grow by the time of the real call below (any allocation, including
     * this one, can add a descriptor), so pad generously rather than
     * reprobing in a loop.
     */
    map_size = 0;
    status = bs->GetMemoryMap(&map_size, NULL, &map_key, &descriptor_size, &descriptor_version);
    if (!(status & EFI_ERROR_BIT) || map_size == 0)
        panic("GetMemoryMap probe did not report a required size");
    map_size += 8 * descriptor_size;

    status = bs->AllocatePool(EFI_LOADER_DATA, map_size, &map_buffer);
    if (status & EFI_ERROR_BIT)
        panic("AllocatePool(memory map) failed");

    for (retry = 0; ; retry++) {
        UQUAD this_map_size = map_size;

        status = bs->GetMemoryMap(&this_map_size, (EFI_MEMORY_DESCRIPTOR *)map_buffer,
                                   &map_key, &descriptor_size, &descriptor_version);
        if (status == EFI_BUFFER_TOO_SMALL) {
            /* The map grew past the headroom padded in above (or a
             * previous iteration's). Legitimate, not fatal: reallocate a
             * buffer sized for the current map (with fresh headroom) and
             * retry, rather than treating this the same as a real error. */
            bs->FreePool(map_buffer);
            map_size = this_map_size + 8 * descriptor_size;
            status = bs->AllocatePool(EFI_LOADER_DATA, map_size, &map_buffer);
            if (status & EFI_ERROR_BIT)
                panic("AllocatePool(memory map) failed");
            if (retry >= 8)
                panic("GetMemoryMap kept outgrowing its buffer");
            continue;
        }
        if (status & EFI_ERROR_BIT)
            panic("GetMemoryMap failed");

        /*
         * Snapshot the map into this image's own bss before every
         * ExitBootServices() attempt (not just the one that ends up
         * succeeding): a retry re-fetches a possibly different map, and
         * this must reflect whichever one the eventually-successful
         * ExitBootServices() call actually used.
         */
        if (this_map_size > X86_64_EFI_MAP_BYTES)
            panic("EFI memory map exceeds the saved buffer");
        copy_bytes(saved_memory_map, map_buffer, this_map_size);
        saved_map_size = this_map_size;
        saved_descriptor_size = descriptor_size;

        status = bs->ExitBootServices(ImageHandle, map_key);
        if (!(status & EFI_ERROR_BIT))
            break;

        /* Per the UEFI spec, ExitBootServices() can fail with a stale
         * MapKey if the memory map changed since GetMemoryMap() returned
         * it; the documented recovery is to fetch the map again. */
        if (retry >= 8)
            panic("ExitBootServices failed repeatedly");
    }

    /*
     * From here on there is no pTOS IDT, and the page tables built below
     * unmap EFI's own IDT and interrupt handlers along with the rest of
     * firmware memory: any interrupt taken after this point until #331
     * gives this arch real exception/interrupt handling would vector
     * through unmapped memory and triple-fault. ExitBootServices() does
     * not itself guarantee interrupts are off, so disable them explicitly
     * before doing anything else.
     */
    x86_64_cli();
    earlycon_puts("pTOS x86-64: boot services exited\n");

    cr3 = x86_64_build_page_tables(image_base, IMAGE_SPAN_BYTES, &mapped_base);
    earlycon_puts("pTOS x86-64: page tables built\n");

    /*
     * Must happen here, still running at this image's actual (low) load
     * address, not after the higher-half jump below: x86_64_build_physmap()
     * stores &physmap_pdpt (pgtable.c) into a page-table entry as a
     * physical address, and a file-static array's address is a position-
     * independent (RIP-relative) computation -- it naturally yields
     * whichever alias is CURRENTLY executing, low here, high once running
     * via x86_64_higher_half_main() post-jump. Evaluated post-jump, that
     * same expression is a huge canonical high-half address that has
     * nothing to do with this array's physical location, and storing it as
     * a page-table frame number raises #PF with the reserved-bit flag set
     * (its high bits fall outside the CPU's physical address width) -- this
     * ordering is not just tidiness, an earlier version of this code got
     * it wrong exactly this way.
     */
    /*
     * Two independent reserved ranges, not one spanning both: this
     * image's own load span, and separately physical [0, 2 MiB), which
     * x86_64_map_low_vectors() (called later, post-jump) identity-maps
     * for the low system-vector area (#349). EFI typically loads this
     * image well above address 0 (mapped_base is usually several MiB in),
     * so collapsing the gap between the two into one reserved block would
     * falsely exclude a large amount of genuinely free memory.
     */
    x86_64_pmem_init(saved_memory_map, saved_map_size, saved_descriptor_size,
                      mapped_base, mapped_base + IMAGE_SPAN_BYTES + X86_64_PAGE_2M_SIZE,
                      0, X86_64_PAGE_2M_SIZE);
    earlycon_puts("pTOS x86-64: physical memory map parsed\n");

    x86_64_build_physmap(x86_64_pmem_highest_addr());
    earlycon_puts("pTOS x86-64: physical memory direct map installed, relocating to higher half\n");

    /*
     * &x86_64_higher_half_main and &boot_stack[...] are this image's
     * actual (low) runtime addresses here: the PE loader already applied
     * each pointer's base relocation to account for wherever it loaded us,
     * the same way it did for loaded_image->ImageBase above. Translated
     * via x86_64_low_to_high() (pgtable.c), which remembers the same
     * mapped_base x86_64_build_page_tables() just returned.
     */
    entry_high = x86_64_low_to_high((UQUAD)(uintptr_t)&x86_64_higher_half_main);
    stack_top_high = x86_64_low_to_high((UQUAD)(uintptr_t)&boot_stack[BOOT_STACK_BYTES]);

    x86_64_relocate_higher_half(entry_high, cr3, (void *)(uintptr_t)stack_top_high);

    /* Not reached: x86_64_relocate_higher_half() never returns. */
    hang();
}

static void print_hex_line(const char *label, UQUAD value)
{
    earlycon_puts(label);
    earlycon_puthex(value);
    earlycon_puts("\n");
}

void NORETURN x86_64_higher_half_main(void)
{
    earlycon_puts("pTOS x86-64 EFI boot stub: alive in the higher half\n");

    x86_64_gdt_init();
    earlycon_puts("pTOS x86-64: GDT/TSS loaded\n");

    x86_64_idt_init();
    earlycon_puts("pTOS x86-64: IDT loaded, exceptions armed\n");

    /*
     * Safe now, and not before: x86_64_idt_init() just translated the one
     * remaining low-address pointer table this image depends on
     * (exception_stub[], via x86_64_low_to_high()) into the gates it
     * installed, so nothing still needs the identity mapping to keep
     * working. Frees that low canonical address range for #334's future
     * ILP32 user processes (see #343 and #344's address-space split).
     */
    x86_64_drop_identity_map();
    earlycon_puts("pTOS x86-64: identity mapping dropped\n");

    /*
     * Must follow the drop above, not precede it: both target PML4 slot 0
     * (see x86_64_map_low_vectors()'s own comment). Gives the shared
     * core's generic bios_init() (bios/bios.c) somewhere real to write
     * VEC_GEM/VEC_BIOS/VEC_XBIOS -- nothing on this arch reads them back
     * (trap.c dispatches directly, see its own comment), but bios_init()
     * writes through them unconditionally regardless of arch.
     */
    x86_64_map_low_vectors();
    earlycon_puts("pTOS x86-64: low system-vector area mapped\n");

    /*
     * A discriminating check, not just "didn't crash": VEC_TRAP1 (0x84,
     * bios/vectors.h) is one of the specific offsets bios_init() and the
     * trap dispatch path (#349) actually read/write, not just an
     * arbitrary address within [0, 2 MiB). Writing a known sentinel there
     * and reading it back proves this exact offset is really backed by
     * the newly mapped page, not e.g. off by a PML4/PDPT/PD index
     * somewhere upstream. x86_64_map_low_vectors() already zeroed it, so
     * this also incidentally confirms the zero loop reached this offset.
     */
    {
        /* GCC's -Warray-bounds statically flags dereferencing a small
         * literal address as "likely null" -- true in general, but this
         * one is deliberately backed by the mapping just installed above;
         * routing it through a volatile intermediate (rather than a bare
         * cast of the 0x84 literal) hides the constant from that analysis
         * without weakening the check itself. */
        volatile UQUAD vec_trap1_addr = 0x84;
        volatile UQUAD *vec_trap1 = (volatile UQUAD *)(uintptr_t)vec_trap1_addr;
        const UQUAD sentinel = 0x1122334455667788ULL;

        if (*vec_trap1 != 0)
            panic("low system-vector area: VEC_TRAP1 not zeroed");
        *vec_trap1 = sentinel;
        if (*vec_trap1 != sentinel)
            panic("low system-vector area verification failed");
        *vec_trap1 = 0;
        earlycon_puts("pTOS x86-64: low system-vector area verified\n");
    }

    print_hex_line("pTOS x86-64: physical memory free=", x86_64_pmem_free_bytes());
    print_hex_line("  highest_addr=", x86_64_pmem_highest_addr());

    /*
     * A discriminating check, not just "didn't crash": allocate a fresh
     * page nothing else has touched, write distinct sentinels through its
     * direct-map alias at both ends of the page, and read them back. A
     * page-table bug (wrong PDPT index, a stale/aliased entry, ...) that
     * merely happened to leave the mapping "present" without actually
     * reaching the intended physical page would still crash or read back
     * silently wrong here, unlike a check that only confirms the access
     * did not fault.
     */
    {
        UQUAD phys = x86_64_pmem_alloc_pages(1);
        volatile UQUAD *virt = (volatile UQUAD *)(uintptr_t)(X86_64_PHYS_MAP_BASE + phys);
        const UQUAD sentinel_lo = 0x1122334455667788ULL;
        const UQUAD sentinel_hi = 0x99AABBCCDDEEFF00ULL;
        const UQUAD last_word = (X86_64_PAGE_SIZE / sizeof(UQUAD)) - 1;

        virt[0] = sentinel_lo;
        virt[last_word] = sentinel_hi;
        if (virt[0] != sentinel_lo || virt[last_word] != sentinel_hi)
            panic("physical memory direct map verification failed");
        earlycon_puts("pTOS x86-64: physical memory direct map verified\n");
    }

    hang();
}
