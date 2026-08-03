/*
 * raspi_pci.h - Raspberry Pi 4 PCIe backend
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef RASPI_PCI_H
#define RASPI_PCI_H

#ifdef MACHINE_RPI

#include "pci_backend.h"

const pci_backend_t *raspi_pci_backend(void);

#endif /* MACHINE_RPI */

#endif /* RASPI_PCI_H */
