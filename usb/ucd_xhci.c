/*
 * ucd_xhci.c - xHCI USB host controller driver
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#define ENABLE_KDEBUG

#include "usb_global.h"
#include "usb.h"
#include "usb_api.h"
#include "raspi_vl805.h"
#include "raspi_int.h"
#include "endian.h"
#include "ucd_xhci.h"
#include "xhci_hw.h"

typedef char xhci_trb_size_check[(sizeof(xhci_trb_t) == 16U) ? 1 : -1];
typedef char xhci_qword_size_check[(sizeof(xhci_qword_t) == 8U) ? 1 : -1];
typedef char xhci_erst_entry_size_check[(sizeof(xhci_erst_entry_t) == 16U) ? 1 : -1];

struct xhci_priv {
    raspi_vl805_resources_t resources;
    BOOL have_resources;
    volatile UBYTE *cap_base;
    volatile UBYTE *op_base;
    volatile UBYTE *rt_base;
    UWORD max_slots;
    UWORD max_ports;
    UWORD slots_enabled;
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

static volatile ULONG *xhci_reg32(volatile UBYTE *base, ULONG offset)
{
    return (volatile ULONG *)(base + offset);
}

static UBYTE xhci_readb(volatile UBYTE *base, ULONG offset)
{
    return *(base + offset);
}

static ULONG xhci_readl(volatile UBYTE *base, ULONG offset)
{
    return le2cpu32(*xhci_reg32(base, offset));
}

static void xhci_writel(volatile UBYTE *base, ULONG offset, ULONG value)
{
    *xhci_reg32(base, offset) = cpu2le32(value);
}

/* 64-bit registers: write the low dword first, then the high dword --
 * per the xHCI spec, write order is irrelevant on implementations that
 * ignore the high dword, and mandatory (low first) on ones that don't. */
static void xhci_writeq(volatile UBYTE *base, ULONG offset, ULONG addr_lo)
{
    xhci_writel(base, offset, addr_lo);
    xhci_writel(base, offset + 4UL, 0UL);
}

static BOOL xhci_wait_clear(volatile UBYTE *base, ULONG offset, ULONG mask, ULONG timeout_us)
{
    ULONG waited;

    waited = 0UL;
    while (xhci_readl(base, offset) & mask) {
        if (waited >= timeout_us)
            return FALSE;
        raspi_delay_us(10UL);
        waited += 10UL;
    }
    return TRUE;
}

static BOOL xhci_wait_set(volatile UBYTE *base, ULONG offset, ULONG mask, ULONG timeout_us)
{
    ULONG waited;

    waited = 0UL;
    while (!(xhci_readl(base, offset) & mask)) {
        if (waited >= timeout_us)
            return FALSE;
        raspi_delay_us(10UL);
        waited += 10UL;
    }
    return TRUE;
}

/*
 * Reset sequence per xHCI spec section 4.2, verified against U-Boot's
 * xhci_reset(): halt if running, then reset, then wait for CNR to clear.
 * No doorbell or operational register other than USBSTS may be touched
 * before CNR clears.
 */
static BOOL xhci_hw_reset(struct xhci_priv *priv)
{
    ULONG cmd;

    if (!(xhci_readl(priv->op_base, XHCI_OP_USBSTS) & XHCI_STS_HALT)) {
        cmd = xhci_readl(priv->op_base, XHCI_OP_USBCMD);
        cmd &= ~XHCI_CMD_RUN;
        xhci_writel(priv->op_base, XHCI_OP_USBCMD, cmd);
    }

    if (!xhci_wait_set(priv->op_base, XHCI_OP_USBSTS, XHCI_STS_HALT, XHCI_HALT_TIMEOUT_US)) {
        KINFO(("xhci: timed out waiting for HALT before reset\n"));
        return FALSE;
    }

    cmd = xhci_readl(priv->op_base, XHCI_OP_USBCMD);
    cmd |= XHCI_CMD_RESET;
    xhci_writel(priv->op_base, XHCI_OP_USBCMD, cmd);

    if (!xhci_wait_clear(priv->op_base, XHCI_OP_USBCMD, XHCI_CMD_RESET, XHCI_RESET_TIMEOUT_US)) {
        KINFO(("xhci: timed out waiting for RESET to self-clear\n"));
        return FALSE;
    }

    if (!xhci_wait_clear(priv->op_base, XHCI_OP_USBSTS, XHCI_STS_CNR, XHCI_RESET_TIMEOUT_US)) {
        KINFO(("xhci: timed out waiting for CNR to clear\n"));
        return FALSE;
    }

    return TRUE;
}

static long xhci_lowlevel_init(struct xhci_priv *priv)
{
    UBYTE caplength;
    ULONG rtsoff;

    priv->have_resources = raspi_vl805_get_resources(&priv->resources);
    if (!priv->have_resources) {
        KINFO(("xhci: VL805 controller not available\n"));
        return EOPNOTSUPP;
    }

    KINFO(("xhci: MMIO 0x%lx size 0x%lx irq %u\n",
           priv->resources.mmio_base,
           priv->resources.mmio_size,
           priv->resources.irq));

    priv->cap_base = (volatile UBYTE *)priv->resources.mmio_base;
    caplength = xhci_readb(priv->cap_base, XHCI_CAP_CAPLENGTH);
    priv->op_base = priv->cap_base + caplength;

    rtsoff = xhci_readl(priv->cap_base, XHCI_CAP_RTSOFF) & ~0x1fUL;
    priv->rt_base = priv->cap_base + rtsoff;

    if (!xhci_hw_reset(priv)) {
        return ETIMEDOUT;
    }
    KINFO(("xhci: controller reset complete\n"));

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
    KDEBUG(("xhci_init\n"));
    if (ucd_register(&xhci_uif, &root_hub_dev)) {
        KINFO(("xhci_init(): ucd register failed!\n"));
        return;
    }

    KDEBUG(("xhci_init(): ucd register succeeded!\n"));
}
