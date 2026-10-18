/*
 * vdi_user.c - shared x32 VDI contract helpers (no privileged accesses)
 *
 * Copyright (C) 2026 The pTOS development team.
 * This file is distributed under the GPL, version 2 or any later version.
 */
#include "vdi_user.h"

enum vdi_user_kind vdi_user_control_kind(WORD code, WORD subcode)
{
    /* The dispatcher in vdi_main.c and the subdispatchers in vdi_esc.c
     * and vdi_gdp.c define the complete inventory. Legacy no-op entries
     * and unused subcodes are explicitly unsupported in the new ABI. */
    if (code == 5)
        return subcode >= 1 && subcode <= 19 ? VDI_USER_ARRAYS : VDI_USER_UNSUPPORTED;
    if (code == 11)
        return subcode >= 1 && subcode <= 10 ? VDI_USER_ARRAYS : VDI_USER_UNSUPPORTED;
    if (subcode != 0)
        return VDI_USER_UNSUPPORTED;    /* includes disabled Bezier subcode 13 */

    switch (code) {
    case 109:
    case 110:
    case 121:
        return VDI_USER_RASTER;
    case 118:
        return VDI_USER_TIMER;
    case 119:
        return VDI_USER_FONT_LOAD;
    case 125:
        return VDI_USER_BUTTON;
    case 126:
        return VDI_USER_MOTION;
    case 127:
        return VDI_USER_CURSOR;
    case 134:
        return VDI_USER_WHEEL;
    case 4: case 10: case 27: case 29: case 34: case 132: case 133:
        return VDI_USER_UNSUPPORTED;
    default:
        if ((code >= 1 && code <= 39) || (code >= 100 && code <= 131))
            return VDI_USER_ARRAYS;
        return VDI_USER_UNSUPPORTED;
    }
}

BOOL vdi_user_bitmap_bytes(const VDI_USER_FDB *form, UWORD packed_bytes,
                           ULONG *bytes)
{
    UQUAD stride, size, address;

    *bytes = 0;
    if (packed_bytes != 0 && packed_bytes != 2 && packed_bytes != 4)
        return FALSE;
    address = (UQUAD)(uintptr_t)USERPTR_TO_PTR(form->fd_addr);
    if (address == 0)
        return TRUE;                    /* screen; checked by the display service */
    if (form->fd_w <= 0 || form->fd_h <= 0 || form->fd_wdwidth <= 0 ||
        form->fd_nplanes <= 0 || form->fd_nplanes > 32 ||
        (form->fd_stand != 0 && form->fd_stand != 1) ||
        form->fd_r1 || form->fd_r2 || form->fd_r3 ||
        (ULONG)form->fd_wdwidth * 16UL < (ULONG)form->fd_w)
        return FALSE;
    if (packed_bytes) {
        if (form->fd_stand || form->fd_nplanes != packed_bytes * 8)
            return FALSE;
        stride = (UQUAD)(UWORD)form->fd_w * packed_bytes;
    } else {
        stride = (UQUAD)(UWORD)form->fd_wdwidth * 2 * (UWORD)form->fd_nplanes;
    }
    size = stride * (UWORD)form->fd_h;
    /* The full bitmap, not just its first row, must fit the x32 address
     * space. Compute wide before narrowing; a wrapped size is never valid. */
    if (size > 0xffffffffULL || address + size > 0x100000000ULL)
        return FALSE;
    *bytes = (ULONG)size;
    return TRUE;
}
