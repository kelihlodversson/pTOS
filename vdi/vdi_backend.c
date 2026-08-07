/*
 * vdi_backend.c - VDI drawing backend selection
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "vdi_defs.h"
#include "vdi_backend.h"

const vdi_backend_ops *vdi_backend_select(const SCREEN_MODE_DESC *mode)
{
    if (!screen_mode_desc_valid(mode))
        return NULL;

    if (mode->layout == SCREEN_LAYOUT_PLANAR && mode->color_model == SCREEN_COLOR_INDEXED)
        return &planar_backend_ops;

#if CONF_WITH_VDI_TRUECOLOR
    if (mode->layout == SCREEN_LAYOUT_PACKED
        && mode->color_model == SCREEN_COLOR_TRUECOLOR
        && mode->pixel_format == SCREEN_PIXEL_RGB565)
        return &packed_truecolor_backend_ops;
#endif

    return NULL;
}

BOOL vdi_screen_is_truecolor(void)
{
    return vdi_screen_backend() == &packed_truecolor_backend_ops;
}
