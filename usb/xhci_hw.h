/*
 * xhci_hw.h - xHCI register layout and TRB definitions (BCM2711/VL805)
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef XHCI_HW_H
#define XHCI_HW_H

#include "portab.h"

/* Capability registers (from the controller's BAR base, read-only) */
#define XHCI_CAP_CAPLENGTH   0x00U   /* UBYTE */
#define XHCI_CAP_HCSPARAMS1  0x04U   /* ULONG */
#define XHCI_CAP_HCSPARAMS2  0x08U   /* ULONG */
#define XHCI_CAP_HCSPARAMS3  0x0cU   /* ULONG */
#define XHCI_CAP_HCCPARAMS1  0x10U   /* ULONG */
#define XHCI_CAP_DBOFF       0x14U   /* ULONG */
#define XHCI_CAP_RTSOFF      0x18U   /* ULONG */
#define XHCI_CAP_LENGTH_MIN  0x20U

/* HCSPARAMS1 fields */
#define XHCI_HCS1_MAX_SLOTS(p)   ((p) & 0xffUL)
#define XHCI_HCS1_MAX_PORTS(p)   (((p) >> 24) & 0xffUL)

/* HCSPARAMS2 Max Scratchpad Buffers: hi 5 bits at 21:25, lo 5 bits at 27:31 */
#define XHCI_HCS2_MAX_SCRATCHPAD(p) \
    ((((p) >> 16) & 0x3e0UL) | (((p) >> 27) & 0x1fUL))

/* Operational registers (from base + CAPLENGTH) */
#define XHCI_OP_USBCMD    0x00U   /* ULONG */
#define XHCI_OP_USBSTS    0x04U   /* ULONG */
#define XHCI_OP_PAGESIZE  0x08U   /* ULONG */
#define XHCI_OP_DNCTRL    0x14U   /* ULONG */
#define XHCI_OP_CRCR      0x18U   /* 64-bit: lo at +0x18, hi at +0x1c */
#define XHCI_OP_DCBAAP    0x30U   /* 64-bit: lo at +0x30, hi at +0x34 */
#define XHCI_OP_CONFIG    0x38U   /* ULONG */
#define XHCI_CONFIG_SLOTS_MASK 0x000000ffUL
/* Port register sets start at +0x400, 16 bytes each; n is 0-based */
#define XHCI_OP_PORTSC(n) (0x400U + (0x10U * (n)))

/* USBCMD bits */
#define XHCI_CMD_RUN     0x00000001UL
#define XHCI_CMD_RESET   0x00000002UL

/* USBSTS bits */
#define XHCI_STS_HALT    0x00000001UL
#define XHCI_STS_HSE     0x00000004UL
#define XHCI_STS_CNR     0x00000800UL

/* PORTSC bits (read-only status subset needed for bring-up tracing) */
#define XHCI_PORTSC_CCS          0x00000001UL   /* Current Connect Status */
#define XHCI_PORTSC_PED          0x00000002UL   /* Port Enabled/Disabled */
#define XHCI_PORTSC_SPEED_SHIFT  10U
#define XHCI_PORTSC_SPEED_MASK   (0xfUL << XHCI_PORTSC_SPEED_SHIFT)

/* Runtime registers (from base + RTSOFF), Interrupter Register Set 0 at +0x20 */
#define XHCI_RT_IR0_IMAN    0x20U   /* ULONG */
#define XHCI_RT_IR0_IMOD    0x24U   /* ULONG */
#define XHCI_RT_IR0_ERSTSZ  0x28U   /* ULONG */
#define XHCI_RT_IR0_ERSTBA  0x30U   /* 64-bit: lo at +0x30, hi at +0x34 */
#define XHCI_RT_IR0_ERDP    0x38U   /* 64-bit: lo at +0x38, hi at +0x3c */
#define XHCI_ERSTSZ_SIZE_MASK 0x0000ffffUL

/* TRB: 16 bytes, 4 dwords. control bit 0 = Cycle, bits 10-15 = Type. */
typedef struct {
    ULONG param_lo;
    ULONG param_hi;
    ULONG status;
    ULONG control;
} xhci_trb_t;

#define XHCI_TRB_CYCLE          0x00000001UL
#define XHCI_TRB_LINK_TOGGLE    0x00000002UL
#define XHCI_TRB_TYPE_SHIFT     10U
#define XHCI_TRB_TYPE(t)        (((ULONG)(t)) << XHCI_TRB_TYPE_SHIFT)
#define XHCI_TRB_TYPE_LINK      6UL

/* A plain 64-bit DMA pointer stored in RAM (DCBAA entries, scratchpad array
 * entries) -- not a register, just memory the controller reads via DMA. */
typedef struct {
    ULONG lo;
    ULONG hi;
} xhci_qword_t;

/* One Event Ring Segment Table entry */
typedef struct {
    ULONG seg_addr_lo;
    ULONG seg_addr_hi;
    ULONG seg_size;
    ULONG rsvd;
} xhci_erst_entry_t;

/* VL805 can prefetch beyond a ring segment.  Keep a zeroed, unlinked 4 KiB
 * guard after each 4 KiB logical segment, as Linux's XHCI_TRB_OVERFETCH quirk
 * does for this controller. */
#define XHCI_TRBS_PER_SEGMENT    256U
#define XHCI_TRB_GUARD_TRBS      256U
#define XHCI_TRB_SEGMENT_ALIGN   8192U
#define XHCI_MAX_SLOTS_ENABLED   8U
#define XHCI_MAX_DCBAA_SLOTS     32U
#define XHCI_MAX_PORTS_TRACED    8U
#define XHCI_MAX_SCRATCHPAD_BUFS 31U
#define XHCI_DMA_ALIGN           128U
#define XHCI_PAGE_SIZE           4096UL

/* Handshake timeouts, in microseconds -- exact values verified against a
 * real xHCI driver (U-Boot's XHCI_MAX_HALT_USEC/XHCI_MAX_RESET_USEC). */
#define XHCI_HALT_TIMEOUT_US     16000UL
#define XHCI_RESET_TIMEOUT_US    250000UL

#endif /* XHCI_HW_H */
