/*
 * vdi_backend_planar.c - planar indexed VDI drawing backend
 *
 * Copyright (C) 2026 by the pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "vdi_defs.h"

/*
 * The planar backend keeps no state beyond VDI_BACKEND_STATE itself, so
 * the lifecycle hooks succeed trivially for now.  The drawing and misc
 * slots are NULL until the corresponding primitives are moved here and
 * routed through the backends.
 */

static BOOL planar_open(Vwk *vwk)
{
    (void)vwk;
    return TRUE;
}

static BOOL planar_clone(Vwk *vwk, const Vwk *source)
{
    (void)vwk;
    (void)source;
    return TRUE;
}

static void planar_close(Vwk *vwk)
{
    (void)vwk;
}

static BOOL planar_mode_changed(Vwk *vwk, const SCREEN_MODE_DESC *mode)
{
    (void)vwk;
    (void)mode;
    return TRUE;
}

const VDI_BACKEND_OPS vdi_backend_planar_ops = {
    .open = planar_open,
    .clone = planar_clone,
    .close = planar_close,
    .mode_changed = planar_mode_changed,
};
