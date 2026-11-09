#!/usr/bin/env python3
"""Production execution cleanup guard with modeled identities and IRQs."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import kconfiglib
ROOT=Path(__file__).resolve().parents[1]
os.chdir(ROOT)
os.environ['CONFIG_']=''
with tempfile.TemporaryDirectory(prefix='ptos-process-observer-') as directory:
    work=Path(directory)
    config=kconfiglib.Kconfig(warn_to_stderr=False)
    config.load_config('configs/pc-x86_64_defconfig')
    config.syms['CONF_WITH_ELF_SHARED'].set_value(0)
    config.syms['CONF_WITH_NATIVE_SCHED'].set_value(2)
    config.syms['CONF_WITH_PCLONE'].set_value(2)
    config.syms['CONF_WITH_PTHJOIN'].set_value(2)
    config.syms['CONF_WITH_PTERMGRP'].set_value(2)
    config.write_config(str(work/'config'))
    subprocess.run([sys.executable,'tools/genconfig.py','--kconfig','Kconfig','--config',str(work/'config'),
                    '--header',str(work/'autoconf.h'),'--makefile',str(work/'auto.conf')],check=True)
    (work/'setjmp.h').write_text('#include <setjmp.h>\n')
    cc=shlex.split(os.environ.get('HOSTCC','cc'))
    subprocess.run(cc+['-fno-builtin','-std=gnu90','-O2','-Wall','-Wextra','-Werror','-fno-pie','-no-pie',
        '-ffunction-sections','-fdata-sections','-iquote','include','-I'+str(work),
        '-Iinclude/arch/x86_64','-idirafter','include','-iquote','bdos','-iquote','fs',
        'tests/cuis/cleanup_guard.c','-Wl,--gc-sections','-o',str(work/'observer')],check=True)
    subprocess.run([str(work/'observer')],check=True)
