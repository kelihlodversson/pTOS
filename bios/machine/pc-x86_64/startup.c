/*
 * startup.c - x86-64 EFI entry point: boot services, higher-half relocation
 *
 * Milestone 1 of the x86-64 port (issue #330): get from UEFI's own boot
 * environment to code running at this kernel's higher-half virtual
 * address, with just enough diagnostics over COM1 to prove it happened.
 * This does not yet call into the shared bios/bdos/fs/util pipeline every
 * other machine's startup.S hands off to (see the ARCH_X86_64 branch of
 * the top level Makefile's $(EMUTOS_IMG) rule for why) -- that begins once
 * exception/interrupt handling (#331) exists.
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

/* GetMemoryMap()'s own EFI_MEMORY_DESCRIPTOR array; sized generously so a
 * single call fills it without an intervening AllocatePool() call, which
 * would otherwise perturb the very map being fetched and invalidate the
 * MapKey ExitBootServices() needs (see the retry loop in efi_main()). */
#define MEMORY_MAP_BUFFER_BYTES 16384
static UBYTE memory_map_buffer[MEMORY_MAP_BUFFER_BYTES] __attribute__((aligned(8)));

/* This image's own boot-time stack. Used only from the higher-half jump
 * onward, replacing whatever transient stack UEFI itself was using; 64 KiB
 * is generous for a boot stub that does not yet call into anything deep. */
#define BOOT_STACK_BYTES 65536
static UBYTE boot_stack[BOOT_STACK_BYTES] __attribute__((aligned(16)));

/* Upper bound on this image's own size (code, data, bss -- including the
 * tables and stack above), used to size the identity/higher-half page
 * table window x86_64_build_page_tables() (pgtable.c) sets up. Checked
 * against EFI_LOADED_IMAGE_PROTOCOL.ImageSize below rather than trusted
 * blindly. */
#define IMAGE_SPAN_BYTES (4 * 1024 * 1024)

extern void x86_64_relocate_higher_half(UQUAD high_half_target, UQUAD cr3_value,
                                         void *new_stack_top) NORETURN;

void NORETURN x86_64_higher_half_main(void);

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
    UQUAD low_addr;
    UQUAD high_target;
    int retry;

    earlycon_init();
    earlycon_puts("pTOS x86-64: EFI entry reached\n");

    status = bs->HandleProtocol(ImageHandle, &loaded_image_guid, (void **)&loaded_image);
    if (status & EFI_ERROR_BIT)
        panic("HandleProtocol(LoadedImage) failed");

    image_base = (UQUAD)(uintptr_t)loaded_image->ImageBase;
    if (loaded_image->ImageSize > IMAGE_SPAN_BYTES)
        panic("image larger than the mapped boot window");
    earlycon_puts("pTOS x86-64: image base obtained\n");

    for (retry = 0; ; retry++) {
        UQUAD map_size = sizeof(memory_map_buffer);
        UQUAD map_key = 0;
        UQUAD descriptor_size = 0;
        ULONG descriptor_version = 0;

        status = bs->GetMemoryMap(&map_size, (EFI_MEMORY_DESCRIPTOR *)memory_map_buffer,
                                   &map_key, &descriptor_size, &descriptor_version);
        if (status & EFI_ERROR_BIT)
            panic("GetMemoryMap failed");

        status = bs->ExitBootServices(ImageHandle, map_key);
        if (!(status & EFI_ERROR_BIT))
            break;

        /* Per the UEFI spec, ExitBootServices() can fail with a stale
         * MapKey if the memory map changed since GetMemoryMap() returned
         * it; the documented recovery is to fetch the map again. */
        if (retry >= 8)
            panic("ExitBootServices failed repeatedly");
    }

    earlycon_puts("pTOS x86-64: boot services exited\n");

    cr3 = x86_64_build_page_tables(image_base, IMAGE_SPAN_BYTES, &mapped_base);
    earlycon_puts("pTOS x86-64: page tables built, relocating to higher half\n");

    /*
     * &x86_64_higher_half_main is this image's actual (low) runtime address
     * here: the PE loader already applied this pointer's base relocation to
     * account for wherever it loaded us, the same way it did for
     * loaded_image->ImageBase above. Subtracting mapped_base -- the 2 MiB-
     * aligned base x86_64_build_page_tables() actually mapped, not the
     * unaligned image_base -- turns that into this image's byte offset
     * from the start of the mapped window; adding the kernel's higher-half
     * virtual base turns that offset into the matching high-half virtual
     * address, which the page tables just built also map to the same
     * physical page.
     */
    low_addr = (UQUAD)(uintptr_t)&x86_64_higher_half_main;
    high_target = X86_64_KERNEL_VIRT_BASE + (low_addr - mapped_base);

    x86_64_relocate_higher_half(high_target, cr3, &boot_stack[BOOT_STACK_BYTES]);

    /* Not reached: x86_64_relocate_higher_half() never returns. */
    hang();
}

void NORETURN x86_64_higher_half_main(void)
{
    earlycon_puts("pTOS x86-64 EFI boot stub: alive in the higher half\n");
    hang();
}
