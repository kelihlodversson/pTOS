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
    config.load_config('tools/hosted-legacy-profile.config', replace=False)
    config.syms['CONF_WITH_ELF_SHARED'].set_value(0)
    config.syms['CONF_WITH_PLUGGABLE_FS'].set_value(2)
    config.syms['CONF_PFS_MAX_CWD'].set_value('8')
    assert config.syms['CONF_WITH_XFS'].str_value == 'y'
    config.write_config(str(work/'config'))
    subprocess.run([sys.executable,'tools/genconfig.py','--kconfig','Kconfig','--config',str(work/'config'),
                    '--header',str(work/'autoconf.h'),'--makefile',str(work/'auto.conf')],check=True)
    (work/'setjmp.h').write_text('#include <setjmp.h>\n'
        '#ifdef SETJMP_H\n#error kernel jump ABI selected for hosted libc unwind\n#endif\n')
    cc=shlex.split(os.environ.get('HOSTCC','cc'))
    extra=shlex.split(os.environ.get('HOSTCFLAGS',''))
    if '--sanitize' in sys.argv: extra += ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    flags = cc + extra + ['-fno-builtin','-std=gnu90','-O2','-Wall','-Wextra','-Werror',
        '-fno-pie','-no-pie','-ffunction-sections','-fdata-sections','-iquote',str(work),
        '-iquote','include','-Iinclude/arch/x86_64','-idirafter','include','-iquote','bdos','-iquote','fs']
    for name, sources in (
        ('observer', ['tests/process_directory_pfs.c']),
        ('cache', ['tests/process_directory_cache.c']),
        ('xfs', ['tests/xfs_pfs.c','fs/xfs_pfs.c']),
    ):
        program = work / name
        subprocess.run(flags + sources + ['-Wl,--gc-sections','-o',str(program)],check=True)
        subprocess.run([str(program)],check=True)
