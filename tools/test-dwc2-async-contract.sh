#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
source=$repo_root/usb/ucd_dwc2.c
usb_source=$repo_root/usb/usb.c

require()
{
    if ! rg -q "$1" "$source"; then
        echo "missing async DWC2 contract: $2"
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
require_multiline 'gintsts = readl\(&regs->gintsts\);[\s\S]*if \(priv->shutting_down\)[\s\S]*return;[\s\S]*pending = readl\(&regs->host_regs.haint\)' \
    'IRQ returns during shutdown before handling async channel interrupts'
require 'ULONG ssplit_frame' 'per-slot start-split frame'
require 'dwc2_async_schedule_split_complete' 'deferred complete-split scheduling'
require_multiline 'slot->ssplit_frame = readl\(&regs->host_regs.hfnum\)[\s\S]*DWC2_ASYNC_SPLIT_COMPLETE[\s\S]*dwc2_async_schedule_split_complete' \
    'start-split ACK records its frame before scheduling the complete split'
require_multiline '\(frame - slot->ssplit_frame\)[\s\S]*> 4[\s\S]*slot->split_state = DWC2_ASYNC_SPLIT_START[\s\S]*dwc2_async_schedule' \
    'complete-split NYET retry window is limited to four microframes'

if ! rg -U -q 'for \(i = 0; i < USB_MAX_DEVICE; i\+\+\)[\s\S]*usb_disconnect\(dev\);[\s\S]*for \(a = allucdifs' "$usb_source"; then
    echo 'missing USB teardown contract: disconnect devices before stopping controller'
    exit 1
fi
if ! rg -q 'if \(dev->children\[i\]\)' "$usb_source"; then
    echo 'missing USB teardown contract: ignore empty child slots'
    exit 1
fi

echo 'dwc2 async contract test passed'
