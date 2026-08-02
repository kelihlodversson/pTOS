/*
 * raspi_vl805.c - Raspberry Pi 4 VL805 USB controller resources
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#ifndef TARGET_RPI4
#error This file must only be compiled for Raspberry Pi 4 targets
#endif

#include "kprint.h"
#include "raspi_vl805.h"

BOOL raspi_vl805_get_resources(raspi_vl805_resources_t *resources)
{
    if (resources != 0) {
        resources->mmio_base = 0;
        resources->mmio_size = 0;
        resources->irq = 0;
    }

    KINFO(("VL805/xHCI: BCM2711 PCIe discovery is not implemented yet\n"));
    return FALSE;
}
