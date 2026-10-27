#!/bin/sh
#
# check-ci-configs.sh - check that CI builds every configuration of configs/
#
# Usage: tools/check-ci-configs.sh
#
# The build workflow does not work out what to build from configs/ (that
# would take a job of its own): the m68k and ARM configurations are listed,
# with the job (group) that builds each, in .github/ci-configs, and the
# x86-64 ones in the matrix of the x86_64 job of .github/workflows/build.yml.
# So that a new configuration is not left out without anyone noticing, this
# fails when
#   - a configuration is not listed, or listed twice, or the list has a name
#     that is not a configuration;
#   - its group does not fit its architecture, or an x86_64 one is not in the
#     matrix of the x86_64 job;
#   - .github/pr-skip-configs has a name that is not a configuration, or an
#     x86_64 one (a pull request builds those whatever it says).

list=.github/ci-configs
skip=.github/pr-skip-configs
workflow=.github/workflows/build.yml
status=0

fail() {
    echo "error: $*" >&2
    status=1
}

names=$(sed -e 's/#.*//' "$list" | awk 'NF { print $1 }')

for config in configs/*_defconfig; do
    name=$(basename "$config" _defconfig)
    n=$(echo "$names" | grep -cx "$name")
    if [ "$n" -ne 1 ]; then
        fail "$name is listed $n times in $list (once, with its group)"
        continue
    fi
    group=$(sed -e 's/#.*//' "$list" | awk -v n="$name" '$1 == n { print $2 }')
    if grep -qE '^MACHINE_(RPI|VIRT_ARM)=y' "$config"; then
        want='arm-*'
    elif grep -qE '^MACHINE_PC_X86_64=y' "$config"; then
        want='x86_64'
    else
        want='m68k-*'
    fi
    case $group in
        $want) ;;
        *) fail "$name is in group $group in $list; its architecture wants $want" ;;
    esac
    if [ "$group" = x86_64 ] &&
       ! grep -qE "^ +- $name\$" "$workflow"; then
        fail "$name is an x86_64 configuration, but not in the matrix of the x86_64 job of $workflow"
    fi
done

for name in $names; do
    [ -e "configs/${name}_defconfig" ] || fail "$list has $name, which is not a configuration"
done

for name in $(sed -e 's/#.*//' "$skip" | awk 'NF { print $1 }'); do
    [ -e "configs/${name}_defconfig" ] || fail "$skip has $name, which is not a configuration"
    group=$(sed -e 's/#.*//' "$list" | awk -v n="$name" '$1 == n { print $2 }')
    [ "$group" != x86_64 ] || fail "$skip has $name, an x86_64 configuration, which a pull request builds anyway"
done

# the groups of the list are the groups of the build job
for group in $(sed -e 's/#.*//' "$list" | awk 'NF && $2 != "x86_64" { print $2 }' | sort -u); do
    grep -q "group: $group," "$workflow" ||
        fail "group $group of $list is not in the matrix of the build job of $workflow"
done

[ $status -eq 0 ] && echo "Every configuration is built by CI."
exit $status
