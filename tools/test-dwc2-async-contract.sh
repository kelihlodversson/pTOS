#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
source=$repo_root/usb/ucd_dwc2.c

require()
{
    if ! rg -q "$1" "$source"; then
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

echo 'dwc2 async contract test passed'
