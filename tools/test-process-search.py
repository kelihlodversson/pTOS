#!/usr/bin/env python3
"""Production trusted DTA/search snapshot ownership and rollback."""
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
    config.syms['CONF_PFS_MAX_SEARCHES'].set_value('16')
    config.syms['CONF_WITH_NATIVE_SCHED'].set_value(0)
    config.write_config(str(work/'config'))
    subprocess.run([sys.executable,'tools/genconfig.py','--kconfig','Kconfig','--config',str(work/'config'),
                    '--header',str(work/'autoconf.h'),'--makefile',str(work/'auto.conf')],check=True)
    (work/'setjmp.h').write_text('#include <setjmp.h>\n#ifdef SETJMP_H\n#error kernel jmp_buf selected\n#endif\n')
    cc=shlex.split(os.environ.get('HOSTCC','cc'))
    extra=shlex.split(os.environ.get('HOSTCFLAGS',''))
    if '--sanitize' in sys.argv: extra += ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    flags=cc+extra+['-fno-builtin','-std=gnu90','-O2','-Wall','-Wextra','-Werror','-fno-pie','-no-pie',
        '-ffunction-sections','-fdata-sections','-iquote',str(work),'-iquote','include',
        '-Iinclude/arch/x86_64','-iquote','bdos','-iquote','fs']
    for name in ('CONF_WITH_ELF_SHARED','CONF_WITH_CUIS','CONF_WITH_X32_VDI',
                 'CONF_WITH_USER_CALLBACKS','CONF_WITH_PTHJOIN','CONF_WITH_PTERMGRP'):
        config.syms[name].set_value(0)
    config.write_config(str(work/'config'))
    subprocess.run([sys.executable,'tools/genconfig.py','--kconfig','Kconfig','--config',str(work/'config'),
                    '--header',str(work/'autoconf.h'),'--makefile',str(work/'auto.conf')],check=True)
    subprocess.run(flags+['tests/process_search.c','bdos/process_files.c','bdos/process_observer.c',
        'bdos/fsclose.c','bdos/fsopnclo.c','bdos/fshand.c','bdos/fsmain.c',
        '-Wl,--gc-sections','-o',str(work/'search')],check=True)
    subprocess.run([str(work/'search')],check=True)
    config.syms['CONF_WITH_PRIVATE_CLONE'].set_value(2)
    assert config.syms['CONF_WITH_PRIVATE_CLONE'].str_value=='y'
    config.write_config(str(work/'config'))
    subprocess.run([sys.executable,'tools/genconfig.py','--kconfig','Kconfig','--config',str(work/'config'),
                    '--header',str(work/'autoconf.h'),'--makefile',str(work/'auto.conf')],check=True)
    subprocess.run(flags+['tests/process_search_dta.c','bdos/process_files.c',
        '-Wl,--gc-sections','-o',str(work/'dta')],check=True)
    subprocess.run([str(work/'dta')],check=True)
