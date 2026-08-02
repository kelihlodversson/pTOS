/*
 * virt_pci.c - QEMU ARM virt PCI backend
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#ifndef MACHINE_VIRT_ARM
#error This file must only be compiled for the QEMU ARM virt target
#endif

#include "portab.h"
#include "endian.h"
#include "pci.h"
#include "pci_backend.h"
#include "virt_memmap.h"
#include "virt_pci.h"

static LONG virt_pci_init(void)
{
    return PCI_SUCCESSFUL;
}

static LONG virt_pci_get_windows(pci_backend_windows_t *windows)
{
    if (windows == 0)
        return PCI_GENERAL_ERROR;

    windows->ecam_base = VIRT_PCIE_ECAM_BASE;
    windows->ecam_size = VIRT_PCIE_ECAM_SIZE;
    windows->mmio_base = VIRT_PCIE_MMIO_BASE;
    windows->mmio_size = VIRT_PCIE_MMIO_SIZE;
    windows->pio_base = VIRT_PCIE_PIO_BASE;
    windows->pio_size = VIRT_PCIE_PIO_SIZE;

    return PCI_SUCCESSFUL;
}

static volatile UBYTE *virt_pci_ecam_ptr(UBYTE bus, UBYTE dev, UBYTE func, UWORD reg)
{
    return (volatile UBYTE *)(VIRT_PCIE_ECAM_BASE + ((ULONG)bus << 20) +
                              ((ULONG)dev << 15) + ((ULONG)func << 12) + reg);
}

static LONG virt_pci_read_config(UBYTE bus, UBYTE dev, UBYTE func, UWORD reg, UWORD size, ULONG *value)
{
    volatile UBYTE *ptr;
    volatile UWORD *word_ptr;
    volatile ULONG *long_ptr;

    if (value == 0)
        return PCI_GENERAL_ERROR;

    ptr = virt_pci_ecam_ptr(bus, dev, func, reg);

    if (size == 1U) {
        *value = (ULONG)*ptr;
        return PCI_SUCCESSFUL;
    }

    if (size == 2U) {
        word_ptr = (volatile UWORD *)ptr;
        *value = (ULONG)le2cpu16(*word_ptr);
        return PCI_SUCCESSFUL;
    }

    if (size == 4U) {
        long_ptr = (volatile ULONG *)ptr;
        *value = le2cpu32(*long_ptr);
        return PCI_SUCCESSFUL;
    }

    return PCI_BAD_REGISTER_NUMBER;
}

static LONG virt_pci_write_config(UBYTE bus, UBYTE dev, UBYTE func, UWORD reg, UWORD size, ULONG value)
{
    volatile UBYTE *ptr;
    volatile UWORD *word_ptr;
    volatile ULONG *long_ptr;

    ptr = virt_pci_ecam_ptr(bus, dev, func, reg);

    if (size == 1U) {
        *ptr = (UBYTE)value;
        return PCI_SUCCESSFUL;
    }

    if (size == 2U) {
        word_ptr = (volatile UWORD *)ptr;
        *word_ptr = cpu2le16((UWORD)value);
        return PCI_SUCCESSFUL;
    }

    if (size == 4U) {
        long_ptr = (volatile ULONG *)ptr;
        *long_ptr = cpu2le32(value);
        return PCI_SUCCESSFUL;
    }

    return PCI_BAD_REGISTER_NUMBER;
}

static LONG virt_pci_bus_to_phys(ULONG bus_address, BOOL io, ULONG *phys_address)
{
    if (phys_address == 0)
        return PCI_GENERAL_ERROR;

    if (io) {
        if (bus_address >= VIRT_PCIE_PIO_SIZE)
            return PCI_BAD_RESOURCE;
        *phys_address = VIRT_PCIE_PIO_BASE + bus_address;
        return PCI_SUCCESSFUL;
    }

    if ((bus_address < VIRT_PCIE_MMIO_BASE) ||
        (bus_address >= VIRT_PCIE_MMIO_BASE + VIRT_PCIE_MMIO_SIZE))
        return PCI_BAD_RESOURCE;

    *phys_address = bus_address;
    return PCI_SUCCESSFUL;
}

static LONG virt_pci_phys_to_bus(ULONG phys_address, BOOL io, ULONG *bus_address)
{
    if (bus_address == 0)
        return PCI_GENERAL_ERROR;

    if (io) {
        if ((phys_address < VIRT_PCIE_PIO_BASE) ||
            (phys_address >= VIRT_PCIE_PIO_BASE + VIRT_PCIE_PIO_SIZE))
            return PCI_BAD_RESOURCE;
        *bus_address = phys_address - VIRT_PCIE_PIO_BASE;
        return PCI_SUCCESSFUL;
    }

    if ((phys_address < VIRT_PCIE_MMIO_BASE) ||
        (phys_address >= VIRT_PCIE_MMIO_BASE + VIRT_PCIE_MMIO_SIZE))
        return PCI_BAD_RESOURCE;

    *bus_address = phys_address;
    return PCI_SUCCESSFUL;
}

static LONG virt_pci_hook_interrupt(PCI_HANDLE handle, UBYTE line, pci_interrupt_handler_t handler, void *param)
{
    return PCI_FUNC_NOT_SUPPORTED;
}

static LONG virt_pci_unhook_interrupt(PCI_HANDLE handle, UBYTE line)
{
    return PCI_FUNC_NOT_SUPPORTED;
}

static pci_backend_t virt_pci_backend_ops = {
    virt_pci_init,
    virt_pci_get_windows,
    virt_pci_read_config,
    virt_pci_write_config,
    virt_pci_bus_to_phys,
    virt_pci_phys_to_bus,
    virt_pci_hook_interrupt,
    virt_pci_unhook_interrupt
};

const pci_backend_t *virt_pci_backend(void)
{
    return &virt_pci_backend_ops;
}

const pci_backend_t *pci_backend_get(void)
{
    return virt_pci_backend();
}
