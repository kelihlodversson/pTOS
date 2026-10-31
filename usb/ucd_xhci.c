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
#include "usb_io.h"
#include "raspi_vl805.h"
#include "raspi_int.h"
#include "raspi_memory.h"
#include "endian.h"
#include "biosext.h"
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

static UBYTE xhci_readb(volatile UBYTE *base, ULONG offset)
{
    return readb(base + offset);
}

static ULONG xhci_readl(volatile UBYTE *base, ULONG offset)
{
    return le2cpu32(readl(base + offset));
}

static void xhci_writel(volatile UBYTE *base, ULONG offset, ULONG value)
{
    writel(cpu2le32(value), base + offset);
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
 * Besides USBCMD (to trigger and, via RESET, poll the reset itself) and
 * USBSTS (to poll HALT/CNR), no doorbell or other operational register
 * may be touched until CNR clears -- xhci_lowlevel_init() only starts
 * programming DCBAAP/CRCR/CONFIG/etc. after this function returns TRUE.
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
        KINFO(("xhci: timed out waiting for RESET to self-clear; USBCMD=%08lx USBSTS=%08lx\n",
               xhci_readl(priv->op_base, XHCI_OP_USBCMD),
               xhci_readl(priv->op_base, XHCI_OP_USBSTS)));
        return FALSE;
    }

    if (!xhci_wait_clear(priv->op_base, XHCI_OP_USBSTS, XHCI_STS_CNR, XHCI_RESET_TIMEOUT_US)) {
        KINFO(("xhci: timed out waiting for CNR to clear\n"));
        return FALSE;
    }

    return TRUE;
}

DEFINE_ALIGN_BUFFER(xhci_qword_t, xhci_dcbaa, XHCI_MAX_DCBAA_SLOTS + 1U, XHCI_DMA_ALIGN);
DEFINE_ALIGN_BUFFER(xhci_trb_t, xhci_cmd_ring,
                    XHCI_TRBS_PER_SEGMENT + XHCI_TRB_GUARD_TRBS,
                    XHCI_TRB_SEGMENT_ALIGN);
static xhci_trb_t *xhci_event_ring;
static xhci_erst_entry_t *xhci_erst;
DEFINE_ALIGN_BUFFER(xhci_qword_t, xhci_scratchpad_array, XHCI_MAX_SCRATCHPAD_BUFS, XHCI_DMA_ALIGN);
DEFINE_ALIGN_BUFFER(UBYTE, xhci_scratchpad_bufs, XHCI_MAX_SCRATCHPAD_BUFS * XHCI_PAGE_SIZE, XHCI_PAGE_SIZE);

/* CONFIG.MaxSlotsEn is capped conservatively, but the DCBAA must still hold
 * one entry per hardware slot plus entry zero. */
static void xhci_configure_slots(struct xhci_priv *priv)
{
    ULONG hcs1;
    ULONG hw_max_slots;
    ULONG config;

    hcs1 = xhci_readl(priv->cap_base, XHCI_CAP_HCSPARAMS1);
    hw_max_slots = XHCI_HCS1_MAX_SLOTS(hcs1);
    priv->max_slots = (UWORD)hw_max_slots;
    priv->slots_enabled = (hw_max_slots < (ULONG)XHCI_MAX_SLOTS_ENABLED)
        ? (UWORD)hw_max_slots
        : (UWORD)XHCI_MAX_SLOTS_ENABLED;

    KINFO(("xhci: %lu slots available, %u enabled\n", hw_max_slots, priv->slots_enabled));

    config = xhci_readl(priv->op_base, XHCI_OP_CONFIG);
    config &= ~XHCI_CONFIG_SLOTS_MASK;
    config |= (ULONG)priv->slots_enabled;
    xhci_writel(priv->op_base, XHCI_OP_CONFIG, config);
}

static void xhci_init_dcbaa(void)
{
    ULONG i;

    for (i = 0UL; i < (ULONG)(XHCI_MAX_DCBAA_SLOTS + 1U); i++) {
        xhci_dcbaa[i].lo = 0UL;
        xhci_dcbaa[i].hi = 0UL;
    }
    flush_data_cache((void *)xhci_dcbaa,
                      (long)((ULONG)(XHCI_MAX_DCBAA_SLOTS + 1U) * sizeof(xhci_qword_t)));
}

/*
 * Single-segment Command Ring, closed into a loop by a Link TRB. Per the
 * xHCI spec (4.11.1.1, "All components of all Command and Transfer TRBs
 * shall be initialized to 0") and verified against U-Boot's
 * xhci_link_segments(): the Link TRB's own Cycle bit stays 0 at init --
 * only its Type field and the Toggle Cycle control bit are set. The
 * ring's initial producer cycle state (1) is written separately, into
 * CRCR itself, not into any TRB.
 */
