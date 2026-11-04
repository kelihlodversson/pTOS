#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d "${TMPDIR:-/tmp}/ptos-kaspace.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
CONFIG_= python3 tools/genconfig.py --kconfig Kconfig --config configs/pc-x86_64_defconfig \
    --header "$work/autoconf.h" --makefile "$work/auto.conf"
${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -iquote include -I"$work" \
    -Iinclude/arch/x86_64 -Iinclude/machine/pc-x86_64 -iquote bdos \
    tests/kaspace_lifetime.c bdos/kaspace.c -o "$work/test"
"$work/test"
${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -iquote include -I"$work" \
    -Iinclude/arch/x86_64 -Iinclude/machine/pc-x86_64 -iquote bdos \
    -iquote bdos/arch/x86_64 tests/kthread_stack.c \
    bdos/arch/x86_64/kthread_context.c -o "$work/thread-test"
"$work/thread-test"
for preset in pc-x86_64 virt-arm-aspace virt-m68k-aspace; do
    CONFIG_= python3 - "$preset" "$work/config" <<'PY'
import kconfiglib, sys
k = kconfiglib.Kconfig(warn_to_stderr=False)
k.load_config('configs/' + sys.argv[1] + '_defconfig')
k.syms['CONF_WITH_MMAP'].set_value(2)
assert k.syms['CONF_WITH_MMAP'].str_value == 'y'
k.write_config(sys.argv[2])
PY
    CONFIG_= python3 tools/genconfig.py --kconfig Kconfig --config "$work/config" \
        --header "$work/autoconf.h" --makefile "$work/auto.conf"
    arch_include=include/arch/x86_64
    [ "$preset" != virt-arm-aspace ] || arch_include=include/arch/arm
    [ "$preset" != virt-m68k-aspace ] || arch_include=include/arch/m68k
    ${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -iquote include -I"$work" \
        -I"$arch_include" -idirafter include -iquote bdos \
        tests/kaspace_mmap_policy.c bdos/kaspace.c -o "$work/mmap-policy"
    "$work/mmap-policy"
    echo "KASPACE mmap policy ($preset configuration, host execution): PASS"
done
echo 'KASPACE ownership and failed adoption: PASS'
echo 'KTHREAD_CONTEXT stack allocation and release: PASS'
