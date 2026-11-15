#!/usr/bin/env python3
"""Reproduce Phase6 production contracts; MMU, disk and registry are host models.

Default asserts current blockers and balanced ownership. --require fs|exec|lazy
runs a positive capability control, expected to exit 1 until its prerequisite
is implemented. This does not execute a user trap or implement public fork.
"""
import argparse
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
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--require', choices=('fs', 'exec', 'lazy'))
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='ptos-phase6-') as directory:
    work = Path(directory)
    (work/'setjmp.h').write_text('#include <setjmp.h>\n#ifdef SETJMP_H\n#error kernel jmp_buf selected\n#endif\n')
    cc = shlex.split(os.environ.get('HOSTCC', 'cc'))
    flags = cc + ['-fno-builtin', '-std=gnu90', '-O2', '-Wall', '-Wextra', '-Werror',
                  '-fno-pie', '-no-pie', '-ffunction-sections', '-fdata-sections',
                  '-iquote', str(work), '-iquote', 'include', '-Iinclude/arch/x86_64',
                  '-idirafter', 'include', '-iquote', 'bdos', '-iquote', 'fs']
    cases = {
        'fs': ('virt-arm-aspace', ['tests/fork_descriptor_control.c', 'bdos/fsclose.c',
                    'bdos/fsopnclo.c', 'bdos/fshand.c', 'bdos/fsmain.c']),
        'exec': ('pc-x86_64', ['tests/fork_exec_control.c', 'bdos/kthread.c']),
        'lazy': ('pc-x86_64', ['tests/fork_lazy_control.c', 'bdos/kaspace.c']),
    }
    for name, (preset, sources) in cases.items():
        if args.require and args.require != name:
            continue
        config = kconfiglib.Kconfig(warn_to_stderr=False)
        config.load_config('configs/' + preset + '_defconfig')
        if name == 'fs':
            config.syms['CONF_WITH_PLUGGABLE_FS'].set_value(2)
        else:
            for symbol in ('CONF_WITH_NATIVE_SCHED', 'CONF_WITH_ELF_SHARED',
                           'CONF_WITH_CUIS', 'CONF_WITH_X32_VDI', 'CONF_WITH_USER_CALLBACKS'):
                config.syms[symbol].set_value(0)
            config.syms['CONF_WITH_X86_64_MEMTEST'].set_value(2)
            config.syms['CONF_WITH_MMAP'].set_value(2 if name == 'exec' else 0)
            config.syms['CONF_WITH_PRIVATE_CLONE'].set_value(2 if name == 'lazy' else 0)
        config.write_config(str(work/'config'))
        subprocess.run([sys.executable, 'tools/genconfig.py', '--kconfig', 'Kconfig',
                        '--config', str(work/'config'), '--header', str(work/'autoconf.h'),
                        '--makefile', str(work/'auto.conf')], check=True)
        output = work/name
        case_flags = flags[:]
        if name == 'fs':
            index = case_flags.index('-idirafter')
            del case_flags[index:index + 2]
        subprocess.run(case_flags + sources + ['-Wl,--gc-sections', '-o', str(output)], check=True)
        result = subprocess.run([str(output)] + (['--require-capability'] if args.require else []))
        if result.returncode:
            sys.exit(result.returncode)
        print('Phase6 ' + name + ' production contract: PASS (host model)', flush=True)