static void xhci_init_command_ring(struct xhci_priv *priv)
{
    ULONG i;
    ULONG addr;
    ULONG last;

    last = (ULONG)(XHCI_TRBS_PER_SEGMENT - 1U);
    for (i = 0UL; i < (ULONG)(XHCI_TRBS_PER_SEGMENT + XHCI_TRB_GUARD_TRBS); i++) {
        xhci_cmd_ring[i].param_lo = 0UL;
        xhci_cmd_ring[i].param_hi = 0UL;
        xhci_cmd_ring[i].status = 0UL;
        xhci_cmd_ring[i].control = 0UL;
    }

    addr = (ULONG)xhci_cmd_ring;
    xhci_cmd_ring[last].param_lo = addr;
    xhci_cmd_ring[last].param_hi = 0UL;
    xhci_cmd_ring[last].status = 0UL;
    xhci_cmd_ring[last].control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_LINK) | XHCI_TRB_LINK_TOGGLE;

    flush_data_cache((void *)xhci_cmd_ring,
                     (long)((ULONG)(XHCI_TRBS_PER_SEGMENT + XHCI_TRB_GUARD_TRBS) *
                            sizeof(xhci_trb_t)));

    /* CRCR's other low bits report or request command stop/abort state;
     * they must not be carried over from the controller's reset value. */
    xhci_writeq(priv->op_base, XHCI_OP_CRCR, addr | XHCI_TRB_CYCLE);
}

/*
 * Single-segment Event Ring. Unlike the Command Ring, the Event Ring has
 * no Link TRB -- the hardware walks segments through the ERST, not
 * in-ring links.  VL805's established Circle implementation programs
 * ERSTSZ, ERSTBA, then ERDP in that order.
 */
static void xhci_init_event_ring(struct xhci_priv *priv)
{
    ULONG i;
    ULONG addr;
    ULONG erstsz;

    xhci_event_ring = (xhci_trb_t *)raspi_get_coherent_buffer(COHERENT_TAG_XHCI_EVENT_RING);
    xhci_erst = (xhci_erst_entry_t *)raspi_get_coherent_buffer(COHERENT_TAG_XHCI_ERST);

    for (i = 0UL; i < (ULONG)(XHCI_TRBS_PER_SEGMENT + XHCI_TRB_GUARD_TRBS); i++) {
        xhci_event_ring[i].param_lo = 0UL;
        xhci_event_ring[i].param_hi = 0UL;
        xhci_event_ring[i].status = 0UL;
        xhci_event_ring[i].control = 0UL;
    }
    flush_data_cache((void *)xhci_event_ring,
                     (long)((ULONG)(XHCI_TRBS_PER_SEGMENT + XHCI_TRB_GUARD_TRBS) *
                            sizeof(xhci_trb_t)));

    addr = (ULONG)xhci_event_ring;
    xhci_erst[0].seg_addr_lo = addr;
    xhci_erst[0].seg_addr_hi = 0UL;
    xhci_erst[0].seg_size = (ULONG)XHCI_TRBS_PER_SEGMENT;
    xhci_erst[0].rsvd = 0UL;
    flush_data_cache((void *)xhci_erst, (long)sizeof(xhci_erst_entry_t));

    erstsz = xhci_readl(priv->rt_base, XHCI_RT_IR0_ERSTSZ);
    erstsz &= ~XHCI_ERSTSZ_SIZE_MASK;
    erstsz |= 1UL;
    xhci_writel(priv->rt_base, XHCI_RT_IR0_ERSTSZ, erstsz);
    xhci_writeq(priv->rt_base, XHCI_RT_IR0_ERSTBA, (ULONG)xhci_erst);
    xhci_writeq(priv->rt_base, XHCI_RT_IR0_ERDP, addr);
}

/*
 * HCSPARAMS2's Max Scratchpad Buffers field tells the driver how many
 * page-sized buffers the controller needs for internal use; if nonzero,
 * DCBAA[0] must point to an array of their addresses (xHCI spec 4.20,
 * verified against U-Boot's xhci_scratchpad_alloc()). PAGESIZE's lowest
 * set bit gives the actual page size as 4096 << bit_index -- this
 * driver's static buffers are sized for exactly 4096, so any other
 * reported page size is a clean failure rather than a silent
 * mis-sized allocation.
 */
