#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
python3 tools/test-scheduler-probes.py
python3 tools/test-timer-fixtures.py
python3 tools/test-hatari-evidence.py
python3 tools/test-amiberry-config.py
python3 tools/test-interrupt-restart.py
python3 tools/test-interrupt-restart.py --preempt
python3 tools/test-procmem-borrow.py --irq
python3 tools/test-shared-root-mapping.py
work=$(mktemp -d "${TMPDIR:-/tmp}/ptos-kaspace.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat configs/pc-x86_64_defconfig tools/hosted-legacy-profile.config > "$work/legacy.config"
CONFIG_= python3 tools/genconfig.py --kconfig Kconfig --config "$work/legacy.config" \
    --header "$work/autoconf.h" --makefile "$work/auto.conf"
${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -iquote include -I"$work" \
    -Iinclude/arch/x86_64 -Iinclude/machine/pc-x86_64 -iquote bdos \
    tests/kaspace_lifetime.c bdos/kaspace.c bdos/kowner.c tests/kowner_model.c -o "$work/test"
"$work/test"
${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -iquote include -I"$work" \
    -Iinclude/arch/x86_64 -Iinclude/machine/pc-x86_64 -iquote bdos \
    -iquote bdos/arch/x86_64 tests/kthread_stack.c \
    bdos/kthread_context.c bdos/kowner.c tests/kowner_model.c -o "$work/thread-test"
"$work/thread-test"
${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -iquote include -I"$work" \
    -Iinclude/arch/x86_64 tests/x32_irq_frame.c bdos/kowner.c tests/kowner_model.c -o "$work/irq-frame"
"$work/irq-frame"
${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -fno-pie -no-pie -iquote include -I"$work" \
    -Iinclude/arch/x86_64 tests/arm_irq_frame.c bdos/kowner.c tests/kowner_model.c -o "$work/arm-irq-frame"
"$work/arm-irq-frame"
${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -DTEST_ARM_D16 -iquote include -I"$work" \
    -Iinclude/arch/x86_64 tests/arm_irq_frame.c bdos/kowner.c tests/kowner_model.c -o "$work/arm-irq-d16"
"$work/arm-irq-d16"
${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -iquote include -I"$work" \
    -Iinclude/arch/x86_64 tests/m68k_irq_frame.c bdos/kowner.c tests/kowner_model.c -o "$work/m68k-irq-frame"
"$work/m68k-irq-frame"
${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -iquote include -I"$work" \
    -Iinclude/arch/x86_64 tests/wake_idle.c bdos/kowner.c tests/kowner_model.c -o "$work/wake-idle"
"$work/wake-idle"
for preset in pc-x86_64 virt-arm-aspace virt-m68k-aspace; do
    CONFIG_= python3 - "$preset" "$work/config" <<'PY'
import kconfiglib, sys
k = kconfiglib.Kconfig(warn_to_stderr=False)
k.load_config('configs/' + sys.argv[1] + '_defconfig')
k.load_config('tools/hosted-legacy-profile.config', replace=False)
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
        -c tests/sched_sequence_header.c -o "$work/sequence-header.o"
    ${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -iquote include -I"$work" \
        -I"$arch_include" -idirafter include -iquote bdos \
        tests/kaspace_mmap_policy.c bdos/kaspace.c bdos/kowner.c tests/kowner_model.c -o "$work/mmap-policy"
    "$work/mmap-policy"
    echo "KASPACE mmap policy ($preset configuration, host execution): PASS"
    ${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
        -iquote include -I"$work" -I"$arch_include" -idirafter include -iquote bdos \
        tests/kaspace_lease.c bdos/kaspace.c -Wl,--gc-sections bdos/kowner.c tests/kowner_model.c -o "$work/lease"
    "$work/lease"
    echo "KASPACE startup leases ($preset configuration, host execution): PASS"
    ${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
        -iquote include -I"$work" -I"$arch_include" -idirafter include -iquote bdos \
        tests/kaspace_reservation.c -Wl,--gc-sections bdos/kowner.c tests/kowner_model.c -o "$work/reservation"
    "$work/reservation"
    ${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
        -iquote include -I"$work" -I"$arch_include" -idirafter include -iquote bdos \
        tests/kaspace_retired_backing.c -Wl,--gc-sections bdos/kowner.c tests/kowner_model.c -o "$work/retired-backing"
    "$work/retired-backing"
    # The clone transaction owns real KTHREAD/KASPACE objects. This harness
    # models PROCESS resources; native x32 tests use production KPROC instead.
    CONFIG_= python3 - "$preset" "$work/config" <<'PYCONFIG'
import kconfiglib, sys
k = kconfiglib.Kconfig(warn_to_stderr=False)
k.load_config('configs/' + sys.argv[1] + '_defconfig')
k.load_config('tools/hosted-legacy-profile.config', replace=False)
k.syms['CONF_WITH_MMAP'].set_value(0)
if sys.argv[1] == 'pc-x86_64':
    k.syms['CONF_WITH_X86_64_MEMTEST'].set_value(2)
    assert k.syms['CONF_WITH_X86_64_MEMTEST'].str_value == 'y'
k.write_config(sys.argv[2])
PYCONFIG
    CONFIG_= python3 tools/genconfig.py --kconfig Kconfig --config "$work/config" \
        --header "$work/autoconf.h" --makefile "$work/auto.conf"
    ${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
        -iquote include -I"$work" -I"$arch_include" -idirafter include -iquote bdos \
        -iquote fs tests/proc_clone.c bdos/proc_clone.c bdos/process_observer.c bdos/thread_startup.c bdos/kthread.c bdos/kaspace.c \
        -Wl,--gc-sections bdos/kowner.c tests/kowner_model.c -o "$work/proc-clone"
    "$work/proc-clone"
    echo "Trusted clone construction ($preset configuration, host execution): PASS"
    ${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
        -iquote include -I"$work" -I"$arch_include" -idirafter include -iquote bdos \
        -iquote fs tests/clone_observer.c bdos/process_observer.c -Wl,--gc-sections bdos/kowner.c tests/kowner_model.c -o "$work/clone-observer"
    "$work/clone-observer"
    echo "Owned clone observer ($preset configuration, host execution): PASS"
    ${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -fno-pie -no-pie \
        -iquote include -I"$work" -I"$arch_include" -idirafter include -iquote bdos \
        tests/sched_lifetime.c bdos/kowner.c tests/kowner_model.c -o "$work/sched-lifetime"
    "$work/sched-lifetime"
    echo "Scheduler lifetime ($preset configuration, host execution): PASS"

done
echo 'KASPACE ownership and failed adoption: PASS'
echo 'KTHREAD_CONTEXT stack allocation and release: PASS'
# x32 backend planning and cancellation with host-owned mapping/stack models.
CONFIG_= python3 - "$work/config" <<'PY'
import kconfiglib, sys
k = kconfiglib.Kconfig(warn_to_stderr=False)
k.load_config('configs/pc-x86_64_defconfig')
k.load_config('tools/hosted-legacy-profile.config', replace=False)
k.syms['CONF_WITH_ELF_SHARED'].set_value(2)
k.syms['CONF_WITH_LIBAES'].set_value(2)
for name in ('CONF_WITH_PCLONE', 'CONF_WITH_PTHJOIN', 'CONF_WITH_FORK_RETURN_TEST', 'CONF_WITH_PRIVATE_CLONE'):
    k.syms[name].set_value(2)
assert k.syms['CONF_WITH_FORK_RETURN_TEST'].str_value == 'y'
assert k.syms['CONF_WITH_NATIVE_SCHED'].str_value == 'y'
k.write_config(sys.argv[1])
PY
CONFIG_= python3 tools/genconfig.py --kconfig Kconfig --config "$work/config" \
    --header "$work/autoconf.h" --makefile "$work/auto.conf"
${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -iquote include -I"$work" -Iinclude/arch/x86_64 -iquote bdos \
    -Ibios/machine/pc-x86_64 -Ibios/arch/x86_64 -Ibios -Iutil \
    tests/x32_irq_capture.c -Wl,--gc-sections bdos/kowner.c tests/kowner_model.c -o "$work/irq-capture"
${HOSTCC:-cc} -std=gnu90 -O2 -Wall -Wextra -Werror \
    -iquote include -I"$work" -Iinclude/arch/x86_64 \
    tests/kowner.c bdos/kowner.c -o "$work/owners"
"$work/owners"
"$work/irq-capture"
# Exercise the actual CPU storage declaration from two translation units.
# Partitioned LTO must resolve the run alias after LTRANS; normal/ASan builds
# must keep their single complete CPU global without a pointer-sized alias.
for alias_mode in ordinary lto; do
    alias_flags=
    if [ "$alias_mode" = lto ]; then
        alias_flags='-flto=4 -flto-partition=max -DPTOS_LTO_RUN_ALIAS -Wl,--defsym,run=boot_cpu'
    fi
    ${HOSTCC:-cc} -std=gnu90 -O2 -Wall -Wextra -Werror -Wno-unused-function \
        -ffunction-sections -fdata-sections $alias_flags \
        -iquote include -I"$work" -Iinclude/arch/x86_64 -iquote bdos -iquote fs \
        tests/sched_cpu_alias.c tests/sched_run_abi.c -Wl,--gc-sections bdos/kowner.c tests/kowner_model.c -o "$work/cpu-alias"
    "$work/cpu-alias"
done
${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -iquote include -I"$work" -Iinclude/arch/x86_64 -idirafter include -iquote bdos \
    -iquote bdos/arch/x86_64 tests/thread_startup.c bdos/thread_startup.c bdos/kaspace.c \
    bdos/arch/x86_64/thread_startup_x32.c bdos/thread_return.c bdos/kthread_context.c \
    -Wl,--gc-sections bdos/kowner.c tests/kowner_model.c -o "$work/startup"
"$work/startup"
echo 'x32 startup CPU/FPU/frame planning and stack rollback (host model): PASS'
# Completion registry tests use real KTHREAD code, with counted ownership models.
${HOSTCC:-cc} -std=gnu90 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -iquote include -I"$work" -Iinclude/arch/x86_64 -idirafter include -iquote bdos \
    -iquote fs tests/kthread_join.c -Wl,--gc-sections bdos/kowner.c tests/kowner_model.c -o "$work/thread-join"
"$work/thread-join"
echo 'KTHREAD TID completion: late join, one reaper, claim cancellation, cycles, retirement and rollback: PASS'

${HOSTCC:-cc} -std=gnu90 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -iquote include -I"$work" -Iinclude/arch/x86_64 -idirafter include -iquote bdos \
    -iquote bdos/arch/x86_64 -iquote fs tests/wait_cancel.c -Wl,--gc-sections bdos/kowner.c tests/kowner_model.c -o "$work/wait-cancel"
"$work/wait-cancel"
echo 'Scheduler cancellation admission, nested identity and reentrant termination hook: PASS (host model)'

${HOSTCC:-cc} -std=gnu90 -O2 -Wall -Wextra -Werror -Wno-unused-function -fno-pie -no-pie \
    -ffunction-sections -fdata-sections -iquote include -I"$work" \
    -Iinclude/arch/x86_64 -idirafter include -iquote bdos -iquote fs \
    tests/sched_result_wait.c bdos/sched_lifetime.c -Wl,--gc-sections bdos/kowner.c tests/kowner_model.c -o "$work/result-wait"
"$work/result-wait"
# Force quoted kernel headers to precede system headers: the fixture must
# explicitly retain the hosted jump ABI under this ordering as well.
${HOSTCC:-cc} -std=gnu90 -O2 -Wall -Wextra -Werror -Wno-unused-function -fno-pie -no-pie \
    -ffunction-sections -fdata-sections -iquote include -I"$work" \
    -Iinclude/arch/x86_64 -iquote bdos -iquote fs \
    -c tests/sched_result_wait.c -o "$work/result-wait-quoted.o"

${HOSTCC:-cc} -std=gnu90 -O2 -Wall -Wextra -Werror -Wno-unused-function -fno-pie -no-pie \
    -ffunction-sections -fdata-sections -iquote include -I"$work" \
    -Iinclude/arch/x86_64 -idirafter include -iquote bdos -iquote fs \
    tests/sched_process_claim.c bdos/sched_lifetime.c -Wl,--gc-sections bdos/kowner.c tests/kowner_model.c -o "$work/process-claim"
"$work/process-claim"
