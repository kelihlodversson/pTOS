#!/usr/bin/env python3
"""Run actual libaes CHILD policy against a modeled transport, not a reaper."""
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
with tempfile.TemporaryDirectory(prefix='ptos-child-adapter-') as directory:
    work = Path(directory)
    config = kconfiglib.Kconfig(warn_to_stderr=False)
    config.load_config('configs/pc-x86_64_defconfig')
    config.write_config(str(work / 'config'))
    subprocess.run([sys.executable, 'tools/genconfig.py', '--kconfig', 'Kconfig',
                    '--config', str(work / 'config'), '--header', str(work / 'autoconf.h'),
                    '--makefile', str(work / 'auto.conf')], check=True)
    subprocess.run(shlex.split(os.environ.get('HOSTCC', 'cc')) + [
        '-fno-builtin', '-std=gnu90', '-O2', '-Wall', '-Wextra', '-Werror',
        '-iquote', 'include', '-I' + str(work), '-Iinclude/arch/x86_64',
        '-idirafter', 'include', 'tests/cuis/child_adapter.c', '-o', str(work / 'adapter')],
        check=True)
    subprocess.run([str(work / 'adapter')], check=True)
