/*
 * gop.c - x86-64 EFI GOP (Graphics Output Protocol) framebuffer (#332)
 *
 * Split into two phases by a hard EFI constraint: GOP's own Mode/Info
 * structures are only valid, and LocateProtocol() only callable, while EFI
 * boot services are still up -- but mapping the discovered framebuffer
 * into this kernel's own page tables needs the physical-memory direct map
 * and PML4 slot 0's low-pool PD, neither of which exist until well after
 * the higher-half jump. x86_64_gop_probe() (called from startup.c, before
 * ExitBootServices()) does the EFI-side query and stashes plain scalars
 * (a physical address, a size, a few small integers) into this file's own
 * bss -- ordinary boot-time image data, unlike GOP's own Mode/Info
 * structures, which live in EFI pool memory this image has no claim on
 * once boot services exit. x86_64_gop_init() (called from
 * x86_64_higher_half_main(), well after the jump) does the actual
 * mapping from those stashed values.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_PC_X86_64
#error This file must only be compiled for the pc-x86_64 machine
#endif

#include "portab.h"
#include "efi.h"
#include "earlycon.h"
#include "pgtable.h"
#include "pc_x86_64_memory.h"
#include "pc_x86_64_gop.h"

static BOOL gop_found;
static UQUAD gop_phys_base;
static UQUAD gop_size;
static ULONG gop_width;
static ULONG gop_height;
static ULONG gop_pitch_pixels;  /* EFI's PixelsPerScanLine -- pixels, not bytes */

static UBYTE *gop_screenbase;   /* 0 until x86_64_gop_init() runs */

void x86_64_gop_probe(void *bs_arg)
{
    EFI_BOOT_SERVICES *bs = (EFI_BOOT_SERVICES *)bs_arg;
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    EFI_STATUS status;

    gop_found = FALSE;

    /*
     * Absence of GOP is routine, not an error: a serial-only VM, or
     * firmware that never installed the protocol at all, is a real,
     * supported (if framebuffer-less) boot -- the same posture
     * virtio_gpu_init() (bios/virtio_gpu.c) takes for its own hardware.
     */
    status = bs->LocateProtocol(&gop_guid, NULL, (void **)&gop);
    if (status & EFI_ERROR_BIT)
        return;

    if (!gop->Mode || !gop->Mode->Info)
        return;

    /*
     * Only the two 32-bit-packed pixel formats are accepted (#332's own
     * scope: "at minimum 32-bit RGB/BGR") -- and of those two, only
     * PixelBlueGreenRedReserved8BitPerColor (memory byte order B,G,R,X)
     * actually matches this codebase's SCREEN_PIXEL_XRGB8888 (a
     * little-endian 32-bit load producing 0x XX RR GG BB) --
     * PixelRedGreenBlueReserved8BitPerColor (R,G,B,X in memory, i.e.
     * 0x XX BB GG RR loaded) has no matching screen_mode.h constant, and
     * silently drawing through it as if it were XRGB8888 would swap red
     * and blue on whatever firmware actually reports it. Both
     * PixelBitMask (a firmware-chosen, possibly non-byte-aligned layout)
     * and PixelBltOnly (no linear framebuffer at all) are rejected
     * outright for the same "don't guess" reason. Real PC/OVMF firmware
     * overwhelmingly reports PixelBlueGreenRedReserved8BitPerColor (the
     * legacy VGA/VESA BGRX convention), so this covers the common case;
     * see efi.h's own comment on the four PIXEL_* values.
     */
    if (gop->Mode->Info->PixelFormat != PIXEL_BGR_RESERVED_8BIT_PER_COLOR)
        return;

    if (gop->Mode->FrameBufferBase == 0 || gop->Mode->FrameBufferSize == 0)
        return;

    if (gop->Mode->Info->HorizontalResolution == 0
        || gop->Mode->Info->VerticalResolution == 0
        || gop->Mode->Info->PixelsPerScanLine < gop->Mode->Info->HorizontalResolution)
        return;

    gop_phys_base = gop->Mode->FrameBufferBase;
    gop_size = gop->Mode->FrameBufferSize;
    gop_width = gop->Mode->Info->HorizontalResolution;
    gop_height = gop->Mode->Info->VerticalResolution;
    gop_pitch_pixels = gop->Mode->Info->PixelsPerScanLine;
    gop_found = TRUE;
}

void x86_64_gop_init(void)
{
    UQUAD aligned_phys, aligned_end, page_count, offset;
    UQUAD virt_base;

    if (!gop_found)
        return;

    /*
     * x86_64_map_kernel_pages() only maps whole 2 MiB pages (this file's
     * own PD granularity, see pgtable.c) -- round the requested range
     * out to that, exactly like x86_64_low_tpa_init()/
     * x86_64_low_kdata_init() already do for their own allocations.
     * offset (how far FrameBufferBase itself sits past the rounded-down
     * base) is added back once mapped, so the returned screenbase is the
     * framebuffer's own real start, not the rounded-down page boundary.
     */
    aligned_phys = gop_phys_base & ~(X86_64_PAGE_2M_SIZE - 1);
    offset = gop_phys_base - aligned_phys;
    aligned_end = (gop_phys_base + gop_size + X86_64_PAGE_2M_SIZE - 1)
                  & ~(X86_64_PAGE_2M_SIZE - 1);
    page_count = (aligned_end - aligned_phys) / X86_64_PAGE_2M_SIZE;

    virt_base = x86_64_low_fb_init(aligned_phys, page_count);
    if (!virt_base) {
        gop_found = FALSE;      /* pool too small for this mode -- see memory.c */
        return;
    }

    gop_screenbase = (UBYTE *)(uintptr_t)(virt_base + offset);
}

BOOL pc_x86_64_gop_present(void)
{
    return gop_found;
}

void pc_x86_64_gop_get_current_mode_desc(SCREEN_MODE_DESC *desc)
{
    desc->width = (UWORD)gop_width;
    desc->height = (UWORD)gop_height;
    desc->pitch = (ULONG)gop_pitch_pixels * 4UL;
    desc->bits_per_pixel = 32;
    desc->layout = SCREEN_LAYOUT_PACKED;
    desc->color_model = SCREEN_COLOR_TRUECOLOR;
    desc->pixel_format = SCREEN_PIXEL_XRGB8888;
    desc->shifter = SCREEN_SHIFTER_NONE;
}

UBYTE *pc_x86_64_gop_screenbase(void)
{
    return gop_screenbase;
}
