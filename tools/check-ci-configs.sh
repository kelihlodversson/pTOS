#!/bin/sh
#
# check-ci-configs.sh - check that CI builds every configuration of configs/
#
# Usage: tools/check-ci-configs.sh
#
# The build workflow does not work out what to build from configs/ (that
# would take a job of its own): the configurations are listed, with the job
# (group) that builds each, in .github/ci-configs, and the groups are the
# matrices of the build and x86_64 jobs of .github/workflows/build.yml.  So
# that a new configuration is not left out without anyone noticing, this
# fails when
#   - a configuration is not listed, or listed twice, or the list has a name
#     that is not a configuration;
#   - its group does not fit its architecture, or is not in the workflow;
#   - .github/pr-skip-configs has a name that is not a configuration, or an
#     x86 one that tools/ci-x86_64.sh boots or runs tests on (that is the only
#     place those run on a pull request).

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
        want='x86-*'
    else
        want='m68k-*'
    fi
    case $group in
        $want) ;;
        *) fail "$name is in group $group in $list; its architecture wants $want" ;;
    esac
done

for name in $names; do
    [ -e "configs/${name}_defconfig" ] || fail "$list has $name, which is not a configuration"
done

for name in $(sed -e 's/#.*//' "$skip" | awk 'NF { print $1 }'); do
    [ -e "configs/${name}_defconfig" ] || fail "$skip has $name, which is not a configuration"
    case $(sed -e 's/#.*//' "$list" | awk -v n="$name" '$1 == n { print $2 }') in
        x86-*)
            case $name in
                pc-x86_64|pc-x86_64-vdi|pc-x86_64-callbacks|pc-x86_64-eager|pc-x86_64-desktop-tests*)
                    fail "$skip has $name, which tools/ci-x86_64.sh boots or tests: a pull request needs it" ;;
            esac
            ;;
    esac
done

# the groups of the list are the groups of the jobs
for group in $(sed -e 's/#.*//' "$list" | awk 'NF { print $2 }' | sort -u); do
    grep -qE "group: $group[,}]" "$workflow" ||
        fail "group $group of $list is not in the matrix of a job of $workflow"
done

# no two configurations build the same thing (#989)
python3 tools/check-duplicate-configs.py || status=1

[ $status -eq 0 ] && echo "Every configuration is built by CI."
exit $status
