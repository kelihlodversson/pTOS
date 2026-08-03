/*
 * virt_pci.h - QEMU ARM virt PCI backend
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VIRT_PCI_H
#define VIRT_PCI_H

#ifdef MACHINE_VIRT_ARM

#include "pci_backend.h"

const pci_backend_t *virt_pci_backend(void);

#endif /* MACHINE_VIRT_ARM */

#endif /* VIRT_PCI_H */
