/*
 * pci_backend.h - internal PCI host bridge backend interface
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef PCI_BACKEND_H
#define PCI_BACKEND_H

#include "portab.h"
#include "pci.h"

typedef struct {
    ULONG ecam_base;
    ULONG ecam_size;
    ULONG mmio_base;
    ULONG mmio_size;
    ULONG pio_base;
    ULONG pio_size;
} pci_backend_windows_t;

typedef struct {
    LONG (*init)(void);
    LONG (*get_windows)(pci_backend_windows_t *windows);
    LONG (*read_config)(UBYTE bus, UBYTE dev, UBYTE func, UWORD reg, UWORD size, ULONG *value);
    LONG (*write_config)(UBYTE bus, UBYTE dev, UBYTE func, UWORD reg, UWORD size, ULONG value);
    LONG (*bus_to_phys)(ULONG bus_address, BOOL io, ULONG *phys_address);
    LONG (*phys_to_bus)(ULONG phys_address, BOOL io, ULONG *bus_address);
    LONG (*hook_interrupt)(PCI_HANDLE handle, UBYTE line, pci_interrupt_handler_t handler, void *param);
    LONG (*unhook_interrupt)(PCI_HANDLE handle, UBYTE line);
} pci_backend_t;

const pci_backend_t *pci_backend_get(void);

#endif /* PCI_BACKEND_H */
