/*
 * virt_fwcfg.c - minimal QEMU fw_cfg client for the ARM 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_VIRT_ARM
#error This file must only be compiled for the QEMU ARM virt target
#endif

#include "portab.h"
#include "endian.h"
#include "string.h"
#include "processor.h"
#include "virt_mmu.h"
#include "virt_fwcfg.h"

/* Register layout confirmed live against this board via QEMU's monitor
 * ("info mtree"): a PL011-style split of data/selector/DMA registers, same
 * base every "virt" ARM boot uses regardless of any "-device" flags (see
 * docs/specs/fw_cfg.rst, "Register Locations" / "Arm"). */
#define FWCFG_BASE          0x09020000UL
#define FWCFG_DATA          (*(volatile UBYTE *)(FWCFG_BASE + 0x00))
#define FWCFG_SELECTOR      (*(volatile UWORD *)(FWCFG_BASE + 0x08))
#define FWCFG_DMA_ADDR_HI   (*(volatile ULONG *)(FWCFG_BASE + 0x10))
#define FWCFG_DMA_ADDR_LO   (*(volatile ULONG *)(FWCFG_BASE + 0x14))

#define FW_CFG_SIGNATURE    0x00U
#define FW_CFG_FILE_DIR     0x19U
#define FW_CFG_MAX_FILE_PATH 56

#define FW_CFG_DMA_CTL_ERROR   0x01UL
#define FW_CFG_DMA_CTL_READ    0x02UL
#define FW_CFG_DMA_CTL_SELECT  0x08UL
#define FW_CFG_DMA_CTL_WRITE   0x10UL

/* DRM_FORMAT_XRGB8888 = fourcc_code('X','R','2','4'); this is the only
 * pixel format this driver ever asks ramfb for, so the value is inlined
 * rather than reimplementing the DRM fourcc macro for one caller. */
#define RAMFB_FORMAT_XRGB8888  0x34325258UL

/* Generous: QEMU's "virt" board registers well under this many fw_cfg
 * files even with ramfb attached, and this only sizes a stack/static
 * scratch buffer, not anything device- or guest-visible. */
#define RAMFB_MAX_FILES     32

/* Wire-format structs (docs/specs/fw_cfg.rst, hw/display/ramfb.c in the
 * QEMU source): every field travels big-endian regardless of guest CPU
 * endianness, so each one is populated/read with cpu2be32()/be2cpu32()
 * rather than being written or read as a native value. A 64-bit address
 * field is split into two ULONGs (high word first, matching how a real
 * big-endian uint64_t would lay out as bytes) instead of using UQUAD, to
 * avoid any dependency on this toolchain's 64-bit alignment/padding rules
 * for a value this code only ever handles 32 bits at a time. */
typedef struct
{
    ULONG control;
    ULONG length;
    ULONG address_hi;
    ULONG address_lo;
} FWCFG_DMA_ACCESS;

typedef struct
{
    ULONG addr_hi;
    ULONG addr_lo;
    ULONG fourcc;
    ULONG flags;
    ULONG width;
    ULONG height;
    ULONG stride;
} FWCFG_RAMFB_CFG;

typedef struct
{
    ULONG size;
    UWORD select;
    UWORD reserved;
    char name[FW_CFG_MAX_FILE_PATH];
} FWCFG_FILE_ENTRY;

/* All DMA'd to/from directly by QEMU (not through the driver's normal
 * memory accesses), so their cache lines are explicitly flushed/invalidated
 * around every transfer instead of relying on ordinary load/store
 * visibility. */
static FWCFG_DMA_ACCESS dma_access;
static FWCFG_RAMFB_CFG ramfb_cfg;
static FWCFG_FILE_ENTRY file_entries[RAMFB_MAX_FILES];

static BOOL fwcfg_present(void)
{
    UBYTE sig[4];
    WORD i;

    FWCFG_SELECTOR = cpu2be16(FW_CFG_SIGNATURE);
    for (i = 0; i < 4; i++)
        sig[i] = FWCFG_DATA;

    return sig[0] == 'Q' && sig[1] == 'E' && sig[2] == 'M' && sig[3] == 'U';
}

