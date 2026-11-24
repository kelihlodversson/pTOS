#!/usr/bin/env python3
"""Production PROCESS descriptor/SFT ownership, host disk/execution models."""
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
with tempfile.TemporaryDirectory(prefix='ptos-process-files-') as directory:
    work=Path(directory)
    config=kconfiglib.Kconfig(warn_to_stderr=False)
    config.load_config('configs/virt-arm-aspace_defconfig')
    for name in ('CONF_WITH_PLUGGABLE_FS','CONF_WITH_PROCESS_FILES_TEST'):
        config.syms[name].set_value(2)
        assert config.syms[name].str_value=='y'
    for name in ('CONF_WITH_EXEC_REPLACE', 'CONF_WITH_PREEMPTION'):
        config.syms[name].set_value(0)
    config.write_config(str(work/'config'))
    subprocess.run([sys.executable,'tools/genconfig.py','--kconfig','Kconfig','--config',str(work/'config'),
                    '--header',str(work/'autoconf.h'),'--makefile',str(work/'auto.conf')],check=True)
    (work/'setjmp.h').write_text('#include <setjmp.h>\n#ifdef SETJMP_H\n#error kernel jmp_buf selected\n#endif\n')
    cc=shlex.split(os.environ.get('HOSTCC','cc'))
    flags=cc+['-fno-builtin','-std=gnu90','-O2','-Wall','-Wextra','-Werror','-fno-pie','-no-pie',
        '-ffunction-sections','-fdata-sections','-iquote',str(work),'-iquote','include',
        '-Iinclude/arch/x86_64','-iquote','bdos','-iquote','fs']
    subprocess.run(flags+['tests/process_files.c','bdos/process_files.c','bdos/fsclose.c','bdos/fsopnclo.c',
        'bdos/fshand.c','bdos/fsmain.c','bdos/kowner.c','tests/kowner_model.c','-Wl,--gc-sections','-o',str(work/'files')],check=True)
    subprocess.run([str(work/'files')],check=True)

    # Actual registry destruction: a finalizer reenters the same PD and destroys
    # the predecessor that originally owned the outer live-list cursor.
    for name in ('CONF_WITH_ELF_SHARED','CONF_WITH_CUIS','CONF_WITH_X32_VDI',
                 'CONF_WITH_USER_CALLBACKS'):
        config.syms[name].set_value(0)
    config.write_config(str(work/'config'))
    subprocess.run([sys.executable,'tools/genconfig.py','--kconfig','Kconfig','--config',str(work/'config'),
                    '--header',str(work/'autoconf.h'),'--makefile',str(work/'auto.conf')],check=True)
    subprocess.run(flags+['tests/process_files_retirement.c','bdos/process_files.c','bdos/process_observer.c',
        'bdos/fsclose.c','bdos/fsopnclo.c','bdos/fshand.c','bdos/fsmain.c',
        'bdos/kowner.c','tests/kowner_model.c','-Wl,--gc-sections','-o',str(work/'retirement')],check=True)
    subprocess.run([str(work/'retirement')],check=True)

    # The existing FAT host provider uses the non-PFS split pointer cookie.
    config.syms['CONF_WITH_PLUGGABLE_FS'].set_value(0)
    config.write_config(str(work/'config'))
    subprocess.run([sys.executable,'tools/genconfig.py','--kconfig','Kconfig','--config',str(work/'config'),
                    '--header',str(work/'autoconf.h'),'--makefile',str(work/'auto.conf')],check=True)
    subprocess.run(flags+['tests/fat_reclaim/process_files_admission.c','bdos/process_files.c',
        'bdos/kowner.c','tests/kowner_model.c','-Wl,--gc-sections','-o',str(work/'driver-admission')],check=True)
    subprocess.run([str(work/'driver-admission')],check=True)
