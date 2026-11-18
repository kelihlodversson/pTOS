#!/usr/bin/env python3
"""Production retained IRQ transaction/leases; modeled CPU/PROCESS/MMU edges."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

import kconfiglib

parser = argparse.ArgumentParser(description=__doc__)
mode = parser.add_mutually_exclusive_group()
mode.add_argument('--sanitize', action='store_true')
mode.add_argument('--fortify', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
os.chdir(root)
os.environ['CONFIG_'] = ''
with tempfile.TemporaryDirectory(prefix='ptos-irq-restart-') as directory:
    work = Path(directory)
    k = kconfiglib.Kconfig(warn_to_stderr=False)
    k.load_config('configs/pc-x86_64_defconfig')
    k.load_config('tools/hosted-legacy-profile.config', replace=False)
    assert k.syms['CONF_WITH_IRQ_RESTART_TEST'].str_value == 'n'
    for name in ('NATIVE_SCHED', 'PCLONE', 'PTHJOIN', 'PTERMGRP', 'MMAP', 'PRIVATE_CLONE',
                 'PRIVATE_CLONE_TEST', 'IRQ_RESTART_TEST'):
        k.syms['CONF_WITH_' + name].set_value(2)
    assert k.syms['CONF_WITH_IRQ_RESTART_TEST'].str_value == 'y'
    for dependency in ('CONF_WITH_MMAP', 'CONF_WITH_PTHJOIN'):
        k.syms[dependency].set_value(0)
        assert k.syms['CONF_WITH_IRQ_RESTART_TEST'].str_value == 'n'
        k.syms[dependency].set_value(2)
        assert k.syms['CONF_WITH_IRQ_RESTART_TEST'].str_value == 'y'
    k.write_config(str(work / 'config'))
    subprocess.run(['python3', 'tools/genconfig.py', '--kconfig', 'Kconfig',
                    '--config', str(work / 'config'), '--header', str(work / 'autoconf.h'),
                    '--makefile', str(work / 'auto.conf')], check=True)
    flags = ['-std=gnu90', '-O2', '-Wall', '-Wextra', '-Werror',
             '-ffunction-sections', '-fdata-sections']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    if args.fortify:
        flags += ['-D_FORTIFY_SOURCE=3', '-fstack-protector-strong']
    subprocess.run(shlex.split(os.environ.get('HOSTCC', 'cc')) + flags + [
        '-iquote', 'include', '-I' + str(work), '-Iinclude/arch/x86_64',
        '-idirafter', 'include', '-iquote', 'bdos', '-iquote', 'fs',
        'tests/interrupt_restart.c', 'bdos/kaspace.c', '-Wl,--gc-sections',
        '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
