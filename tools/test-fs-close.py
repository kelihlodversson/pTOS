#!/usr/bin/env python3
"""Production SFT close ownership and bounded recovery; disk I/O is injected."""
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
with tempfile.TemporaryDirectory(prefix='ptos-close-') as directory:
    work=Path(directory)
    config=kconfiglib.Kconfig(warn_to_stderr=False)
    config.load_config('configs/virt-arm-aspace_defconfig')
    config.syms['CONF_WITH_PLUGGABLE_FS'].set_value(2)
    config.write_config(str(work/'config'))
    subprocess.run([sys.executable,'tools/genconfig.py','--kconfig','Kconfig','--config',str(work/'config'),
                    '--header',str(work/'autoconf.h'),'--makefile',str(work/'auto.conf')],check=True)
    (work/'setjmp.h').write_text('#include <setjmp.h>\n'
        '#ifdef SETJMP_H\n#error kernel jmp_buf selected for hosted libc unwind\n#endif\n')
    cc=shlex.split(os.environ.get('HOSTCC','cc'))
    flags=cc+['-fno-builtin','-std=gnu90','-O2','-Wall','-Wextra','-Werror','-fno-pie','-no-pie',
        '-ffunction-sections','-fdata-sections','-iquote',str(work),'-iquote','include',
        '-Iinclude/arch/x86_64','-iquote','bdos','-iquote','fs']
    subprocess.run(flags+['tests/fs_close.c','bdos/fsclose.c','bdos/fsopnclo.c','bdos/fshand.c','bdos/fsmain.c','-Wl,--gc-sections','-o',str(work/'close')],check=True)
    subprocess.run([str(work/'close')],check=True)

    config.syms['CONF_WITH_NATIVE_SCHED'].set_value(2)
    config.write_config(str(work/'config'))
    subprocess.run([sys.executable,'tools/genconfig.py','--kconfig','Kconfig','--config',str(work/'config'),
                    '--header',str(work/'autoconf.h'),'--makefile',str(work/'auto.conf')],check=True)
    subprocess.run(flags+['tests/fs_close_rights.c','bdos/fsclose.c','bdos/fsopnclo.c',
        'bdos/fshand.c','bdos/fsmain.c','-Wl,--gc-sections','-o',str(work/'rights')],check=True)
    subprocess.run([str(work/'rights')],check=True)
