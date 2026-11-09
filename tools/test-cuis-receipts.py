#!/usr/bin/env python3
"""Production CUIS receipt/ring paths with modeled PROCESS service."""
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
for preset in Path('configs').glob('*_defconfig'):
    config=kconfiglib.Kconfig(warn_to_stderr=False); config.load_config(str(preset))
    assert config.syms['CONF_WITH_CUIS_RECEIPT_TEST'].str_value=='n',str(preset)
with tempfile.TemporaryDirectory(prefix='ptos-cuis-observers-') as directory:
    for enabled in (True,):
        work=Path(directory)/str(enabled);work.mkdir()
        config=kconfiglib.Kconfig(warn_to_stderr=False)
        config.load_config('configs/pc-x86_64_defconfig')
        config.syms['CONF_WITH_ELF_SHARED'].set_value(2)
        config.syms['CONF_WITH_LIBAES'].set_value(2)
        config.syms['CONF_WITH_CUIS_OBSERVER_TEST'].set_value(2 if enabled else 0)
        assert config.syms['CONF_WITH_CUIS_OBSERVER_TEST'].str_value==('y' if enabled else 'n')
        config.syms['CONF_WITH_CUIS_RECEIPT_TEST'].set_value(2)
        assert config.syms['CONF_WITH_CUIS_RECEIPT_TEST'].str_value=='y'
        config.write_config(str(work/'config'))
        subprocess.run([sys.executable,'tools/genconfig.py','--kconfig','Kconfig','--config',str(work/'config'),
                        '--header',str(work/'autoconf.h'),'--makefile',str(work/'auto.conf')],check=True)
        subprocess.run(shlex.split(os.environ.get('HOSTCC','cc'))+[
            '-std=gnu90','-O2','-Wall','-Wextra','-Werror','-fno-builtin','-fno-pie','-no-pie',
            '-ffunction-sections','-fdata-sections','-I'+str(work),'-Iinclude/arch/x86_64',
            '-Iinclude','-Ibdos','-Iaes','-Iaes/shared','tests/cuis/receipt_runtime.c','aes/cuis_region.c',
            'aes/shared/gemframegeom.c','-Wl,--gc-sections','-o',str(work/'test')],check=True)
        subprocess.run([str(work/'test')],check=True)
        print('Production CUIS receipt runtime: '+str(enabled)+' PASS')
