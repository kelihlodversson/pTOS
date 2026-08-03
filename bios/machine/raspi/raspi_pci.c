/*
 * raspi_pci.c - Raspberry Pi 4 BCM2711 PCIe backend
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#if !defined(MACHINE_RPI) || !defined(TARGET_RPI4)
#error This file must only be compiled for Raspberry Pi 4 targets
#endif

#include "portab.h"
#include "pci.h"
#include "pci_backend.h"
#include "raspi_pci.h"

static LONG raspi_pci_init(void)
{
    return PCI_FUNC_NOT_SUPPORTED;
}

static LONG raspi_pci_get_windows(pci_backend_windows_t *windows)
{
    if (windows == 0)
        return PCI_GENERAL_ERROR;
    windows->ecam_base = 0UL;
    windows->ecam_size = 0UL;
    windows->mmio_base = 0UL;
    windows->mmio_size = 0UL;
    windows->pio_base = 0UL;
    windows->pio_size = 0UL;
    return PCI_SUCCESSFUL;
}

static LONG raspi_pci_read_config(UBYTE bus, UBYTE dev, UBYTE func, UWORD reg, UWORD size, ULONG *value)
{
    (void)bus;
    (void)dev;
    (void)func;
    (void)reg;
    (void)size;
    if (value == 0)
        return PCI_GENERAL_ERROR;
    *value = 0xffffffffUL;
    return PCI_FUNC_NOT_SUPPORTED;
}

static LONG raspi_pci_write_config(UBYTE bus, UBYTE dev, UBYTE func, UWORD reg, UWORD size, ULONG value)
{
    (void)bus;
    (void)dev;
    (void)func;
    (void)reg;
    (void)size;
    (void)value;
    return PCI_FUNC_NOT_SUPPORTED;
}

static LONG raspi_pci_bus_to_phys(ULONG bus_address, BOOL io, ULONG *phys_address)
{
    (void)bus_address;
    (void)io;
    if (phys_address == 0)
        return PCI_GENERAL_ERROR;
    return PCI_BAD_RESOURCE;
}

static LONG raspi_pci_phys_to_bus(ULONG phys_address, BOOL io, ULONG *bus_address)
{
    (void)phys_address;
    (void)io;
    if (bus_address == 0)
        return PCI_GENERAL_ERROR;
    return PCI_BAD_RESOURCE;
}

static LONG raspi_pci_hook_interrupt(PCI_HANDLE handle, UBYTE line, pci_interrupt_handler_t handler, void *param)
{
    (void)handle;
    (void)line;
    (void)handler;
    (void)param;
    return PCI_FUNC_NOT_SUPPORTED;
}

static LONG raspi_pci_unhook_interrupt(PCI_HANDLE handle, UBYTE line)
{
    (void)handle;
    (void)line;
    return PCI_FUNC_NOT_SUPPORTED;
}

static pci_backend_t raspi_pci_backend_ops = {
    raspi_pci_init,
    raspi_pci_get_windows,
    raspi_pci_read_config,
    raspi_pci_write_config,
    raspi_pci_bus_to_phys,
    raspi_pci_phys_to_bus,
    raspi_pci_hook_interrupt,
    raspi_pci_unhook_interrupt
};

const pci_backend_t *raspi_pci_backend(void)
{
    return &raspi_pci_backend_ops;
}

const pci_backend_t *pci_backend_get(void)
{
    return raspi_pci_backend();
}
