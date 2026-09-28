/*
 * pc_x86_64_gop.h - x86-64 EFI GOP (Graphics Output Protocol) framebuffer
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef PC_X86_64_GOP_H
#define PC_X86_64_GOP_H

#include "screen_mode.h"

/*
 * Queries GOP (if present) and stashes its framebuffer's physical base/
 * size and mode info into this file's own bss. Must be called while EFI
 * boot services are still valid -- LocateProtocol() is a boot-services
 * call, unavailable after ExitBootServices() -- and before the higher-half
 * jump, since GOP's own Mode/Info structures live in ordinary EFI pool
 * memory this image has no further claim on once boot services exit.
 * Absence of GOP (headless firmware, or an unsupported pixel format) is
 * not fatal: this only records that no framebuffer is available, for
 * x86_64_gop_init() and the pc_x86_64_gop_*() accessors below to reflect.
 *
 * bs is really an EFI_BOOT_SERVICES* (efi.h) -- taken as void* so this
 * header, unlike efi.h itself, stays includable from bios/screen.c and
 * other shared, non-EFI-aware files without pulling in EFI-specific
 * types they have no business seeing. gop.c casts it back internally.
 */
void x86_64_gop_probe(void *bs);

/*
 * Maps the framebuffer x86_64_gop_probe() found into this kernel's own
 * page tables (bios/machine/pc-x86_64/memory.c's low-pool machinery) and
 * computes its real, dereferenceable virtual address. Must run after
 * x86_64_low_kdata_init() (same preconditions: the physical-memory direct
 * map and PML4 slot 0's low-pool PD must already exist) and before
 * anything calls the pc_x86_64_gop_*() accessors below -- in practice,
 * before biosmain() reaches bios_init()'s screen_init_mode()/
 * screen_init_address(). A no-op if x86_64_gop_probe() found no usable
 * GOP framebuffer.
 */
void x86_64_gop_init(void);

/* True iff a GOP framebuffer was found, is in a supported pixel format,
 * and has been successfully mapped. Every other function below is only
 * meaningful once this returns TRUE. */
BOOL pc_x86_64_gop_present(void);

/* Fills desc per screen_mode.h's own contract, from the mode GOP reported
 * at boot -- see bios/screen.c's screen_get_current_mode_desc(), the
 * MACHINE_RPI/CONF_WITH_VIRTIO_GPU equivalents this mirrors. */
void pc_x86_64_gop_get_current_mode_desc(SCREEN_MODE_DESC *desc);

/* The framebuffer's real, dereferenceable (virtual, post-relocation)
 * address -- what bios/screen.c's screen_init_address() assigns to
 * v_bas_ad, the same role raspi_physbase()/virtio_gpu's own v_bas_ad
 * assignment play for their machines. Physical, not virtual, addresses
 * are never directly dereferenceable on this arch once the identity
 * mapping is dropped (see pgtable.c), unlike m68k/ARM/raspi's own
 * flat-ish addressing -- hence "screenbase", not "physbase". */
UBYTE *pc_x86_64_gop_screenbase(void);

#endif /* PC_X86_64_GOP_H */
