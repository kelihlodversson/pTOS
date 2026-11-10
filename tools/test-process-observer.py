#!/usr/bin/env python3
"""Production PROCESS observation lifetime; absent CPU/MMU resources on host."""
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
    config.write_config(str(work/'config'))
    subprocess.run([sys.executable,'tools/genconfig.py','--kconfig','Kconfig','--config',str(work/'config'),
                    '--header',str(work/'autoconf.h'),'--makefile',str(work/'auto.conf')],check=True)
    cc=shlex.split(os.environ.get('HOSTCC','cc'))
    subprocess.run(cc+['-fno-builtin','-std=gnu90','-O2','-Wall','-Wextra','-Werror','-fno-pie','-no-pie',
        '-ffunction-sections','-fdata-sections','-iquote','include','-I'+str(work),
        '-Iinclude/arch/x86_64','-idirafter','include','-iquote','bdos','-iquote','fs',
        'tests/process_observer.c','bdos/process_observer.c','-Wl,--gc-sections','-o',str(work/'observer')],check=True)
    subprocess.run([str(work/'observer')],check=True)
    subprocess.run(cc+['-fno-builtin','-std=gnu90','-O2','-Wall','-Wextra','-Werror',
        '-fno-pie','-no-pie','-ffunction-sections','-fdata-sections',
        '-iquote','include','-I'+str(work),'-Iinclude/machine/pc-x86_64',
        '-Iinclude/arch/x86_64','-Ibios/machine/pc-x86_64','-Ibios/arch/x86_64','-idirafter','include',
        'tests/process_allocation_observer.c','-Wl,--gc-sections','-o',str(work/'allocation')],check=True)
    subprocess.run([str(work/'allocation')],check=True)

    # The same production registry with native owner teardown enabled: model
    # a prepared-child finalizer at proc_exec_owner_exit and use real ixforce.
    config.syms['CONF_WITH_NATIVE_SCHED'].set_value(2)
    config.write_config(str(work/'config'))
    subprocess.run([sys.executable,'tools/genconfig.py','--kconfig','Kconfig','--config',str(work/'config'),
                    '--header',str(work/'autoconf.h'),'--makefile',str(work/'auto.conf')],check=True)
    subprocess.run(cc+['-fno-builtin','-std=gnu90','-O2','-Wall','-Wextra','-Werror','-fno-pie','-no-pie',
        '-ffunction-sections','-fdata-sections','-iquote','include','-I'+str(work),
        '-Iinclude/arch/x86_64','-idirafter','include','-iquote','bdos','-iquote','fs',
        'tests/process_observer.c','bdos/process_observer.c','bdos/fshand.c',
        '-Wl,--gc-sections','-o',str(work/'native-owner')],check=True)
    subprocess.run([str(work/'native-owner')],check=True)
