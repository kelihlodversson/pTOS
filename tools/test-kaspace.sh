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
    ${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
        -iquote include -I"$work" -I"$arch_include" -idirafter include -iquote bdos \
        tests/kaspace_lease.c bdos/kaspace.c -Wl,--gc-sections -o "$work/lease"
    "$work/lease"
    echo "KASPACE startup leases ($preset configuration, host execution): PASS"
    # The clone transaction owns real KTHREAD/KASPACE objects. This harness
    # models PROCESS resources; native x32 tests use production KPROC instead.
    CONFIG_= python3 - "$preset" "$work/config" <<'PYCONFIG'
import kconfiglib, sys
k = kconfiglib.Kconfig(warn_to_stderr=False)
k.load_config('configs/' + sys.argv[1] + '_defconfig')
k.syms['CONF_WITH_MMAP'].set_value(0)
k.write_config(sys.argv[2])
PYCONFIG
    CONFIG_= python3 tools/genconfig.py --kconfig Kconfig --config "$work/config" \
        --header "$work/autoconf.h" --makefile "$work/auto.conf"
    ${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
        -iquote include -I"$work" -I"$arch_include" -idirafter include -iquote bdos \
        -iquote fs tests/proc_clone.c bdos/proc_clone.c bdos/thread_startup.c bdos/kthread.c bdos/kaspace.c \
        -Wl,--gc-sections -o "$work/proc-clone"
    "$work/proc-clone"
    echo "Trusted clone construction ($preset configuration, host execution): PASS"
    ${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
        -iquote include -I"$work" -I"$arch_include" -idirafter include -iquote bdos \
        -iquote fs tests/clone_observer.c -Wl,--gc-sections -o "$work/clone-observer"
    "$work/clone-observer"
    echo "Owned clone observer ($preset configuration, host execution): PASS"

done
echo 'KASPACE ownership and failed adoption: PASS'
echo 'KTHREAD_CONTEXT stack allocation and release: PASS'
# x32 backend planning and cancellation with host-owned mapping/stack models.
CONFIG_= python3 - "$work/config" <<'PY'
import kconfiglib, sys
k = kconfiglib.Kconfig(warn_to_stderr=False)
k.load_config('configs/pc-x86_64_defconfig')
k.syms['CONF_WITH_ELF_SHARED'].set_value(2)
k.syms['CONF_WITH_LIBAES'].set_value(2)
assert k.syms['CONF_WITH_NATIVE_SCHED'].str_value == 'y'
k.write_config(sys.argv[1])
PY
CONFIG_= python3 tools/genconfig.py --kconfig Kconfig --config "$work/config" \
    --header "$work/autoconf.h" --makefile "$work/auto.conf"
${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -iquote include -I"$work" -Iinclude/arch/x86_64 -idirafter include -iquote bdos \
    -iquote bdos/arch/x86_64 tests/thread_startup.c bdos/thread_startup.c bdos/kaspace.c \
    bdos/arch/x86_64/thread_startup_x32.c bdos/arch/x86_64/thread_return_x32.c bdos/arch/x86_64/kthread_context.c \
    -Wl,--gc-sections -o "$work/startup"
"$work/startup"
echo 'x32 startup CPU/FPU/frame planning and stack rollback (host model): PASS'
