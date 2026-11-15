#!/usr/bin/env python3
"""Production standard ELF startup drafts/backends with modeled CPU/MMU."""
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
parser = argparse.ArgumentParser()
parser.add_argument('--mode', choices=['normal', 'asan', 'fortified'], default='normal')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='ptos-elf-startup-') as directory:
    work = Path(directory)
    for preset, arch in [('pc-x86_64', 'x86_64'), ('virt-arm-aspace', 'arm'), ('virt-m68k-aspace', 'm68k')]:
        k = kconfiglib.Kconfig(warn_to_stderr=False)
        k.load_config(f'configs/{preset}_defconfig')
        for name in ['CONF_WITH_NATIVE_SCHED', 'CONF_WITH_SHARED_VM_PEXEC_TEST']:
            k.syms[name].set_value(2)
            assert k.syms[name].str_value == 'y', name
        for name in ['CONF_WITH_PRIVATE_CLONE', 'CONF_WITH_PRIVATE_CLONE_TEST', 'CONF_WITH_MMAP', 'CONF_WITH_FORK_RETURN_TEST']:
            k.syms[name].set_value(0)
        k.write_config(str(work / 'config'))
        subprocess.run([sys.executable, 'tools/genconfig.py', '--kconfig', 'Kconfig', '--config', str(work / 'config'), '--header', str(work / 'autoconf.h'), '--makefile', str(work / 'auto.conf')], check=True)
        flags = []
        if args.mode == 'asan':
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        elif args.mode == 'fortified':
            flags += ['-D_FORTIFY_SOURCE=3', '-fstack-protector-strong']
        backend = 'bdos/arch/x86_64/thread_startup_x32.c' if arch == 'x86_64' else 'bdos/thread_startup32.c'
        cmd = shlex.split(os.environ.get('HOSTCC', 'cc')) + ['-std=gnu90', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-fno-builtin', '-fno-pie', '-no-pie', '-ffunction-sections', '-fdata-sections', '-Dsetjmp=fixture_setjmp', '-iquote', 'include', '-I' + str(work), '-Iinclude/arch/' + arch, '-Iinclude/machine/' + preset.removesuffix('-aspace'), '-iquote', 'bdos', '-iquote', 'bdos/arch/x86_64', '-idirafter', 'include'] + flags
        subprocess.run(cmd + ['tests/elf_thread_startup.c', 'bdos/thread_startup.c', backend, 'bdos/thread_return.c', 'bdos/kthread_context.c', 'bdos/kaspace.c', '-Wl,--gc-sections', '-o', str(work / 'test')], check=True)
        subprocess.run([str(work / 'test')], check=True)
        print(f'ELF startup ({preset}, {args.mode}): PASS', flush=True)
