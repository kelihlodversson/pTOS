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
#include "pci.h"
#include "raspi_vl805.h"

#define VL805_XHCI_CLASSCODE 0x0c0330UL

BOOL raspi_vl805_get_resources(raspi_vl805_resources_t *resources)
{
    PCI_HANDLE handle;
    pci_resource_t resource;
    UBYTE irq;
    LONG ret;

    if (resources != 0) {
        resources->mmio_base = 0;
        resources->mmio_size = 0;
        resources->irq = 0;
    }

    ret = pci_find_classcode(VL805_XHCI_CLASSCODE, 0UL, 0, &handle);
    if (ret != PCI_SUCCESSFUL) {
        KINFO(("VL805/xHCI: PCI device not found (%ld)\n", ret));
        return FALSE;
    }

    ret = pci_get_resource(handle, 0, &resource);
    if (ret != PCI_SUCCESSFUL) {
        KINFO(("VL805/xHCI: PCI BAR0 is not usable yet (%ld)\n", ret));
        return FALSE;
    }

    if ((resource.flags & PCI_RESOURCE_IO) != 0U) {
        KINFO(("VL805/xHCI: PCI BAR0 is an I/O resource\n"));
        return FALSE;
    }

    irq = 0;
    ret = pci_read_config_byte(handle, PCI_CONFIG_INTERRUPT_LINE, &irq);
    if ((ret != PCI_SUCCESSFUL) || (irq == 0xffU))
        irq = 0;

    if (resources != 0) {
        resources->mmio_base = resource.start + resource.offset;
        resources->mmio_size = resource.length;
        resources->irq = (UWORD)irq;
    }

    return TRUE;
}
