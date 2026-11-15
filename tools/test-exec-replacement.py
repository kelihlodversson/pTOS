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
    config.write_config(str(work/'config'))
    subprocess.run([sys.executable, 'tools/genconfig.py', '--kconfig', 'Kconfig',
                    '--config', str(work/'config'), '--header', str(work/'autoconf.h'),
                    '--makefile', str(work/'auto.conf')], check=True)
    cc = shlex.split(os.environ.get('HOSTCC', 'cc'))
    subprocess.run(cc + ['-fno-builtin', '-std=gnu90', '-O2', '-Wall', '-Wextra', '-Werror',
        '-fno-pie', '-no-pie', '-ffunction-sections', '-fdata-sections',
        '-iquote', 'include', '-I'+str(work), '-Iinclude/arch/x86_64',
        '-idirafter', 'include', '-iquote', 'bdos', '-iquote', 'fs',
        'tests/exec_replacement.c', 'bdos/exec_replace.c', '-Wl,--gc-sections',
        '-o', str(work/'replacement')], check=True)
    subprocess.run([str(work/'replacement')], check=True)
