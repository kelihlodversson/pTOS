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
#include "raspi_int.h"
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
#define PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1 0x0188UL
#define PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1_ENDIAN_MODE_BAR2_MASK 0x0000000cUL
#define PCIE_RC_CFG_VENDOR_SPECIFIC_REG1_LITTLE_ENDIAN 0x00000000UL

#define PCIE_MISC_MISC_CTRL             0x4008UL
#define PCIE_MISC_MISC_CTRL_PCIE_RCB_64B_MODE_MASK 0x00000080UL
#define PCIE_MISC_MISC_CTRL_PCIE_RCB_MPS_MODE_MASK 0x00000400UL
#define PCIE_MISC_MISC_CTRL_SCB_ACCESS_EN_MASK 0x00001000UL
#define PCIE_MISC_MISC_CTRL_CFG_READ_UR_MODE_MASK 0x00002000UL
#define PCIE_MISC_MISC_CTRL_MAX_BURST_SIZE_MASK 0x00300000UL
#define PCIE_MISC_MISC_CTRL_SCB0_SIZE_MASK 0xf8000000UL

#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LO 0x400cUL
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_HI 0x4010UL
#define PCIE_MISC_RC_BAR1_CONFIG_LO     0x402cUL
#define PCIE_MISC_PCIE_CTRL             0x4064UL
#define PCIE_MISC_PCIE_CTRL_PCIE_PERSTB_MASK 0x00000004UL
#define PCIE_MISC_PCIE_STATUS           0x4068UL
#define PCIE_MISC_PCIE_STATUS_PCIE_DL_ACTIVE_MASK 0x20UL
#define PCIE_MISC_PCIE_STATUS_PCIE_PHYLINKUP_MASK 0x10UL
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT 0x4070UL
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT_LIMIT_MASK 0xfff00000UL
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT_BASE_MASK 0x0000fff0UL
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI 0x4080UL
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI 0x4084UL
#define PCIE_MISC_HARD_PCIE_HARD_DEBUG  0x4204UL
#define PCIE_MISC_HARD_PCIE_HARD_DEBUG_SERDES_IDDQ_MASK 0x08000000UL
#define PCIE_RGR1_SW_INIT_1             0x9210UL
#define PCIE_RGR1_SW_INIT_1_PERST_MASK  0x00000001UL
#define RGR1_SW_INIT_1_INIT_GENERIC_MASK 0x00000002UL

#define RASPI_PCIE_OUTBOUND_CPU_BASE_LO 0x00000000UL
#define RASPI_PCIE_OUTBOUND_CPU_BASE_HI 0x00000006UL
#define RASPI_PCIE_INBOUND_SIZE         0x80000000UL
#define RASPI_PCIE_INBOUND_SIZE_CODE    16U
#define RASPI_PCIE_LINK_WAIT_LOOPS      20U

#define PCIE_EXT_CFG_INDEX              0x9000UL
#define PCIE_EXT_CFG_DATA               0x8000UL

#define PCI_ECAM_REG(reg)               ((reg) & 0xfffU)
#define PCI_ECAM_OFFSET(bus, dev, func, reg) \
    (((ULONG)(bus) << 20) | ((ULONG)(dev) << 15) | ((ULONG)(func) << 12) | PCI_ECAM_REG(reg))

/*
 * BCM2711 PCIe INTx routing.
 *
 * Unlike the MSI path (PCIE_MSI_INTR2_BASE, unused here), the RC has no
 * software INTx status/mask/ack register block: each of the four INTx
 * pins is wired to its own dedicated GIC SPI, fixed by the SoC's
 * interrupt-map (see Linux's arch/arm/boot/dts/broadcom/bcm2711.dtsi,
 * the pcie0 node):
 *
 *   INTA -> GIC SPI 143   INTB -> GIC SPI 144
 *   INTC -> GIC SPI 145   INTD -> GIC SPI 146
 *
 * raspi_gic_connect_irq() takes GICD interrupt IDs, which are SPI number
 * + 32, so INTA..INTD are GIC IDs 175..178. Masking is the GIC's own
 * GICD_ISENABLER/ICENABLER (done inside raspi_gic_connect_irq), and
 * acknowledgement is the standard GICC_IAR/EOIR cycle already performed
 * generically by raspi_gic_handle_irq() -- there is nothing extra to
 * mask or ack at the PCIe RC itself.
 *
 * A GIC SPI line may be shared: the RPi4 exposes a single root port, but
 * a multi-function device sitting in that one slot can have several
 * functions asserting the same INTx pin (interrupt-map-mask wildcards
 * the device/function fields, matching on pin alone), and pTOS already
 * enumerates every function of a device. Each line therefore fans out to
 * up to RASPI_PCIE_INTX_MAX_SHARERS hooked handlers, called unconditionally
 * on every event -- same as any shared level-triggered PCI INTx line, each
 * driver is expected to check its own device and no-op if it is not the
 * source. The GIC line is enabled while any sharer is hooked and disabled
 * once the last one unhooks.
 */
