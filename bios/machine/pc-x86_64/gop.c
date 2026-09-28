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

    /*
     * Resolution/pitch fields are firmware-reported ULONGs (up to 2^32-1),
     * but desc->width/height (screen_mode.h's SCREEN_MODE_DESC, filled in
     * by pc_x86_64_gop_get_current_mode_desc() below) are UWORD -- an
     * unchecked value above 65535 would silently truncate there. Capping
     * here also bounds the PixelsPerScanLine * 4 * VerticalResolution
     * multiplication below well clear of UQUAD overflow (65535^2 * 4 is a
     * tiny fraction of 2^64), rather than needing a separate overflow
     * check for it. No real GOP mode approaches this cap.
     */
    if (gop->Mode->Info->HorizontalResolution == 0
        || gop->Mode->Info->VerticalResolution == 0
        || gop->Mode->Info->HorizontalResolution > 0xFFFF
        || gop->Mode->Info->VerticalResolution > 0xFFFF
        || gop->Mode->Info->PixelsPerScanLine > 0xFFFF
        || gop->Mode->Info->PixelsPerScanLine < gop->Mode->Info->HorizontalResolution)
        return;

    /*
     * FrameBufferSize is what x86_64_gop_init() below maps and the only
     * bound anything later writing through the framebuffer has -- reject
     * a mode whose reported size doesn't actually cover a full
     * PixelsPerScanLine * 4 bytes/pixel * VerticalResolution image, rather
     * than trusting firmware to have gotten its own three fields
     * mutually consistent (Copilot review, PR #373). The multiplication
     * cannot overflow: both factors were just capped to 0xFFFF above.
     */
    if (gop->Mode->FrameBufferSize
        < (UQUAD)gop->Mode->Info->PixelsPerScanLine * 4 * gop->Mode->Info->VerticalResolution)
        return;

    /*
     * Overflow-safe rather than computing FrameBufferBase + FrameBufferSize
     * and checking the result: x86_64_gop_init()/x86_64_gop_reserved_range()
     * below add FrameBufferSize (and then a further 2 MiB - 1 for
     * alignment) to FrameBufferBase, and firmware is not trusted to have
     * reported a pair that can't wrap a 64-bit address -- rejecting before
     * doing that arithmetic, rather than after, means a wrapped result
     * can never be mistaken for a small, valid range. The extra
     * X86_64_PAGE_2M_SIZE margin covers the round-up-to-2MiB math both of
     * those functions still need to do safely.
     *
     * Done as two separate subtractions, each checked before it is
     * trusted, rather than one chained "~0 - size - 2M" expression: if
     * FrameBufferSize itself were within X86_64_PAGE_2M_SIZE of UQUAD_MAX,
     * that chained subtraction would itself underflow and wrap back
     * around to a huge value, silently *accepting* the exact malformed
     * range this check exists to reject (Copilot review, PR #373).
     * Rejecting on the first line whenever FrameBufferSize alone is too
     * close to UQUAD_MAX to leave room for the margin guarantees the
     * second line's own subtraction cannot underflow either.
     */
    if (gop->Mode->FrameBufferSize > ~(UQUAD)0 - X86_64_PAGE_2M_SIZE)
        return;
    if (gop->Mode->FrameBufferBase >
        ~(UQUAD)0 - X86_64_PAGE_2M_SIZE - gop->Mode->FrameBufferSize)
        return;

    gop_phys_base = gop->Mode->FrameBufferBase;
    gop_size = gop->Mode->FrameBufferSize;
    gop_width = gop->Mode->Info->HorizontalResolution;
    gop_height = gop->Mode->Info->VerticalResolution;
    gop_pitch_pixels = gop->Mode->Info->PixelsPerScanLine;
    gop_found = TRUE;
}

/*
 * The 2 MiB-aligned [phys_base, phys_end) span x86_64_gop_init() below maps
 * -- shared with x86_64_gop_reserved_range() so the physical range pmem.c
 * excludes from its own free list (reserved before any allocation can
 * hand out the same pages -- see that function's own comment) is always
 * exactly the range that ends up mapped, not a second, independently
 * computed approximation of it that could drift out of sync.
 *
 * x86_64_map_kernel_pages() only maps whole 2 MiB pages (this file's own
 * PD granularity, see pgtable.c) -- round the requested range out to
 * that, exactly like x86_64_low_tpa_init()/x86_64_low_kdata_init()
 * already do for their own allocations. gop_phys_base + gop_size cannot
 * overflow this addition: x86_64_gop_probe() already rejected any mode
 * whose FrameBufferBase/FrameBufferSize couldn't safely take the extra
 * X86_64_PAGE_2M_SIZE this rounding needs.
 */
static void gop_aligned_range(UQUAD *phys_base, UQUAD *phys_end)
{
    *phys_base = gop_phys_base & ~(X86_64_PAGE_2M_SIZE - 1);
    *phys_end = (gop_phys_base + gop_size + X86_64_PAGE_2M_SIZE - 1)
                & ~(X86_64_PAGE_2M_SIZE - 1);
}

void x86_64_gop_reserved_range(UQUAD *base, UQUAD *end)
{
    if (!gop_found) {
        *base = 0;
        *end = 0;
        return;
    }
    gop_aligned_range(base, end);
}

void x86_64_gop_init(void)
{
    UQUAD aligned_phys, aligned_end, page_count, offset;
    UQUAD virt_base;

    if (!gop_found)
        return;

    /* offset (how far FrameBufferBase itself sits past the rounded-down
     * base) is added back once mapped, so the returned screenbase is the
     * framebuffer's own real start, not the rounded-down page boundary. */
    gop_aligned_range(&aligned_phys, &aligned_end);
    offset = gop_phys_base - aligned_phys;
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
