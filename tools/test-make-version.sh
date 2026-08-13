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

expect_rejected()
{
    version=$1
    log=make-$version.log

    if make -f Makefile MAKE_VERSION=$version help >$log 2>&1; then
        echo "Makefile accepted GNU Make $version"
        exit 1
    fi

    if ! grep -F 'GNU Make 4.3 or later is required' $log >/dev/null; then
        cat $log
        echo "GNU Make $version did not report the required version diagnostic"
        exit 1
    fi
}

expect_accepted()
{
    version=$1
    log=make-$version.log

    if make -f Makefile MAKE_VERSION=$version help >$log 2>&1; then
        echo "GNU Make $version unexpectedly completed the incomplete temporary build"
        exit 1
    fi

    if grep -F 'GNU Make 4.3 or later is required' $log >/dev/null; then
        cat $log
        echo "GNU Make $version was rejected by the version guard"
        exit 1
    fi
}

expect_rejected 3.81
expect_rejected 4.0
expect_rejected 4.2
expect_accepted 4.3
expect_accepted 4.4
expect_accepted 4.10

echo 'GNU Make version test passed'