#define RASPI_PCIE_INTX_LINES  4U
#define RASPI_PCIE_INTX_MAX_SHARERS 8U
#define RASPI_PCIE_INTX_GIC_IRQ(pin) (175U + ((pin) - 1U))

static struct {
    PCI_HANDLE handle;
    pci_interrupt_handler_t handler;
    void *param;
} raspi_pci_intx_hooks[RASPI_PCIE_INTX_LINES][RASPI_PCIE_INTX_MAX_SHARERS];

static void raspi_pci_intx_dispatch(UWORD line_idx)
{
    UWORD i;

    for (i = 0; i < RASPI_PCIE_INTX_MAX_SHARERS; i++)
    {
        if (raspi_pci_intx_hooks[line_idx][i].handler)
            raspi_pci_intx_hooks[line_idx][i].handler(raspi_pci_intx_hooks[line_idx][i].param);
    }
}

static void raspi_pci_intx_isr_a(void) { raspi_pci_intx_dispatch(0); }
static void raspi_pci_intx_isr_b(void) { raspi_pci_intx_dispatch(1); }
static void raspi_pci_intx_isr_c(void) { raspi_pci_intx_dispatch(2); }
static void raspi_pci_intx_isr_d(void) { raspi_pci_intx_dispatch(3); }

static const PFVOID raspi_pci_intx_isr[RASPI_PCIE_INTX_LINES] = {
    raspi_pci_intx_isr_a,
    raspi_pci_intx_isr_b,
    raspi_pci_intx_isr_c,
    raspi_pci_intx_isr_d
};

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

static ULONG raspi_pci_field_shift(ULONG mask)
{
    ULONG shift;

    shift = 0UL;
    while (((mask >> shift) & 1UL) == 0UL)
        shift++;
    return shift;
}

static ULONG raspi_pci_replace_bits(ULONG original, ULONG value, ULONG mask)
{
    ULONG shift;

    shift = raspi_pci_field_shift(mask);
    original &= ~mask;
    original |= (value << shift) & mask;
    return original;
}

static void raspi_pci_update_bits(ULONG offset, ULONG mask, ULONG value)
{
    ULONG reg;

    reg = raspi_pci_readl(offset);
    reg &= ~mask;
    reg |= value & mask;
    raspi_pci_writel(offset, reg);
}

static BOOL raspi_pci_link_up(void)
{
    ULONG status;

    status = raspi_pci_readl(PCIE_MISC_PCIE_STATUS);
    return ((status & PCIE_MISC_PCIE_STATUS_PCIE_DL_ACTIVE_MASK) != 0UL) &&
           ((status & PCIE_MISC_PCIE_STATUS_PCIE_PHYLINKUP_MASK) != 0UL);
}

static void raspi_pci_set_bridge_reset(BOOL asserted)
{
    ULONG value;

    value = asserted ? RGR1_SW_INIT_1_INIT_GENERIC_MASK : 0UL;
    raspi_pci_update_bits(PCIE_RGR1_SW_INIT_1, RGR1_SW_INIT_1_INIT_GENERIC_MASK, value);
}

static void raspi_pci_set_perst(BOOL asserted)
{
    ULONG value;

    value = asserted ? PCIE_RGR1_SW_INIT_1_PERST_MASK : 0UL;
    raspi_pci_update_bits(PCIE_RGR1_SW_INIT_1, PCIE_RGR1_SW_INIT_1_PERST_MASK, value);
}

static void raspi_pci_set_outbound_window(void)
{
    ULONG cpu_mb;
    ULONG limit_mb;
    ULONG reg;

    raspi_pci_writel(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LO, RASPI_PCIE_MMIO_BUS_BASE);
    raspi_pci_writel(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_HI, 0UL);

    cpu_mb = RASPI_PCIE_OUTBOUND_CPU_BASE_LO >> 20;
    limit_mb = (RASPI_PCIE_OUTBOUND_CPU_BASE_LO + RASPI_PCIE_MMIO_SIZE - 1UL) >> 20;

    reg = raspi_pci_readl(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT);
    reg = raspi_pci_replace_bits(reg, cpu_mb, PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT_BASE_MASK);
    reg = raspi_pci_replace_bits(reg, limit_mb, PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT_LIMIT_MASK);
    raspi_pci_writel(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT, reg);

    raspi_pci_writel(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI, RASPI_PCIE_OUTBOUND_CPU_BASE_HI);
    raspi_pci_writel(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI, RASPI_PCIE_OUTBOUND_CPU_BASE_HI);
}

