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
#include "endian.h"
#include "kprint.h"
#include "raspi_io.h"
#include "raspi_mbox.h"
#include "raspi_pci.h"

#define RASPI_PCIE_REG_BASE             0xfd500000UL
#define RASPI_PCIE_REG_SIZE             0x00009310UL

#define RASPI_PCIE_MMIO_BUS_BASE        0xf8000000UL
#define RASPI_PCIE_MMIO_SIZE            0x04000000UL

#define RASPI_PCIE_DMA_BUS_BASE         0x00000000UL
#define RASPI_PCIE_DMA_SIZE             0xc0000000UL

#define PCIE_RC_CFG_PRIV1_ID_VAL3       0x043cUL
#define PCIE_RC_CFG_PRIV1_ID_VAL3_CLASS_CODE_MASK 0x00ffffffUL
#define PCIE_MISC_PCIE_STATUS           0x4068UL
#define PCIE_MISC_PCIE_STATUS_PCIE_DL_ACTIVE_MASK 0x20UL
#define PCIE_MISC_PCIE_STATUS_PCIE_PHYLINKUP_MASK 0x10UL

#define PCIE_EXT_CFG_INDEX              0x9000UL
#define PCIE_EXT_CFG_DATA               0x8000UL

#define PCI_ECAM_REG(reg)               ((reg) & 0xfffU)
#define PCI_ECAM_OFFSET(bus, dev, func, reg) \
    (((ULONG)(bus) << 20) | ((ULONG)(dev) << 15) | ((ULONG)(func) << 12) | PCI_ECAM_REG(reg))

static BOOL raspi_pci_link_ready;

static volatile UBYTE *raspi_pci_reg_ptr(ULONG offset)
{
    return (volatile UBYTE *)(RASPI_PCIE_REG_BASE + offset);
}

static ULONG raspi_pci_readl(ULONG offset)
{
    return le2cpu32(*(volatile ULONG *)raspi_pci_reg_ptr(offset));
}

static void raspi_pci_writel(ULONG offset, ULONG value)
{
    *(volatile ULONG *)raspi_pci_reg_ptr(offset) = cpu2le32(value);
}

static BOOL raspi_pci_link_up(void)
{
    ULONG status;

    status = raspi_pci_readl(PCIE_MISC_PCIE_STATUS);
    return ((status & PCIE_MISC_PCIE_STATUS_PCIE_DL_ACTIVE_MASK) != 0UL) &&
           ((status & PCIE_MISC_PCIE_STATUS_PCIE_PHYLINKUP_MASK) != 0UL);
}

static volatile UBYTE *raspi_pci_config_ptr(UBYTE bus, UBYTE dev, UBYTE func, UWORD reg)
{
    ULONG index;

    if (bus == 0U) {
        if ((dev != 0U) || (func != 0U))
            return 0;
        return raspi_pci_reg_ptr(PCI_ECAM_REG(reg));
    }

    if (!raspi_pci_link_ready)
        return 0;

    index = PCI_ECAM_OFFSET(bus, dev, func, 0U);
    raspi_pci_writel(PCIE_EXT_CFG_INDEX, index);
    return raspi_pci_reg_ptr(PCIE_EXT_CFG_DATA + PCI_ECAM_REG(reg));
}

static LONG raspi_pci_init(void)
{
    if (raspi_pci_link_ready)
        raspi_pci_link_ready = raspi_pci_link_up();
    return PCI_FUNC_NOT_SUPPORTED;
}

static LONG raspi_pci_get_windows(pci_backend_windows_t *windows)
{
    if (windows == 0)
        return PCI_GENERAL_ERROR;
    windows->ecam_base = 0UL;
    windows->ecam_size = 0UL;
    windows->mmio_base = RASPI_PCIE_MMIO_BUS_BASE;
    windows->mmio_size = RASPI_PCIE_MMIO_SIZE;
    windows->pio_base = 0UL;
    windows->pio_size = 0UL;
    return PCI_SUCCESSFUL;
}

static LONG raspi_pci_read_config(UBYTE bus, UBYTE dev, UBYTE func, UWORD reg, UWORD size, ULONG *value)
{
    volatile UBYTE *ptr;

    if (value == 0)
        return PCI_GENERAL_ERROR;
    if (((size == 2U) && ((reg & 1U) != 0U)) ||
        ((size == 4U) && ((reg & 3U) != 0U)) ||
        ((size != 1U) && (size != 2U) && (size != 4U)))
        return PCI_BAD_REGISTER_NUMBER;

    ptr = raspi_pci_config_ptr(bus, dev, func, reg);
    if (ptr == 0) {
        *value = 0xffffffffUL;
        return PCI_SUCCESSFUL;
    }

    if (size == 1U)
        *value = (ULONG)*ptr;
    else if (size == 2U)
        *value = (ULONG)le2cpu16(*(volatile UWORD *)ptr);
    else
        *value = le2cpu32(*(volatile ULONG *)ptr);
    return PCI_SUCCESSFUL;
}

static LONG raspi_pci_write_config(UBYTE bus, UBYTE dev, UBYTE func, UWORD reg, UWORD size, ULONG value)
{
    volatile UBYTE *ptr;

    if (((size == 2U) && ((reg & 1U) != 0U)) ||
        ((size == 4U) && ((reg & 3U) != 0U)) ||
        ((size != 1U) && (size != 2U) && (size != 4U)))
        return PCI_BAD_REGISTER_NUMBER;

    ptr = raspi_pci_config_ptr(bus, dev, func, reg);
    if (ptr == 0)
        return PCI_SUCCESSFUL;

    if (size == 1U)
        *ptr = (UBYTE)value;
    else if (size == 2U)
        *(volatile UWORD *)ptr = cpu2le16((UWORD)value);
    else
        *(volatile ULONG *)ptr = cpu2le32(value);
    return PCI_SUCCESSFUL;
}

static LONG raspi_pci_bus_to_phys(ULONG bus_address, BOOL io, ULONG *phys_address)
{
    if (phys_address == 0)
        return PCI_GENERAL_ERROR;
    if (io)
        return PCI_BAD_RESOURCE;
    if ((bus_address < RASPI_PCIE_MMIO_BUS_BASE) ||
        (bus_address >= RASPI_PCIE_MMIO_BUS_BASE + RASPI_PCIE_MMIO_SIZE))
        return PCI_BAD_RESOURCE;

    return PCI_BAD_RESOURCE;
}

static LONG raspi_pci_phys_to_bus(ULONG phys_address, BOOL io, ULONG *bus_address)
{
    (void)phys_address;
    if (bus_address == 0)
        return PCI_GENERAL_ERROR;
    if (io)
        return PCI_BAD_RESOURCE;
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
