#!/usr/bin/env python3
"""Production procmem holds under three ABI configs; physical windows modeled."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
os.chdir(ROOT)
with tempfile.TemporaryDirectory(prefix='ptos-allocation-hold-') as directory:
    work=Path(directory)
    cc=shlex.split(os.environ.get('HOSTCC','cc'))
    for preset,arch,machine in [('pc-x86_64','x86_64','pc-x86_64'),('virt-arm-aspace','arm','virt-arm'),('virt-m68k-aspace','m68k','virt-m68k')]:
        subprocess.run([sys.executable,'tools/genconfig.py','--kconfig','Kconfig','--config',f'configs/{preset}_defconfig',
            '--header',str(work/'autoconf.h'),'--makefile',str(work/'auto.conf')],env={**os.environ,'CONFIG_':''},check=True)
        flags=[] if arch=='x86_64' else ['-DALLOCATION_HOLD_GENERIC']
        subprocess.run(cc+['-fno-builtin','-std=gnu90','-O2','-Wall','-Wextra','-Werror',
            '-fno-pie','-no-pie','-ffunction-sections','-fdata-sections','-iquote','include','-I'+str(work),
            '-Iinclude/machine/'+machine,'-Iinclude/arch/'+arch,'-Ibios/machine/'+machine,
            '-Ibios/arch/'+arch,'-idirafter','include']+flags+['tests/allocation_hold.c',
            '-Wl,--gc-sections','-o',str(work/'hold')],check=True)
        subprocess.run([str(work/'hold')],check=True)
        print(f'Production allocation hold ({preset}, host execution): PASS',flush=True)