static void raspi_pci_set_inbound_window(void)
{
    ULONG reg;

    reg = RASPI_PCIE_DMA_BUS_BASE;
    reg |= RASPI_PCIE_INBOUND_SIZE_CODE;
    raspi_pci_writel(PCIE_MISC_RC_BAR1_CONFIG_LO + 8UL, reg);
    raspi_pci_writel(PCIE_MISC_RC_BAR1_CONFIG_LO + 12UL, 0UL);
}

static void raspi_pci_set_root_bridge_class(void)
{
    ULONG reg;

    reg = raspi_pci_readl(PCIE_RC_CFG_PRIV1_ID_VAL3);
    reg &= ~PCIE_RC_CFG_PRIV1_ID_VAL3_CLASS_CODE_MASK;
    reg |= 0x00060400UL;
    raspi_pci_writel(PCIE_RC_CFG_PRIV1_ID_VAL3, reg);
}

static volatile UBYTE *raspi_pci_config_ptr(UBYTE bus, UBYTE dev, UBYTE func, UWORD reg)
{
    ULONG index;

    /*
     * BCM2711 topology:
     *
     * bus 0: root complex, device 0 only
     * bus 1: immediate downstream device, device 0 only
     *
     * Accessing other device numbers on either bus is invalid and may
     * provoke bad controller behaviour rather than returning 0xffff.
     */
    if ((bus < 2U) && (dev != 0U))
        return 0;

    if (bus == 0U) {
        if (func != 0U)
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
    prop_tag_2u32_t power_state;
    ULONG reg;
    UWORD i;

    raspi_pci_link_ready = FALSE;

    power_state.value1 = DEVICE_ID_USB_HCD;
    power_state.value2 = POWER_STATE_ON | POWER_STATE_WAIT;
    if (!raspi_prop_get_tag(PROPTAG_SET_POWER_STATE, &power_state, sizeof power_state, sizeof(ULONG) * 2)) {
        KINFO(("pci: RPi4 USB power-state request failed\n"));
        return PCI_GENERAL_ERROR;
    }
    if (power_state.value2 & POWER_STATE_NO_DEVICE) {
        KINFO(("pci: RPi4 USB power domain not found\n"));
        return PCI_DEVICE_NOT_FOUND;
    }
    if (!(power_state.value2 & POWER_STATE_ON)) {
        KINFO(("pci: RPi4 USB power domain did not power on\n"));
        return PCI_GENERAL_ERROR;
    }

    raspi_pci_set_bridge_reset(TRUE);
    raspi_pci_set_perst(TRUE);
    raspi_delay_us(200UL);

    raspi_pci_set_bridge_reset(FALSE);

    reg = raspi_pci_readl(PCIE_MISC_HARD_PCIE_HARD_DEBUG);
    reg &= ~PCIE_MISC_HARD_PCIE_HARD_DEBUG_SERDES_IDDQ_MASK;
    raspi_pci_writel(PCIE_MISC_HARD_PCIE_HARD_DEBUG, reg);
    raspi_delay_us(200UL);

    reg = raspi_pci_readl(PCIE_MISC_MISC_CTRL);
    reg = raspi_pci_replace_bits(reg, 1UL, PCIE_MISC_MISC_CTRL_SCB_ACCESS_EN_MASK);
    reg = raspi_pci_replace_bits(reg, 1UL, PCIE_MISC_MISC_CTRL_CFG_READ_UR_MODE_MASK);
    reg = raspi_pci_replace_bits(reg, 0UL, PCIE_MISC_MISC_CTRL_MAX_BURST_SIZE_MASK);
    reg = raspi_pci_replace_bits(reg, 1UL, PCIE_MISC_MISC_CTRL_PCIE_RCB_MPS_MODE_MASK);
    reg = raspi_pci_replace_bits(reg, 1UL, PCIE_MISC_MISC_CTRL_PCIE_RCB_64B_MODE_MASK);
    reg = raspi_pci_replace_bits(reg, RASPI_PCIE_INBOUND_SIZE_CODE, PCIE_MISC_MISC_CTRL_SCB0_SIZE_MASK);
    raspi_pci_writel(PCIE_MISC_MISC_CTRL, reg);

    raspi_pci_set_inbound_window();
    raspi_pci_set_outbound_window();
    raspi_pci_set_root_bridge_class();

    reg = raspi_pci_readl(PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1);
    reg &= ~PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1_ENDIAN_MODE_BAR2_MASK;
    reg |= PCIE_RC_CFG_VENDOR_SPECIFIC_REG1_LITTLE_ENDIAN;
    raspi_pci_writel(PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1, reg);

    raspi_pci_set_perst(FALSE);
    for (i = 0; i < RASPI_PCIE_LINK_WAIT_LOOPS; i++) {
        raspi_delay_us(5000UL);
        if (raspi_pci_link_up()) {
            raspi_pci_link_ready = TRUE;
            KINFO(("pci: RPi4 PCIe link up\n"));
            return PCI_SUCCESSFUL;
        }
    }

    KINFO(("pci: RPi4 PCIe link down\n"));
    return PCI_DEVICE_NOT_FOUND;
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

    return PCI_BACKEND_UNMAPPABLE;
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
    UBYTE pin;
    UWORD line_idx;
    UWORD i;
    LONG ret;

    (void)line;

    ret = pci_read_config_byte(handle, PCI_CONFIG_INTERRUPT_PIN, &pin);
    if (ret != PCI_SUCCESSFUL)
        return ret;
    if ((pin < 1U) || (pin > RASPI_PCIE_INTX_LINES))
        return PCI_FUNC_NOT_SUPPORTED;

    line_idx = pin - 1U;
    for (i = 0; i < RASPI_PCIE_INTX_MAX_SHARERS; i++)
    {
        if (raspi_pci_intx_hooks[line_idx][i].handle == handle)
            return PCI_GENERAL_ERROR;
    }
    for (i = 0; i < RASPI_PCIE_INTX_MAX_SHARERS; i++)
    {
        if (raspi_pci_intx_hooks[line_idx][i].handler == 0)
            break;
    }
    if (i == RASPI_PCIE_INTX_MAX_SHARERS)
        return PCI_GENERAL_ERROR;

    /*
     * handler is the field the ISR dispatcher gates on (see
     * raspi_pci_intx_dispatch); write it last so a concurrent interrupt
     * on an already-enabled shared line never observes a hooked entry
     * with a stale param.
     */
    raspi_pci_intx_hooks[line_idx][i].handle = handle;
    raspi_pci_intx_hooks[line_idx][i].param = param;
    raspi_pci_intx_hooks[line_idx][i].handler = handler;
    raspi_gic_connect_irq(RASPI_PCIE_INTX_GIC_IRQ(pin), raspi_pci_intx_isr[line_idx]);

    return PCI_SUCCESSFUL;
}

static LONG raspi_pci_unhook_interrupt(PCI_HANDLE handle, UBYTE line)
{
    UBYTE pin;
    UWORD line_idx;
    UWORD i;
    UWORD j;
    BOOL any_left;
    LONG ret;

    (void)line;

    ret = pci_read_config_byte(handle, PCI_CONFIG_INTERRUPT_PIN, &pin);
    if (ret != PCI_SUCCESSFUL)
        return ret;
    if ((pin < 1U) || (pin > RASPI_PCIE_INTX_LINES))
        return PCI_FUNC_NOT_SUPPORTED;

    line_idx = pin - 1U;
    for (i = 0; i < RASPI_PCIE_INTX_MAX_SHARERS; i++)
    {
        if (raspi_pci_intx_hooks[line_idx][i].handle == handle)
            break;
    }
    if (i == RASPI_PCIE_INTX_MAX_SHARERS)
        return PCI_GENERAL_ERROR;

    /*
     * PCI INTx is level-triggered: if this is the last sharer, disable
     * the GIC line before clearing its entry, not after, so a line
     * asserted right at unhook time can never retrigger the ISR against
     * an already-empty hook table.
     */
    any_left = FALSE;
    for (j = 0; j < RASPI_PCIE_INTX_MAX_SHARERS; j++)
    {
        if ((j != i) && raspi_pci_intx_hooks[line_idx][j].handler)
        {
            any_left = TRUE;
            break;
        }
    }
    if (!any_left)
        raspi_gic_connect_irq(RASPI_PCIE_INTX_GIC_IRQ(pin), 0);

    raspi_pci_intx_hooks[line_idx][i].handle = PCI_HANDLE_NONE;
    raspi_pci_intx_hooks[line_idx][i].handler = 0;
    raspi_pci_intx_hooks[line_idx][i].param = 0;

    return PCI_SUCCESSFUL;
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
