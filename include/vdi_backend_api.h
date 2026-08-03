/*
 * vdi_backend_api.h - BIOS to VDI backend interface
 *
 * Copyright (C) 2026 by the pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VDI_BACKEND_API_H
#define VDI_BACKEND_API_H

/*
 * Notify the VDI that the screen mode may have changed.  Called from the
 * BIOS setscreen() path after the hardware, the Line-A geometry and the
 * logical screen base have been updated.  The VDI re-queries the current
 * mode descriptor and rebinds the physical workstation and all virtual
 * ones, so an unsupported mode leaves every workstation unbound instead
 * of drawing with a stale layout.
 */
void vdi_screen_mode_changed(void);

#endif /* VDI_BACKEND_API_H */
