#!/usr/bin/env python3
"""Check assembled production SVC stack boundaries and real Kconfig admission.

This is structural assembly analysis, not native interrupted CPU execution.
Native register/return probes exercise the compiled routers independently.
"""
import os
import re
import shlex
import subprocess
import tempfile
from pathlib import Path

import kconfiglib

ROOT = Path(__file__).resolve().parent.parent
os.chdir(ROOT)
os.environ['CONFIG_'] = ''

for preset, admitted in [('rpi1', False), ('rpi2-aspace', True),
                         ('virt-arm-aspace', True)]:
    config = kconfiglib.Kconfig(warn_to_stderr=False)
    config.load_config(str(ROOT / 'configs' / (preset + '_defconfig')))
    config.syms['CONF_WITH_AES'].set_value(0)
    config.syms['CONF_WITH_PROCESS_ASPACES'].set_value(2)
    for name in ('CONF_WITH_NATIVE_SCHED',
                 'CONF_WITH_PCLONE', 'CONF_WITH_PTHJOIN'):
        config.syms[name].set_value(2)
    # Ensure an unrelated unmet prerequisite cannot make the rejection pass.
    assert all(config.syms[n].str_value == 'y' for n in
               ('CONF_WITH_USER_ASPACE', 'CONF_WITH_NATIVE_SCHED',
                'CONF_WITH_PCLONE', 'CONF_WITH_PTHJOIN'))
    config.syms['CONF_WITH_FORK_RETURN_TEST'].set_value(2)
    assert (config.syms['CONF_WITH_FORK_RETURN_TEST'].str_value == 'y') == admitted
    print(preset + ': saved-return admission ' + ('accepted' if admitted else 'rejected'))

cc = shlex.split(os.environ.get('ARMCC', 'arm-none-eabi-gcc'))
objdump = shlex.split(os.environ.get('ARMOBJDUMP', 'arm-none-eabi-objdump'))
with tempfile.TemporaryDirectory(prefix='ptos-svc-alignment-') as temporary:
    work = Path(temporary)
    for machine in ('virt-arm', 'raspi'):
        source = (ROOT / 'bios' / 'machine' / machine / 'startup.S').read_text()
        start = source.index('C_SYM(arm_dispatch_svc):')
        end = source.index('    .globl C_SYM(arm_dispatch_prefetch_abort)', start)
        # Assemble the actual production routing body, selecting higher-half
        # on RPi just as the user-ASPACE configuration does. No copied router.
        (work / 'route.S').write_text(
            '#define C_SYM(n) n\n#define CONF_WITH_HIGHER_HALF 1\n'
            '.arm\n.text\n.global arm_dispatch_svc\n' + source[start:end])
        subprocess.run(cc + ['-march=armv7-a', '-marm', '-c', str(work / 'route.S'),
                             '-o', str(work / 'route.o')], check=True)
        assembly = subprocess.check_output(objdump + ['-d', str(work / 'route.o')], text=True)
        delta, enabled, returned = 0, False, False
        for line in assembly.splitlines():
            match = re.match(r'\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(\w+)\s*(.*)', line)
            if not match:
                continue
            instruction, operands = match.groups()
            if instruction in ('push', 'pop'):
                registers = re.search(r'\{([^}]+)\}', operands).group(1).split(',')
                delta += (4 if instruction == 'pop' else -4) * len(registers)
                returned = instruction == 'pop' and any(r.strip() == 'pc' for r in registers)
            elif instruction in ('ldr', 'str'):
                post = re.search(r'\[sp\],\s*#(\d+)', operands)
                if post:
                    delta += int(post.group(1))
                returned = instruction == 'ldr' and operands.startswith('pc,')
            elif instruction == 'cpsie' and operands.strip() == 'i':
                enabled = True
            # IRQ handlers transfer to SVC's stack. Every observable enabled
            # instruction boundary must satisfy AAPCS eight-byte alignment.
            if enabled:
                assert delta % 8 == 0, (machine, 'IRQ-exposed misaligned SVC stack', line, delta)
            if returned:
                break
        assert returned and enabled and delta == 0, (machine, assembly)
        print(machine + ': assembled SVC IRQ boundaries aligned, routing frame balanced')
