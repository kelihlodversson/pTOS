#!/bin/sh
# Qualified 040 Phase C lane. Keep the ordinary ROM/030/fastcall matrix.
set -eu
mkdir -p logs
make falcon-aspace040_defconfig
CONFIG_= python3 - <<'PYCONFIG'
import kconfiglib
k = kconfiglib.Kconfig()
k.load_config()
# The complete native test kernel exceeds the 512-KB image at -O2. Use the
# existing size profile; retain stack ABI, MMU/FPU and every required probe.
k.syms['OPTIMIZE_SIZE'].set_value(2)
assert k.syms['OPTIMIZE_SIZE'].str_value == 'y'
for name in ('CONF_WITH_MULTITHREADING', 'CONF_WITH_EXIT_HANDOFF_TEST',
             'CONF_WITH_PRIVATE_CLONE_TEST', 'CONF_WITH_PROCESS_FILES_TEST'):
    k.syms[name].set_value(2)
    assert k.syms[name].str_value == 'y', name
assert k.syms['CONF_WITH_PREEMPTION'].str_value == 'y'
assert k.syms['CONF_WITH_FORK'].str_value == 'n'
k.syms['CONF_WITH_FORK_RETURN_TEST'].set_value(0)
assert k.syms['CONF_WITH_SAVED_RETURN'].str_value == 'y'
assert k.syms['CONF_WITH_MFASTCALL'].str_value == 'n'
assert k.syms['CONF_WITH_IRQ_RESTART_TEST'].str_value == 'n'
k.write_config()
PYCONFIG
make -j"$(getconf _NPROCESSORS_ONLN)" >logs/hatari-preemption.build 2>&1 || {
    tail -35 logs/hatari-preemption.build
    exit 1
}
make test-hd-shell >logs/hatari-preemption.testdisk 2>&1
halt=$(awk '/^ +0x[0-9a-f]+ +halt$/ { print $1; exit }' emutos.map)
panic=$(awk '/^ +0x[0-9a-f]+ +dopanic$/ { print $1; exit }' emutos.map)
test -n "$halt"
test -n "$panic"
for mode in f040a tt040a; do
    halt_args=
    [ "$mode" != tt040a ] || halt_args="--halt $halt --panic $panic"
    python3 tools/run-hatari-tests.py --tos ptos512k.img --disk test-hd-shell.img \
        --mode "$mode" --timeout 1200 --require-native-preemption $halt_args \
        >"logs/hatari-preemption-$mode.log" 2>&1 || {
        tail -45 "logs/hatari-preemption-$mode.log"
        exit 1
    }
    grep -E 'native timer|Summary' "logs/hatari-preemption-$mode.log"
done
