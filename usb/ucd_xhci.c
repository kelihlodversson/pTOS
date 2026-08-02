/*
 * ucd_xhci.c - xHCI USB controller driver
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "usb_global.h"
#include "ucd_xhci.h"

void xhci_init(void)
{
    /* The VL805 PCIe resource path is wired in a later task. */
    KINFO(("xhci_init(): controller initialization not implemented yet\n"));
}
