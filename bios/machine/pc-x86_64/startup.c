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

/*
 * Translates a low (this image's actual, EFI-chosen load address) pointer
 * to its higher-half virtual counterpart: X86_64_KERNEL_VIRT_BASE plus the
 * pointer's byte offset from mapped_base, the 2 MiB-aligned base
 * x86_64_build_page_tables() actually mapped both windows from (see
 * x86_64_build_page_tables()'s own comment on why that -- not the
 * unaligned image base -- is the correct reference point). Only valid for
 * addresses inside the window that call was told to map.
 */
static UQUAD to_high_alias(UQUAD low_addr, UQUAD mapped_base)
{
    return X86_64_KERNEL_VIRT_BASE + (low_addr - mapped_base);
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
    earlycon_puts("pTOS x86-64: page tables built, relocating to higher half\n");

    /*
     * &x86_64_higher_half_main and &boot_stack[...] are this image's
     * actual (low) runtime addresses here: the PE loader already applied
     * each pointer's base relocation to account for wherever it loaded us,
     * the same way it did for loaded_image->ImageBase above.
     */
    entry_high = to_high_alias((UQUAD)(uintptr_t)&x86_64_higher_half_main, mapped_base);
    stack_top_high = to_high_alias((UQUAD)(uintptr_t)&boot_stack[BOOT_STACK_BYTES], mapped_base);

    x86_64_relocate_higher_half(entry_high, cr3, (void *)(uintptr_t)stack_top_high);

    /* Not reached: x86_64_relocate_higher_half() never returns. */
    hang();
}

void NORETURN x86_64_higher_half_main(void)
{
    earlycon_puts("pTOS x86-64 EFI boot stub: alive in the higher half\n");
    hang();
}
