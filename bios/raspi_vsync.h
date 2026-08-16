/*
 * raspi_vsync.h - real vsync-driven VBL for the Raspberry Pi (Pi 1-3)
 *
 * Copyright (C) 2013-2026 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef RASPI_VSYNC_H
#define RASPI_VSYNC_H

#ifdef MACHINE_RPI

#if CONF_WITH_RASPI_VSYNC_IRQ
void raspi_vsync_init(void);
#endif

#endif /* MACHINE_RPI */

#endif /* RASPI_VSYNC_H */
