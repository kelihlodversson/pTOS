#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tmpdir=${TMPDIR:-/tmp}/ptos-make-version-test.$$

cleanup()
{
    rm -rf "$tmpdir"
}

trap cleanup EXIT HUP INT TERM

mkdir -p "$tmpdir"
cp "$repo_root/Makefile" "$tmpdir/Makefile"

cd "$tmpdir"

if make -f Makefile MAKE_VERSION=4.2 help >make-4.2.log 2>&1; then
    echo 'Makefile accepted GNU Make 4.2'
    exit 1
fi

if ! grep -F 'GNU Make 4.3 or later is required' make-4.2.log >/dev/null; then
    cat make-4.2.log
    echo 'GNU Make 4.2 did not report the required version diagnostic'
    exit 1
fi

if make -f Makefile MAKE_VERSION=4.3 help >make-4.3.log 2>&1; then
    echo 'GNU Make 4.3 unexpectedly completed the incomplete temporary build'
    exit 1
fi

if grep -F 'GNU Make 4.3 or later is required' make-4.3.log >/dev/null; then
    cat make-4.3.log
    echo 'GNU Make 4.3 was rejected by the version guard'
    exit 1
fi

echo 'GNU Make version test passed'
