#!/usr/bin/env python3
"""Production replacement admission; CPU/scheduler/THREAD execution is modeled."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import kconfiglib

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
os.environ['CONFIG_'] = ''
with tempfile.TemporaryDirectory(prefix='ptos-exec-replacement-') as directory:
    work = Path(directory)
    config = kconfiglib.Kconfig(warn_to_stderr=False)
    config.load_config('configs/pc-x86_64_defconfig')
    assert config.syms['CONF_WITH_EXEC_REPLACEMENT_TEST'].str_value == 'n'
    for name in ('CONF_WITH_ELF_SHARED', 'CONF_WITH_CUIS', 'CONF_WITH_X32_VDI',
                 'CONF_WITH_USER_CALLBACKS', 'CONF_WITH_PTHJOIN', 'CONF_WITH_PTERMGRP'):
        config.syms[name].set_value(0)
    for name in ('CONF_WITH_MMAP', 'CONF_WITH_X86_64_MEMTEST', 'CONF_WITH_NATIVE_SCHED',
                 'CONF_WITH_EXEC_REPLACEMENT_TEST'):
        config.syms[name].set_value(2)
        assert config.syms[name].str_value == 'y', name
    # Execute the production runner's guard with generated headers, including
    # the legal test=y/Pclone=n configuration. Only the Pclone fixture can
    # produce this marker; enabled transport must still reject missing proof.
    script = (ROOT/'tools/run-regression-tests.sh').read_text()
    start = script.index("if grep -q '^#define CONF_WITH_EXEC_REPLACEMENT_TEST 1'")
    end = script.index('\nfi', start) + 3
    block = script[start:end]
    (work/'obj').mkdir()
    log = work/'suite.log'
    for staging in (0, 2):
        for clone in (0, 2):
            config.syms['CONF_WITH_EXEC_REPLACEMENT_TEST'].set_value(staging)
            config.syms['CONF_WITH_PCLONE'].set_value(clone)
            assert config.syms['CONF_WITH_EXEC_REPLACEMENT_TEST'].str_value == ('y' if staging else 'n')
            assert config.syms['CONF_WITH_PCLONE'].str_value == ('y' if clone else 'n')
            config.write_config(str(work/'marker.config'))
            subprocess.run([sys.executable, 'tools/genconfig.py', '--kconfig', 'Kconfig',
                            '--config', str(work/'marker.config'),
                            '--header', str(work/'obj/autoconf.h'),
                            '--makefile', str(work/'marker.conf')], check=True)
            for present in (False, True):
                log.write_text('native reversible executable staging ownership (not Pexec200): PASS\n'
                               if present else 'unrelated fixture: PASS\n')
                shell = 'top=$1; suite_log=$2\nrecord() { printf "%s|%s" "$1" "$2"; }\n' + block
                result = subprocess.run(['sh', '-c', shell, 'probe', str(work), str(log)],
                                        check=True, text=True, capture_output=True)
                required = bool(staging and clone and not present)
                expected = 'native executable staging ownership|FAIL' if required else ''
                assert result.stdout == expected, (staging, clone, present, result.stdout)
    print('Native staging marker: 8 actual runner/Kconfig cases, missing enabled evidence rejected: PASS')
    start = script.index("if grep -q '^#define CONF_WITH_EXEC_HANDOFF_TEST 1'")
    block = script[start:script.index('\nfi', start) + 3]
    config.syms['CONF_WITH_PCLONE'].set_value(2)
    for enabled in (0, 2):
        config.syms['CONF_WITH_EXEC_REPLACEMENT_TEST'].set_value(2)
        config.syms['CONF_WITH_EXEC_HANDOFF_TEST'].set_value(enabled)
        assert config.syms['CONF_WITH_EXEC_HANDOFF_TEST'].str_value == ('y' if enabled else 'n')
        config.write_config(str(work/'handoff.config'))
        subprocess.run([sys.executable, 'tools/genconfig.py', '--kconfig', 'Kconfig',
                        '--config', str(work/'handoff.config'),
                        '--header', str(work/'obj/autoconf.h'),
                        '--makefile', str(work/'marker.conf')], check=True)
        for present in (False, True):
            log.write_text('native same-PROCESS/TID NORETURN fresh-stack transport (not Pexec200): PASS\n'
                           if present else 'unrelated fixture: PASS\n')
            shell = 'top=$1; suite_log=$2\nrecord() { printf "%s|%s" "$1" "$2"; }\n' + block
            result = subprocess.run(['sh', '-c', shell, 'probe', str(work), str(log)],
                                    check=True, text=True, capture_output=True)
            assert result.stdout == ('native executable handoff transport|FAIL'
                                     if enabled and not present else '')
    print('Native handoff marker: 4 actual runner/Kconfig cases; no modeled exec event: PASS')
    config.syms['CONF_WITH_EXEC_HANDOFF_TEST'].set_value(0)
    config.syms['CONF_WITH_EXEC_REPLACEMENT_TEST'].set_value(2)
    config.syms['CONF_WITH_PCLONE'].set_value(0)
    config.write_config(str(work/'config'))
    subprocess.run([sys.executable, 'tools/genconfig.py', '--kconfig', 'Kconfig',
                    '--config', str(work/'config'), '--header', str(work/'autoconf.h'),
                    '--makefile', str(work/'auto.conf')], check=True)
    cc = shlex.split(os.environ.get('HOSTCC', 'cc'))
    subprocess.run(cc + ['-fno-builtin', '-std=gnu90', '-O2', '-Wall', '-Wextra', '-Werror',
        '-fno-pie', '-no-pie', '-ffunction-sections', '-fdata-sections',
        '-iquote', 'include', '-I'+str(work), '-Iinclude/arch/x86_64',
        '-idirafter', 'include', '-iquote', 'bdos', '-iquote', 'fs',
        'tests/exec_replacement.c', 'bdos/exec_replace.c', 'bdos/kowner.c', 'tests/kowner_model.c', '-Wl,--gc-sections',
        '-o', str(work/'replacement')], check=True)
    subprocess.run([str(work/'replacement')], check=True)
