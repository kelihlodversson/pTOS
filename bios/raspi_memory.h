/*
 * raspi_screen.h Raspberry PI framebuffer support
 *
 * Copyright (C) 2013-2018 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef RASPI_MEMORY_H
#   define RASPI_MEMORY_H
#   ifdef MACHINE_RPI

void raspi_vcmem_init(void);

#   endif /* MACHINE_RPI */
#endif /* RASPI_MEMORY_H */
