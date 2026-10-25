/* Bounded scalar/text VDI operations shared by the x32 library and service.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later.
 */
#include "vdi_service.h"
#include "gemerror.h"

LONG vdi_user_draw_shape(const VDI_SERVICE_DRAW_REQUEST *r,
                         WORD *nintout, WORD *nptsout)
{
    WORD ni = 0, np = 0;
    LONG i;

    *nintout = *nptsout = 0;
    if ((r->code == 102 && r->subcode != 0 && r->subcode != 1) ||
        (r->code != 102 && r->subcode != (r->code == 11 ? 1 : 0)))
        return EINVFN;
    switch (r->code) {
    case 8:                             /* v_gtext, bounded word characters */
        if (r->nintin < 0 || r->nintin > VDI_SERVICE_MAX_TEXT)
            return EINVAL;
        ni = r->nintin;
        np = 1;
        break;
    case 11: case 114:                  /* bar / filled rectangle */
        np = 2;
        break;
    case 12:                            /* character height */
        np = 1;
        *nptsout = 2;
        if (r->ptsin[0] || r->ptsin[1] < 1 || r->ptsin[1] > 256)
            return EINVAL;
        break;
    case 15: case 22: case 23: case 24: case 25: case 32: case 113:
        ni = *nintout = 1;
        if ((r->code == 15 && (r->intin[0] < 1 || r->intin[0] > 7)) ||
            ((r->code == 22 || r->code == 25) &&
             (r->intin[0] < 0 || r->intin[0] > 255)) ||
            (r->code == 23 && (r->intin[0] < 0 || r->intin[0] > 3)) ||
            (r->code == 24 && (r->intin[0] < 1 || r->intin[0] > 24)) ||
            (r->code == 32 && (r->intin[0] < 1 || r->intin[0] > 4)))
            return EINVAL;
        /* vsl_udsty() has no output. */
        if (r->code == 113)
            *nintout = 0;
        break;
    case 16:                            /* line width */
        np = 1;
        *nptsout = 1;
        if (r->ptsin[0] < 1 || r->ptsin[0] > 39 || r->ptsin[1])
            return EINVAL;
        break;
    case 38:                            /* text attributes */
        *nintout = 6;
        *nptsout = 2;
        break;
    case 102:                           /* vq_extnd(1) */
        ni = 1;
        *nintout = 45;
        *nptsout = 6;
        if (r->intin[0] != 1)
            return EINVAL;
        break;
    default:
        return EINVFN;
    }
    if (r->nintin != ni || r->nptsin != np)
        return EINVAL;
    for (i = ni; i < VDI_SERVICE_MAX_TEXT; i++)
        if (r->intin[i])
            return EINVAL;
    for (i = np * 2; i < 4; i++)
        if (r->ptsin[i])
            return EINVAL;
    return E_OK;
}
