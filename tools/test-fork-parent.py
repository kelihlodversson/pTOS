#!/usr/bin/env python3
"""Production COW-vfork dependency and KTHREAD retirement with modeled CPU/events."""
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
with tempfile.TemporaryDirectory(prefix='ptos-fork-parent-') as directory:
    work = Path(directory)
    config = kconfiglib.Kconfig(warn_to_stderr=False)
    config.load_config('configs/pc-x86_64_defconfig')
    for symbol in ('CONF_WITH_MULTITHREADING',
                   'CONF_WITH_PRIVATE_CLONE', 'CONF_WITH_PRIVATE_CLONE_TEST', 'CONF_WITH_FORK_RETURN_TEST'):
        config.syms[symbol].set_value(2)
    assert config.syms['CONF_WITH_FORK_CORE'].str_value == 'y'
    config.write_config(str(work/'config'))
    subprocess.run([sys.executable, 'tools/genconfig.py', '--kconfig', 'Kconfig',
                    '--config', str(work/'config'), '--header', str(work/'autoconf.h'),
                    '--makefile', str(work/'auto.conf')], check=True)
    cc = shlex.split(os.environ.get('HOSTCC', 'cc'))
    subprocess.run(cc+['-std=gnu90','-O2','-Wall','-Wextra','-Werror','-fno-builtin',
        '-ffunction-sections','-fdata-sections','-fno-pie','-no-pie','-iquote','include',
        '-I'+str(work),'-Iinclude/arch/x86_64','-idirafter','include','-iquote','bdos',
        '-iquote','fs','tests/fork_parent.c','bdos/kowner.c','tests/kowner_model.c',
        '-Wl,--gc-sections','-o',str(work/'test')], check=True)
    subprocess.run([str(work/'test')], check=True)
