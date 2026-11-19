#!/bin/sh
# Separate opt-in 040 Phase C lane. Keep the ordinary ROM/030/fastcall matrix.
set -eu
mkdir -p logs
make falcon-aspace040_defconfig
CONFIG_= python3 - <<'PYCONFIG'
import kconfiglib
k = kconfiglib.Kconfig()
k.load_config()
for name in ('CONF_WITH_NATIVE_SCHED', 'CONF_WITH_PCLONE', 'CONF_WITH_PTHJOIN',
             'CONF_WITH_PTERMGRP', 'CONF_WITH_EXIT_HANDOFF_TEST',
             'CONF_WITH_FORK_RETURN_TEST', 'CONF_WITH_PRIVATE_CLONE',
             'CONF_WITH_PRIVATE_CLONE_TEST', 'CONF_WITH_PROCESS_FILES_TEST',
             'CONF_WITH_PFORK', 'CONF_WITH_NATIVE_PREEMPT'):
    k.syms[name].set_value(2)
    assert k.syms[name].str_value == 'y', name
assert k.syms['CONF_WITH_MFASTCALL'].str_value == 'n'
assert k.syms['CONF_WITH_IRQ_RESTART_TEST'].str_value == 'n'
k.write_config()
PYCONFIG
make -j"$(getconf _NPROCESSORS_ONLN)" >logs/hatari-preemption.build 2>&1
make test-hd-shell >logs/hatari-preemption.testdisk 2>&1
for mode in f040a tt040a; do
    python3 tools/run-hatari-tests.py --tos ptos512k.img --disk test-hd-shell.img \
        --mode "$mode" --timeout 1200 --require-native-preemption \
        >"logs/hatari-preemption-$mode.log" 2>&1 || {
        tail -45 "logs/hatari-preemption-$mode.log"
        exit 1
    }
    grep -E 'native timer|Summary' "logs/hatari-preemption-$mode.log"
done
