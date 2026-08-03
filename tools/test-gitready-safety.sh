#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tmpdir=${TMPDIR:-/tmp}/ptos-gitready-test.$$

cleanup()
{
    rm -rf "$tmpdir"
}

trap cleanup EXIT HUP INT TERM

mkdir -p "$tmpdir/src" "$tmpdir/tools" "$tmpdir/po"
cp "$repo_root/Makefile" "$tmpdir/Makefile"
cp "$repo_root/version.mk" "$tmpdir/version.mk"
cp "$repo_root/country.mk" "$tmpdir/country.mk"
cp "$repo_root/release.mk" "$tmpdir/release.mk"
cp "$repo_root/tools/kconfig.mk" "$tmpdir/tools/kconfig.mk"
cp "$repo_root/po/POTFILES.in" "$tmpdir/po/POTFILES.in"
for dir in aes bdos bios cli desk usb util vdi; do
    mkdir -p "$tmpdir/$dir"
    cp "$repo_root/$dir/build.mk" "$tmpdir/$dir/build.mk"
done
if [ -f "$repo_root/tools/check-gitready.sh" ]; then
    cp "$repo_root/tools/check-gitready.sh" "$tmpdir/tools/check-gitready.sh"
    chmod +x "$tmpdir/tools/check-gitready.sh"
fi

cd "$tmpdir"
git init -q
git config user.email test@example.invalid
git config user.name 'gitready test'

printf 'int\tuntouched;\n' >src/untouched.c
cp src/untouched.c src/untouched.c.orig
git add src/untouched.c
git commit -q -m baseline

printf 'int changed;\n' >src/changed.c
git add src/changed.c

if ! make -s gitready >gitready-clean.log 2>&1; then
    cat gitready-clean.log
    echo 'gitready failed for a clean staged change'
    exit 1
fi

if ! cmp -s src/untouched.c src/untouched.c.orig; then
    echo 'gitready modified an unrelated tracked file'
    exit 1
fi

git reset -q -- src/changed.c
rm -f src/changed.c
printf 'int\tbad;\r\n' >src/changed.c
git add src/changed.c

if make -s gitready >gitready-bad.log 2>&1; then
    echo 'gitready accepted a staged tab/CRLF change'
    exit 1
fi

if ! cmp -s src/untouched.c src/untouched.c.orig; then
    echo 'gitready modified an unrelated tracked file while rejecting bad input'
    exit 1
fi

echo 'gitready safety test passed'
