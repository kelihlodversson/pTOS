#!/bin/sh
# Build the native B2 suite with timer preemption off on a pinned Hatari lane.
set -eu
mode=${1:?expected f030a, f040a or tt040a}
case $mode in
    f030a) config=falcon-aspace; profile=refused ;;
    f040a|tt040a) config=falcon-aspace040; profile=deferred ;;
    *) echo "unsupported Hatari B2 mode: $mode" >&2; exit 2 ;;
esac
mkdir -p logs
make "${config}_defconfig"
CONFIG_= python3 - <<'PYCONFIG'
import kconfiglib
k = kconfiglib.Kconfig()
k.load_config()
for name in ('OPTIMIZE_SIZE', 'CONF_WITH_NATIVE_SCHED', 'CONF_WITH_PCLONE',
             'CONF_WITH_PTHJOIN', 'CONF_WITH_PTERMGRP', 'CONF_WITH_EXIT_HANDOFF_TEST',
             'CONF_WITH_PRIVATE_CLONE', 'CONF_WITH_PRIVATE_CLONE_TEST',
             'CONF_WITH_IRQ_RESTART_TEST'):
    k.syms[name].set_value(2)
    assert k.syms[name].str_value == 'y', name
for name in ('CONF_WITH_MFASTCALL', 'CONF_WITH_NATIVE_PREEMPT', 'CONF_WITH_PFORK',
             'CONF_WITH_FORK_RETURN_TEST'):
    k.syms[name].set_value(0)
    assert k.syms[name].str_value == 'n', name
assert k.syms['CONF_WITH_SAVED_RETURN'].str_value == 'y'
k.write_config()
PYCONFIG
make -j"$(getconf _NPROCESSORS_ONLN)" >logs/hatari-b2.build 2>&1 || {
    tail -35 logs/hatari-b2.build
    exit 1
}
tools/check-build-warnings.sh logs/hatari-b2.build
make test-hd-shell >logs/hatari-b2.testdisk 2>&1
halt=$(awk '/^ +0x[0-9a-f]+ +halt$/ { print $1; exit }' emutos.map)
panic=$(awk '/^ +0x[0-9a-f]+ +dopanic$/ { print $1; exit }' emutos.map)
test -n "$halt"
test -n "$panic"
python3 tools/run-hatari-tests.py --tos ptos512k.img --disk test-hd-shell.img \
    --mode "$mode" --timeout 1200 --require-irq-restart "$profile" \
    --halt "$halt" --panic "$panic" >"logs/hatari-b2-$mode.log" 2>&1 || {
    tail -45 "logs/hatari-b2-$mode.log"
    exit 1
}
grep -E 'native (interrupted USER|retained IRQ|m68k nonzero FPIAR)|Summary' "logs/hatari-b2-$mode.log"
