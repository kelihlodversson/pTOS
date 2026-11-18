#!/bin/sh
# Explicit private test configurations; never change shipped capability gates.
set -eu
case "${1:-}:${2:-}" in
    virt-m68k-aspace:fat|virt-m68k-aspace:pfs) ;;
    *) echo "usage: $0 virt-m68k-aspace fat|pfs (68k compatibility only, #1018)" >&2; exit 2 ;;
esac
export SHARED_VM_PEXEC_TEST=1 SHARED_VM_PEXEC_FS=$2
exec bash tools/ci-regression.sh "$1"
