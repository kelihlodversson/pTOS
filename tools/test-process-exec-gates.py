#!/usr/bin/env python3
"""Validate actual hidden-gate selection, object inclusion and discovery cases.

The temporary Kconfig selector exists only in this fixture. No production
preset or source gate is enabled by these prospective-configuration tests.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import kconfiglib

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
os.environ['CONFIG_'] = ''
with tempfile.TemporaryDirectory(prefix='ptos-process-gates-') as directory:
    work = Path(directory)
    root = work / 'Kconfig'
    root.write_text('source "Kconfig"\n'
                    'config PROCESS_EXEC_VALIDATION_SELECT\n'
                    '    bool "Fixture-only production selector"\n'
                    '    depends on CONF_WITH_USER_ASPACE && CONF_WITH_NATIVE_SCHED && CONF_WITH_SSYSTEM\n'
                    '    select CONF_WITH_PROCESS_EXEC\n')
    cases = [('ordinary', False, False), ('test', False, True),
             ('production', True, False), ('both', True, True)]
    for name, production, test in cases:
        config = kconfiglib.Kconfig(str(root), warn_to_stderr=False)
        config.load_config('configs/pc-x86_64_defconfig')
        config.syms['CONF_WITH_ELF_SHARED'].set_value(0)
        config.syms['CONF_WITH_NATIVE_SCHED'].set_value(2)
        # Assigning a hidden symbol is not activation. A real selector is
        # required in the future reviewed activation increment.
        config.syms['CONF_WITH_PROCESS_EXEC'].set_value(2)
        assert config.syms['CONF_WITH_PROCESS_EXEC'].str_value == 'n'
        config.syms['PROCESS_EXEC_VALIDATION_SELECT'].set_value(2 if production else 0)
        config.syms['CONF_WITH_PROCESS_EXEC_TEST'].set_value(2 if test else 0)
        expected = ('y' if production else 'n', 'y' if production or test else 'n', 'y' if test else 'n')
        actual = tuple(config.syms[x].str_value for x in (
            'CONF_WITH_PROCESS_EXEC', 'CONF_WITH_PROCESS_EXEC_DISPATCH', 'CONF_WITH_PROCESS_EXEC_TEST'))
        assert actual == expected, (name, actual, expected)
        config.write_config(str(work / 'config'))
        subprocess.run([sys.executable, 'tools/genconfig.py', '--kconfig', str(root),
                        '--config', str(work / 'config'), '--header', str(work / 'autoconf.h'),
                        '--makefile', str(work / 'auto.conf')], check=True, stdout=subprocess.DEVNULL)
        # Consume the real build.mk: both gates must not duplicate an object.
        objects = subprocess.check_output(['gmake' if sys.platform == 'darwin' else 'make', '-s', '-f', '-', 'all'],
            input=('include '+str(work / 'auto.conf')+'\ninclude bdos/build.mk\n'
                   'all:\n\t@echo $(obj-y)\n').encode(), cwd=ROOT).decode().split()
        assert objects.count('process_exec_dispatch.o') == int(production or test), (name, objects)
        # Read the real top-level native fixture exports and payload decision.
        # Config paths stay temporary; this never rewrites the source .config.
        native = subprocess.check_output([
            'gmake' if sys.platform == 'darwin' else 'make', '-s', '--no-print-directory',
            '-f', '-', 'process-gate-fixture', 'KCONFIG_TOP='+str(root),
            'KCONFIG_CONFIG='+str(work / 'config'), 'AUTOCONF_MK='+str(work / 'auto.conf'),
            'AUTOCONF_H='+str(work / 'autoconf.h')], input=(
                'include Makefile\n.PHONY: process-gate-fixture\nprocess-gate-fixture:\n'
                '\t@echo FLAGS $(filter -DTEST_WITH_PROCESS_EXEC%,$(TEST_CFLAGS))\n'
                '\t@echo PAYLOAD $(TEST_PROCESS_EXEC_FILES)\n').encode(), cwd=ROOT).decode()
        gate_flags = next(line.split()[1:] for line in native.splitlines() if line.startswith('FLAGS'))
        expected_flags = set()
        if production: expected_flags.add('-DTEST_WITH_PROCESS_EXEC')
        if test: expected_flags.add('-DTEST_WITH_PROCESS_EXEC_TEST')
        if production or test: expected_flags.add('-DTEST_WITH_PROCESS_EXEC_DISPATCH')
        assert set(gate_flags) == expected_flags, (name, 'native gate exports', gate_flags, expected_flags)
        payload = next(line.split()[1:] for line in native.splitlines() if line.startswith('PAYLOAD'))
        assert payload == (['PROCXIT.TOS'] if production or test else []), (name, payload)
        # Preprocess the actual user fixture's branches. Empty trap headers are
        # sufficient here: no native binding/CPU execution is modeled by CPP.
        headers = work / 'fixture-headers' / 'mint'
        headers.mkdir(parents=True, exist_ok=True)
        for header in ('osbind.h', 'mintbind.h'): (headers / header).write_text('/* CPP only */\n')
        user = subprocess.check_output(shlex.split(os.environ.get('HOSTCC', 'cc')) + [
            '-E', '-P', '-Itests', '-I'+str(headers.parent)] + gate_flags +
            ['tests/process_transport/process_transport.c'], cwd=ROOT).decode()
        assert ('production PROCESS transport must be advertised' in user) == production, name
        assert ('unpublished PROCESS transport must not be advertised' in user) == (not production), name
        assert ('normal/unsupported kernel exposed test-only PROCESS dispatch' in user) == (not (production or test)), name
        assert ('prepare ordinary PROCESS without GUI registration' in user) == (production or test), name
        # Inspect the actual preprocessed Ssystem functions, not a parallel
        # table of capabilities. Host preprocessing does not execute a trap.
        source = subprocess.check_output(shlex.split(os.environ.get('HOSTCC', 'cc')) + [
            '-E', '-P', '-iquote', 'include', '-I'+str(work), '-Iinclude/arch/x86_64',
            '-iquote', 'bdos', '-iquote', 'fs', '-idirafter', 'include', 'bdos/ssystem.c'],
            cwd=ROOT).decode()
        inquiry = source.split('static BOOL ssystem_has_mode(', 1)[1].split('LONG xssystem(', 1)[0]
        assert bool(re.search(r'case\s+\(\(WORD\)0xfff7\)', inquiry)) == production, name
        dispatch = source.split('LONG xssystem(', 1)[1]
        assert ('return proc_exec_dispatch(arg1,arg2);' in dispatch) == (production or test), name
        if production:
            # Link and execute the actual adapter/decoder for future production
            # and both-gate validation configurations, still only on the host.
            subprocess.run(shlex.split(os.environ.get('HOSTCC', 'cc')) + [
                '-fno-builtin', '-std=gnu90', '-O2', '-Wall', '-Wextra', '-Werror',
                '-fno-pie', '-no-pie', '-ffunction-sections', '-fdata-sections',
                '-iquote', 'include', '-I'+str(work), '-Iinclude/arch/x86_64',
                '-idirafter', 'include', '-iquote', 'bdos', '-iquote', 'fs',
                'tests/process_exec_dispatch.c', '-Wl,--gc-sections', '-o', str(work / name)],
                check=True)
            subprocess.run([str(work / name)], check=True)
        print('PROCESS gate '+name+': selection/build/discovery PASS')
    for preset in ('atari256', 'pc-x86_64-eager'):
        config = kconfiglib.Kconfig(str(root), warn_to_stderr=False)
        config.load_config('configs/'+preset+'_defconfig')
        config.syms['PROCESS_EXEC_VALIDATION_SELECT'].set_value(2)
        config.syms['CONF_WITH_PROCESS_EXEC_TEST'].set_value(2)
        assert all(config.syms[x].str_value == 'n' for x in (
            'CONF_WITH_PROCESS_EXEC', 'CONF_WITH_PROCESS_EXEC_DISPATCH', 'CONF_WITH_PROCESS_EXEC_TEST'))
        print('PROCESS gate unsupported '+preset+': no exposure PASS')
