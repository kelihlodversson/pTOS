#!/usr/bin/env python3
"""Production PROCESS-owned pluggable-filesystem directory preparation."""
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
    # Host driver cookies contain no architecture pointers. Exercise PFS with
    # the host KPROC layout even though real x32 FAT PFS remains unsupported.
    with (work/'autoconf.h').open('a') as header:
        header.write('\n#undef CONF_WITH_PLUGGABLE_FS\n#define CONF_WITH_PLUGGABLE_FS 1\n#define CONF_PFS_MAX_CWD 8\n#define CONF_PFS_MAX_SEARCHES 8\n')
    cc=shlex.split(os.environ.get('HOSTCC','cc'))
    subprocess.run(cc+['-fno-builtin','-std=gnu90','-O2','-Wall','-Wextra','-Werror','-fno-pie','-no-pie',
        '-ffunction-sections','-fdata-sections','-iquote','include','-I'+str(work),
        '-Iinclude/arch/x86_64','-idirafter','include','-iquote','bdos','-iquote','fs',
        'tests/process_directory_pfs.c','-Wl,--gc-sections','-o',str(work/'observer')],check=True)
    subprocess.run([str(work/'observer')],check=True)
