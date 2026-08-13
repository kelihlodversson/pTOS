#!/bin/sh

set -eu

if ! command -v rg >/dev/null 2>&1; then
    echo 'tools/test-dwc2-async-contract.sh requires ripgrep (rg)'
    exit 1
fi

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
source=$repo_root/usb/ucd_dwc2.c
usb_source=$repo_root/usb/usb.c
usb_header=$repo_root/usb/usb_global.h
usb_kconfig=$repo_root/usb/Kconfig
mouse_source=$repo_root/usb/udd_mouse.c
keyboard_source=$repo_root/usb/udd_keyboard.c

require()
{
    if ! rg -q "$1" "$source"; then
        echo "missing async DWC2 contract: $2"
        exit 1
    fi
}

require_file()
{
    if ! rg -q "$1" "$2"; then
        echo "missing async DWC2 contract: $3"
        exit 1
    fi
}

require_multiline()
{
    if ! rg -U -q "$1" "$source"; then
        echo "missing async DWC2 contract: $2"
        exit 1
    fi
}

require 'next_frame' 'per-slot next eligible frame'
require 'dwc2_async_interval' 'USB speed-aware interval conversion'
require 'DWC2_GINTMSK_SOFINTR' 'SOF wakeup for deferred transfers'
require 'dwc2_async_schedule' 'NAK and completion scheduling'
require 'error_pending' 'post-halt error callback state'
require 'struct usb_async_int_msg \*msg = slot->msg' \
    'saved request for post-release error callback'
require 'msg->callback\(msg, -1, 0\)' 'error callback after slot release'
require 'wait_for_bit_le32\(&hc->hcint, DWC2_HCINT_CHHLTD' \
    'shutdown halt wait'
require 'BOOL shutting_down' 'controller shutdown state'
require 'priv->shutting_down' 'shutdown state guards'
require 'return ETIMEDOUT' 'halt timeout defers controller reset'
require '!priv->shutting_down' 'callback and rearm shutdown guard'
require 'if \(!priv->shutting_down\)' 'synchronous shutdown halt observer'
require_multiline 'gintsts = readl\(&regs->gintsts\);[\s\S]*if \(priv->shutting_down\) \{[\s\S]*writel\(gintsts & \(DWC2_GINTSTS_HCINTR \| DWC2_GINTSTS_SOFINTR\),[\s\S]*return;' \
    'IRQ acknowledges pending sources during shutdown without channel handling'
require_multiline 'priv->shutting_down = TRUE;[\s\S]*clrbits_le32\(&priv->regs->gintmsk, DWC2_GINTMSK_SOFINTR\);[\s\S]*clrbits_le32\(&priv->regs->gintmsk, DWC2_GINTMSK_HCINTR\);[\s\S]*for \(index = 0; index < DWC2_ASYNC_SLOT_COUNT; index\+\+\)' \
    'shutdown masks DWC2 IRQ sources before draining async slots'
require_multiline 'ret = dwc2_async_shutdown\(priv, &priv->async\[index\]\);[\s\S]*if \(ret\)[\s\S]*break;[\s\S]*clrbits_le32\(&priv->regs->gahbcfg, DWC2_GAHBCFG_GLBLINTRMSK\);[\s\S]*raspi_connect_irq\(ARM_IRQ_USB, NULL\);[\s\S]*return ret;' \
    'shutdown disconnects global IRQ handling after a slot halt failure'
require_multiline 'actual_length = msg->transfer_len -[\s\S]*if \(actual_length < 0 \|\| actual_length > msg->transfer_len\) \{[\s\S]*dwc2_async_finish_error\(priv, slot\);[\s\S]*return;' \
    'async completion validates DMA length before cache and copy'
require 'ULONG ssplit_frame' 'per-slot start-split frame'
require 'dwc2_async_schedule_split_complete' 'deferred complete-split scheduling'
require_multiline 'slot->ssplit_frame = readl\(&regs->host_regs.hfnum\)[\s\S]*DWC2_ASYNC_SPLIT_COMPLETE[\s\S]*dwc2_async_schedule_split_complete' \
    'start-split ACK records its frame before scheduling the complete split'
require_multiline '\(frame - slot->ssplit_frame\)[\s\S]*> 4[\s\S]*slot->split_state = DWC2_ASYNC_SPLIT_START[\s\S]*dwc2_async_schedule' \
    'complete-split NYET retry window is limited to four microframes'
require_file 'config CONF_DEBUG_USB_ASYNC' "$usb_kconfig" \
    'shared USB async trace option'
if ! rg -U -q 'config CONF_DEBUG_USB_ASYNC[\s\S]*default n[\s\S]*help' "$usb_kconfig"; then
    echo 'missing async DWC2 contract: async trace disabled by default'
    exit 1
fi
if ! rg -U -q '#if CONF_DEBUG_USB_ASYNC[\s\S]*#define KINFO_USB_ASYNC\(a\) KINFO\(a\)[\s\S]*#else[\s\S]*#define KINFO_USB_ASYNC\(a\) \(\(void\)0\)[\s\S]*#endif' "$usb_header"; then
    echo 'missing async DWC2 contract: compile-time USB async trace gate'
    exit 1
fi
require_multiline 'KINFO_USB_ASYNC\(\("dwc2_async: start[\s\S]*KINFO_USB_ASYNC\(\("dwc2_async: complete[\s\S]*KINFO_USB_ASYNC\(\("dwc2_async: split-start-ack[\s\S]*KINFO_USB_ASYNC\(\("dwc2_async: split-complete-nyet[\s\S]*KINFO_USB_ASYNC\(\("dwc2_async: split-complete-nak[\s\S]*KINFO_USB_ASYNC\(\("dwc2_async: rearm' \
    'DWC2 high-frequency async traces use the shared trace gate'

if ! rg -U -q 'for \(i = 0; i < USB_MAX_DEVICE; i\+\+\)[\s\S]*usb_disconnect\(dev\);[\s\S]*for \(a = allucdifs' "$usb_source"; then
    echo 'missing USB teardown contract: disconnect devices before stopping controller'
    exit 1
fi
if ! rg -q 'if \(dev->children\[i\]\)' "$usb_source"; then
    echo 'missing USB teardown contract: ignore empty child slots'
    exit 1
fi
if ! rg -U -q 'static void mouse_report_complete\([\s\S]*\{[\s\S]*\(void\)msg;' "$mouse_source"; then
    echo 'missing async DWC2 contract: mouse callback marks request unused'
    exit 1
fi
if ! rg -U -q 'static void keyboard_report_complete\([\s\S]*\{[\s\S]*\(void\)msg;' "$keyboard_source"; then
    echo 'missing async DWC2 contract: keyboard callback marks request unused'
    exit 1
fi

echo 'dwc2 async contract test passed'