/* Triggers one fw_cfg DMA operation and waits for it to finish. data_buf is
 * the caller's own read/write buffer; its physical address is what the
 * device transfers to/from, so callers must flush_data_cache() it first
 * when it is a write, and this function invalidates it on the caller's
 * behalf once the transfer completes (safe even for a write: it just
 * discards the driver's own now-stale cached copy). */
static BOOL fwcfg_dma(ULONG control_flags, UWORD selector, ULONG length, void *data_buf)
{
    ULONG control = control_flags;
    ULONG spins;

    if (control_flags & FW_CFG_DMA_CTL_SELECT)
        control |= ((ULONG)selector) << 16;

    dma_access.control    = cpu2be32(control);
    dma_access.length     = cpu2be32(length);
    dma_access.address_hi = cpu2be32(0);
    dma_access.address_lo = cpu2be32(virt_to_phys(data_buf));
    flush_data_cache(&dma_access, sizeof(dma_access));

    FWCFG_DMA_ADDR_HI = 0;
    /* Triggers the operation: per docs/specs/fw_cfg.rst, a write to the
     * low half is enough for a 32-bit address, provided the high half is
     * already zero, which the write just above guarantees. */
    FWCFG_DMA_ADDR_LO = cpu2be32(virt_to_phys(&dma_access));

    for (spins = 0; spins < 1000000UL; spins++)
    {
        ULONG status;

        invalidate_data_cache(&dma_access, sizeof(dma_access));
        status = be2cpu32(dma_access.control);
        if (status == 0)
        {
            invalidate_data_cache(data_buf, length);
            return TRUE;
        }
        if (status & FW_CFG_DMA_CTL_ERROR)
            return FALSE;
    }

    return FALSE;
}

/* Finds "etc/ramfb"'s fw_cfg selector key by walking the file directory --
 * QEMU assigns it dynamically, so it cannot be a fixed constant. Returns
 * FALSE if fw_cfg has no DMA interface, or no ramfb device is registered
 * (i.e. no "-device ramfb" was given): callers must keep working with no
 * visible display in that case, not treat it as fatal. */
static BOOL find_ramfb_selector(UWORD *out_selector)
{
    ULONG count, i, want;

    if (!fwcfg_dma(FW_CFG_DMA_CTL_SELECT | FW_CFG_DMA_CTL_READ, (UWORD)FW_CFG_FILE_DIR,
                   sizeof(count), &count))
        return FALSE;
    count = be2cpu32(count);
    if (count > RAMFB_MAX_FILES)
        count = RAMFB_MAX_FILES;

    /* Deliberately not re-selecting FW_CFG_FILE_DIR here: selecting an
     * item resets its data offset to zero, and this read must continue
     * right after the 4 count bytes the transfer above just consumed. */
    want = count * sizeof(FWCFG_FILE_ENTRY);
    if (!fwcfg_dma(FW_CFG_DMA_CTL_READ, 0, want, file_entries))
        return FALSE;

    for (i = 0; i < count; i++)
    {
        if (strcmp(file_entries[i].name, "etc/ramfb") == 0)
        {
            *out_selector = be2cpu16(file_entries[i].select);
            return TRUE;
        }
    }

    return FALSE;
}

BOOL virt_ramfb_configure(ULONG phys_addr, ULONG width, ULONG height, ULONG stride)
{
    UWORD selector;

    if (!fwcfg_present())
        return FALSE;

    if (!find_ramfb_selector(&selector))
        return FALSE;

    ramfb_cfg.addr_hi = cpu2be32(0);
    ramfb_cfg.addr_lo = cpu2be32(phys_addr);
    ramfb_cfg.fourcc  = cpu2be32(RAMFB_FORMAT_XRGB8888);
    ramfb_cfg.flags   = cpu2be32(0);
    ramfb_cfg.width   = cpu2be32(width);
    ramfb_cfg.height  = cpu2be32(height);
    ramfb_cfg.stride  = cpu2be32(stride);
    flush_data_cache(&ramfb_cfg, sizeof(ramfb_cfg));

    return fwcfg_dma(FW_CFG_DMA_CTL_SELECT | FW_CFG_DMA_CTL_WRITE, selector,
                     sizeof(ramfb_cfg), &ramfb_cfg);
}