static BOOL xhci_init_scratchpad(struct xhci_priv *priv)
{
    ULONG hcs2;
    ULONG num_sp;
    ULONG page_size_bits;
    ULONG page_size;
    ULONG i;
    ULONG addr;

    hcs2 = xhci_readl(priv->cap_base, XHCI_CAP_HCSPARAMS2);
    num_sp = XHCI_HCS2_MAX_SCRATCHPAD(hcs2);

    if (num_sp == 0UL) {
        return TRUE;
    }
    if (num_sp > (ULONG)XHCI_MAX_SCRATCHPAD_BUFS) {
        KINFO(("xhci: %lu scratchpad buffers required, only %lu supported\n",
               num_sp, (ULONG)XHCI_MAX_SCRATCHPAD_BUFS));
        return FALSE;
    }

    page_size_bits = xhci_readl(priv->op_base, XHCI_OP_PAGESIZE) & 0xffffUL;
    for (i = 0UL; i < 16UL; i++) {
        if (page_size_bits & 1UL)
            break;
        page_size_bits >>= 1;
    }
    if (i == 16UL) {
        KINFO(("xhci: PAGESIZE register reports no valid page size\n"));
        return FALSE;
    }
    page_size = 4096UL << i;
    if (page_size != XHCI_PAGE_SIZE) {
        KINFO(("xhci: unsupported hardware page size %lu (only %lu supported)\n",
               page_size, XHCI_PAGE_SIZE));
        return FALSE;
    }

    if (((ULONG)xhci_scratchpad_bufs & (XHCI_PAGE_SIZE - 1UL)) != 0UL) {
        KINFO(("xhci: scratchpad buffer pool is not page-aligned\n"));
        return FALSE;
    }

    for (i = 0UL; i < num_sp; i++) {
        addr = (ULONG)(xhci_scratchpad_bufs + (i * XHCI_PAGE_SIZE));
        xhci_scratchpad_array[i].lo = addr;
        xhci_scratchpad_array[i].hi = 0UL;
    }
    flush_data_cache((void *)xhci_scratchpad_bufs, (long)(num_sp * XHCI_PAGE_SIZE));
    flush_data_cache((void *)xhci_scratchpad_array, (long)(num_sp * sizeof(xhci_qword_t)));

    xhci_dcbaa[0].lo = (ULONG)xhci_scratchpad_array;
    xhci_dcbaa[0].hi = 0UL;
    flush_data_cache((void *)&xhci_dcbaa[0], (long)sizeof(xhci_qword_t));

    return TRUE;
}

static BOOL xhci_hw_start(struct xhci_priv *priv)
{
    ULONG cmd;
    ULONG crcr_before_run;
    ULONG usbsts_before_run;

    crcr_before_run = xhci_readl(priv->op_base, XHCI_OP_CRCR);
    usbsts_before_run = xhci_readl(priv->op_base, XHCI_OP_USBSTS);
    cmd = xhci_readl(priv->op_base, XHCI_OP_USBCMD);
    cmd |= XHCI_CMD_RUN;
    xhci_writel(priv->op_base, XHCI_OP_USBCMD, cmd);

    if (!xhci_wait_clear(priv->op_base, XHCI_OP_USBSTS, XHCI_STS_HALT,
                         XHCI_HALT_TIMEOUT_US)) {
        KINFO(("xhci: start state USBSTS-before=%08lx USBCMD=%08lx USBSTS=%08lx CONFIG=%08lx "
               "CRCR-before=%08lx CRCR=%08lx:%08lx DCBAAP=%08lx:%08lx "
               "ERSTSZ=%08lx ERSTBA=%08lx:%08lx ERDP=%08lx:%08lx\n",
               usbsts_before_run,
               xhci_readl(priv->op_base, XHCI_OP_USBCMD),
               xhci_readl(priv->op_base, XHCI_OP_USBSTS),
               xhci_readl(priv->op_base, XHCI_OP_CONFIG),
               crcr_before_run,
               xhci_readl(priv->op_base, XHCI_OP_CRCR + 4UL),
               xhci_readl(priv->op_base, XHCI_OP_CRCR),
               xhci_readl(priv->op_base, XHCI_OP_DCBAAP + 4UL),
               xhci_readl(priv->op_base, XHCI_OP_DCBAAP),
               xhci_readl(priv->rt_base, XHCI_RT_IR0_ERSTSZ),
               xhci_readl(priv->rt_base, XHCI_RT_IR0_ERSTBA + 4UL),
               xhci_readl(priv->rt_base, XHCI_RT_IR0_ERSTBA),
               xhci_readl(priv->rt_base, XHCI_RT_IR0_ERDP + 4UL),
               xhci_readl(priv->rt_base, XHCI_RT_IR0_ERDP)));
        return FALSE;
    }

    return TRUE;
}

