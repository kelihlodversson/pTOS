#!/usr/bin/env python3
"""Production x32/ARM/m68k bootstrap bounds and atomic relocation providers."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--mode', choices=['normal', 'asan', 'fortified'], default='normal')
args = parser.parse_args()
cc = shlex.split(os.environ.get('HOSTCC', os.environ.get('CC_HOST', 'cc')))
with tempfile.TemporaryDirectory(prefix='ptos-bootstrap-') as directory:
    for arch, flag in [('x32', []), ('arm', ['-DBOOT_TEST_ARM']), ('m68k', ['-DBOOT_TEST_M68K'])]:
        binary = Path(directory) / arch
        flags = []
        if args.mode == 'asan':
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        elif args.mode == 'fortified':
            flags += ['-D_FORTIFY_SOURCE=3', '-fstack-protector-strong']
        subprocess.run(cc + ['-std=gnu90', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-fno-pie', '-no-pie', '-I' + str(ROOT / 'lib/ldso')] + flag + flags + [str(ROOT / 'tests/ldso_runtime/bootstrap_host.c'), str(ROOT / f'lib/ldso/arch/{arch}/bootstrap.c'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        print(f'Production bootstrap ({arch}, {args.mode}): old/shared bias, bounds and unchanged-output rejection PASS', flush=True)
