/*
 * pci_core.c - native pTOS PCI access layer
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#if CONF_WITH_PCI

#include "pci.h"

LONG pci_init(void)
{
    return PCI_SUCCESSFUL;
}

#endif /* CONF_WITH_PCI */