static BOOL xhci_setup_has_host_error(struct xhci_priv *priv, const char *stage)
{
    ULONG status;

    status = xhci_readl(priv->op_base, XHCI_OP_USBSTS);
    if (status & XHCI_STS_HSE) {
        KINFO(("xhci: Host System Error after %s: USBSTS=%08lx HCS2=%08lx PAGESIZE=%08lx "
               "CONFIG=%08lx CRCR=%08lx:%08lx DCBAAP=%08lx:%08lx "
               "ERSTSZ=%08lx ERSTBA=%08lx:%08lx ERDP=%08lx:%08lx\n",
               stage,
               status,
               xhci_readl(priv->cap_base, XHCI_CAP_HCSPARAMS2),
               xhci_readl(priv->op_base, XHCI_OP_PAGESIZE),
               xhci_readl(priv->op_base, XHCI_OP_CONFIG),
               xhci_readl(priv->op_base, XHCI_OP_CRCR + 4UL),
               xhci_readl(priv->op_base, XHCI_OP_CRCR),
               xhci_readl(priv->op_base, XHCI_OP_DCBAAP + 4UL),
               xhci_readl(priv->op_base, XHCI_OP_DCBAAP),
               xhci_readl(priv->rt_base, XHCI_RT_IR0_ERSTSZ),
               xhci_readl(priv->rt_base, XHCI_RT_IR0_ERSTBA + 4UL),
               xhci_readl(priv->rt_base, XHCI_RT_IR0_ERSTBA),
               xhci_readl(priv->rt_base, XHCI_RT_IR0_ERDP + 4UL),
               xhci_readl(priv->rt_base, XHCI_RT_IR0_ERDP)));
        return TRUE;
    }

    return FALSE;
}

static void xhci_trace_ports(struct xhci_priv *priv)
{
    ULONG hcs1;
    ULONG hw_max_ports;
    ULONG n;
    ULONG port;
    ULONG value;
    ULONG speed;

    hcs1 = xhci_readl(priv->cap_base, XHCI_CAP_HCSPARAMS1);
    hw_max_ports = XHCI_HCS1_MAX_PORTS(hcs1);
    priv->max_ports = (UWORD)hw_max_ports;

    KINFO(("xhci: %lu root hub ports\n", hw_max_ports));

    n = (hw_max_ports < (ULONG)XHCI_MAX_PORTS_TRACED) ? hw_max_ports : (ULONG)XHCI_MAX_PORTS_TRACED;

    for (port = 0UL; port < n; port++) {
        value = xhci_readl(priv->op_base, XHCI_OP_PORTSC(port));
        speed = (value & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
        KINFO(("xhci: port %lu: connect=%lu enabled=%lu speed=%lu\n",
               port + 1UL,
               (value & XHCI_PORTSC_CCS) ? 1UL : 0UL,
               (value & XHCI_PORTSC_PED) ? 1UL : 0UL,
               speed));
    }
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
    if ((caplength < XHCI_CAP_LENGTH_MIN) || ((caplength & 3U) != 0U)) {
        KINFO(("xhci: invalid CAPLENGTH 0x%02x; MMIO read did not reach the controller\n",
               caplength));
        return EOPNOTSUPP;
    }
    priv->op_base = priv->cap_base + caplength;

    rtsoff = xhci_readl(priv->cap_base, XHCI_CAP_RTSOFF) & ~0x1fUL;
    priv->rt_base = priv->cap_base + rtsoff;

    if (!xhci_hw_reset(priv)) {
        return ETIMEDOUT;
    }
    KINFO(("xhci: controller reset complete\n"));

    xhci_configure_slots(priv);
    xhci_init_dcbaa();
    if (!xhci_init_scratchpad(priv)) {
        return EOPNOTSUPP;
    }
    if (xhci_setup_has_host_error(priv, "scratchpad setup"))
        return EOPNOTSUPP;
    xhci_init_command_ring(priv);
    if (xhci_setup_has_host_error(priv, "CRCR setup"))
        return EOPNOTSUPP;
    xhci_writeq(priv->op_base, XHCI_OP_DCBAAP, (ULONG)xhci_dcbaa);
    if (xhci_setup_has_host_error(priv, "DCBAAP setup"))
        return EOPNOTSUPP;
    xhci_init_event_ring(priv);
    if (xhci_setup_has_host_error(priv, "event-ring setup"))
        return EOPNOTSUPP;

    xhci_writel(priv->op_base, XHCI_OP_DNCTRL, 0UL);
    if (xhci_setup_has_host_error(priv, "DNCTRL setup"))
        return EOPNOTSUPP;

    if (!xhci_hw_start(priv)) {
        KINFO(("xhci: controller did not start\n"));
        return ETIMEDOUT;
    }
    KINFO(("xhci: controller running\n"));

    xhci_trace_ports(priv);

    KINFO(("xhci: bring-up complete; transfer support is not implemented yet\n"));

    return E_OK;
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
