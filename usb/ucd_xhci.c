/*
 * ucd_xhci.c - xHCI USB host controller driver
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "usb_global.h"
#include "usb.h"
#include "usb_api.h"
#include "raspi_vl805.h"
#include "ucd_xhci.h"

struct xhci_priv {
    raspi_vl805_resources_t resources;
    BOOL have_resources;
};

static long xhci_open(struct ucdif *u);
static long xhci_close(struct ucdif *u);
static long xhci_ioctl(struct ucdif *u, short cmd, long arg);
static long xhci_lowlevel_init(struct xhci_priv *priv);

static char xhci_lname[] = "xHCI USB driver\0";
static struct usb_device *root_hub_dev = NULL;
static struct xhci_priv xhci_local;
static struct ucdif xhci_uif =
{
    0,
    USB_API_VERSION,
    USB_CONTRLL,
    xhci_lname,
    "xhci",
    0,
    0,
    xhci_open,
    xhci_close,
    0,
    xhci_ioctl,
    0,
    (long *)&xhci_local
};

static long xhci_open(struct ucdif *u)
{
    (void)u;
    return E_OK;
}

static long xhci_close(struct ucdif *u)
{
    struct xhci_priv *priv;

    priv = (struct xhci_priv *)u->ucd_priv;
    priv->have_resources = FALSE;
    return E_OK;
}

static long xhci_lowlevel_init(struct xhci_priv *priv)
{
    priv->have_resources = raspi_vl805_get_resources(&priv->resources);
    if (!priv->have_resources) {
        KINFO(("xhci: VL805 controller not available\n"));
        return EOPNOTSUPP;
    }

    KINFO(("xhci: MMIO 0x%lx size 0x%lx irq %u\n",
           priv->resources.mmio_base,
           priv->resources.mmio_size,
           priv->resources.irq));
    KINFO(("xhci: controller bring-up is not implemented yet\n"));
    return EOPNOTSUPP;
}

static long xhci_ioctl(struct ucdif *u, short cmd, long arg)
{
    struct xhci_priv *priv;

    priv = (struct xhci_priv *)u->ucd_priv;

    switch (cmd) {
    case LOWLEVEL_INIT:
        return xhci_lowlevel_init(priv);
    case LOWLEVEL_STOP:
        priv->have_resources = FALSE;
        return E_OK;
    case SUBMIT_CONTROL_MSG:
    case SUBMIT_BULK_MSG:
    case SUBMIT_INT_MSG:
        (void)arg;
        KINFO(("xhci: transfer submission is not implemented yet\n"));
        return EOPNOTSUPP;
    default:
        (void)arg;
        return EINVFN;
    }
}

void xhci_init(void)
{
    if (ucd_register(&xhci_uif, &root_hub_dev)) {
        KINFO(("xhci_init(): ucd register failed!\n"));
        return;
    }

    KDEBUG(("xhci_init(): ucd register succeeded!\n"));
}
