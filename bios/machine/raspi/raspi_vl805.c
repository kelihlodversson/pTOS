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

#include "portab.h"
#include "kprint.h"
#include "pci.h"
#include "raspi_int.h"
#include "raspi_mbox.h"
#include "raspi_vl805.h"

#define VL805_XHCI_CLASSCODE 0x0c0330UL
#define VL805_PCI_BDF        0x00100000UL

static BOOL raspi_vl805_load_firmware(void)
{
    prop_tag_1u32_t reset;

    reset.tag.tag_id = PROPTAG_NOTIFY_XHCI_RESET;
    reset.tag.value_buf_size = sizeof(ULONG);
    reset.tag.value_length = sizeof(ULONG);
    reset.value = VL805_PCI_BDF;
    if (!raspi_prop_get_tags(&reset, sizeof(reset))) {
        KINFO(("VL805/xHCI: firmware reset notification failed\n"));
        return FALSE;
    }

    /* VideoCore may have to load and start the VL805 firmware blob. */
    raspi_delay_us(20000UL);
    return TRUE;
}

BOOL raspi_vl805_get_resources(raspi_vl805_resources_t *resources)
{
    PCI_HANDLE handle;
    pci_resource_t resource;
    UWORD command;
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

    /*
     * VL805's BAR0 reports itself as 64-bit-capable, but its actual
     * firmware-assigned address may well fit in 32 bits (high dword
     * zero) -- pci_get_resource() (via pci_core.c's pci_decode_bar())
     * already checks that and only succeeds when it does, so there is
     * no separate BAR-type check to do here anymore.
     */
    ret = pci_get_resource(handle, 0, &resource);
    if (ret != PCI_SUCCESSFUL) {
        KINFO(("VL805/xHCI: PCI BAR0 is not usable yet (%ld)\n", ret));
        return FALSE;
    }

    if ((resource.flags & PCI_RESOURCE_IO) != 0U) {
        KINFO(("VL805/xHCI: PCI BAR0 is an I/O resource\n"));
        return FALSE;
    }

    if (!raspi_vl805_load_firmware())
        return FALSE;

    command = 0U;
    if (pci_read_config_word(handle, PCI_CONFIG_COMMAND, &command) != PCI_SUCCESSFUL)
        return FALSE;
    command |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    if (pci_write_config_word(handle, PCI_CONFIG_COMMAND, command) != PCI_SUCCESSFUL)
        return FALSE;
    if (pci_read_config_word(handle, PCI_CONFIG_COMMAND, &command) != PCI_SUCCESSFUL)
        return FALSE;
    if ((command & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) !=
        (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) {
        KINFO(("VL805/xHCI: PCI memory or bus mastering did not enable\n"));
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
