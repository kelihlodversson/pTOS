#!/usr/bin/env python3
"""Run production PROCESS/mapping lifetime policy with a counted host backend."""
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
with tempfile.TemporaryDirectory(prefix='ptos-shared-root-') as directory:
    work = Path(directory)
    config = kconfiglib.Kconfig(warn_to_stderr=False)
    config.load_config('configs/pc-x86_64_defconfig')
    for name in ('CONF_WITH_NATIVE_SCHED', 'CONF_WITH_ELF_SHARED',
                 'CONF_WITH_CUIS', 'CONF_WITH_X32_VDI', 'CONF_WITH_USER_CALLBACKS'):
        config.syms[name].set_value(0)
    config.syms['CONF_WITH_MMAP'].set_value(2)
    config.syms['CONF_WITH_X86_64_MEMTEST'].set_value(2)
    assert config.syms['CONF_WITH_MMAP'].str_value == 'y'
    assert config.syms['CONF_WITH_X86_64_MEMTEST'].str_value == 'y'
    config.write_config(str(work / 'config'))
    subprocess.run([sys.executable, 'tools/genconfig.py', '--kconfig', 'Kconfig',
                    '--config', str(work / 'config'), '--header', str(work / 'autoconf.h'),
                    '--makefile', str(work / 'auto.conf')], check=True)
    cc = shlex.split(os.environ.get('HOSTCC', 'cc'))
    subprocess.run(cc + ['-fno-builtin', '-std=gnu90', '-O2', '-Wall', '-Wextra', '-Werror',
        '-fno-pie', '-no-pie', '-ffunction-sections', '-fdata-sections',
        '-iquote', 'include', '-I' + str(work), '-Iinclude/arch/x86_64',
        '-idirafter', 'include', '-iquote', 'bdos', '-iquote', 'fs',
        'tests/shared_root_mapping.c', '-Wl,--gc-sections', '-o', str(work / 'mapping')], check=True)
    subprocess.run([str(work / 'mapping')], check=True)
