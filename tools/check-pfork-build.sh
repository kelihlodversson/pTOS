#!/bin/sh
#
# check-pfork-build.sh - build public Pfork/Pvfork (CONF_WITH_PFORK) without
# any of the test transports, on each architecture whose return image is
# admitted: the production options must not depend on test-only code.
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.

set -eu
for config in ${*:-pc-x86_64 virt-arm-aspace virt-m68k-aspace}; do
    echo "::group::$config with CONF_WITH_PFORK and no test transports"
    make -s distclean >/dev/null 2>&1 || true
    make -s "${config}_defconfig" >/dev/null
    CONFIG_= python3 - <<'PY'
import kconfiglib
config = kconfiglib.Kconfig()
config.load_config()
for name in ('CONF_WITH_PCLONE', 'CONF_WITH_NATIVE_SCHED', 'CONF_WITH_PRIVATE_CLONE',
             'CONF_WITH_PTHJOIN', 'CONF_WITH_PROCESS_FILES', 'CONF_WITH_PFORK'):
    config.syms[name].set_value(2)
for name in ('CONF_WITH_PFORK', 'CONF_WITH_SAVED_RETURN', 'CONF_WITH_FORK_CORE',
             'CONF_WITH_PROCESS_FILES', 'CONF_WITH_PEXEC200'):
    assert config.syms[name].str_value == 'y', name
for name in ('CONF_WITH_FORK_RETURN_TEST', 'CONF_WITH_PRIVATE_CLONE_TEST',
             'CONF_WITH_PROCESS_FILES_TEST', 'CONF_WITH_EXEC_REPLACEMENT_TEST'):
    assert config.syms[name].str_value != 'y', name
config.write_config()
PY
    make >"pfork-$config.log" 2>&1 || { tail -20 "pfork-$config.log"; exit 1; }
    tools/check-build-warnings.sh "pfork-$config.log"
    echo "::endgroup::"
done
make -s distclean >/dev/null 2>&1 || true
echo "public Pfork/Pvfork builds without test transports: PASS"
