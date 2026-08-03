/*
 * raspi_vl805.h - Raspberry Pi 4 VL805 USB controller resources
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef RASPI_VL805_H
#define RASPI_VL805_H

#include "portab.h"

typedef struct {
    ULONG mmio_base;
    ULONG mmio_size;
    UWORD irq;
} raspi_vl805_resources_t;

BOOL raspi_vl805_get_resources(raspi_vl805_resources_t *resources);

#endif /* RASPI_VL805_H */
