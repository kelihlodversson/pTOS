#!/usr/bin/env python3
"""Check native feature defaults and refusal profiles through actual Kconfig."""
import os
from pathlib import Path

import kconfiglib

root = Path(__file__).resolve().parents[1]
os.chdir(root)
os.environ['CONFIG_'] = ''

def load(preset):
    config = kconfiglib.Kconfig(warn_to_stderr=False)
    config.load_config(str(preset))
    assert not [name for name, _ in config.missing_syms if not name.startswith("#")]
    return config

def enabled(config, name):
    return config.syms[name].str_value == 'y'

def expect(config, **expected):
    for name, wanted in expected.items():
        assert enabled(config, 'CONF_WITH_' + name) == wanted, (name, wanted)
    assert not config.warnings, config.warnings

presets = sorted((root / 'configs').glob('*_defconfig'))
for preset in presets:
    config = load(preset)
    on = lambda name: enabled(config, name)
    native = on('CONF_WITH_USER_ASPACE') and (not on('ARCH_ARM') or on('CPU_ARMV7'))
    exec_replace = native and (on('CONF_WITH_ELF_LOADER') or
                               (on('ARCH_M68K_CLASSIC') and on('CONF_WITH_PRG_LOADER')))
    can_fork = on('ARCH_X86_64') or on('CPU_ARMV7') or (
        on('ARCH_M68K_CLASSIC') and not on('CONF_WITH_MFASTCALL') and
        not on('CONF_M68K_ROM_WINDOW'))
    can_preempt = on('ARCH_X86_64') or on('CPU_ARMV7') or (
        on('ARCH_M68K_CLASSIC') and not on('CONF_WITH_MFASTCALL') and
        (on('MACHINE_VIRT_M68K') or on('CONF_M68K_ROM_WINDOW_040')))
    try:
        expect(config, MULTITHREADING=native, PROCESS_FILES=native,
               EXEC_REPLACE=exec_replace, FORK=exec_replace and can_fork,
               PREEMPTION=native and can_preempt, IRQ_RESTART=native and can_preempt,
               PRIVATE_CLONE=exec_replace and can_fork)
    except AssertionError as error:
        raise AssertionError(preset.name, *error.args) from error

config = load('configs/virt-m68k-aspace_defconfig')
config.syms['CONF_WITH_ELF_LOADER'].set_value(0)
expect(config, ELF_LOADER=False, PRG_LOADER=True, EXEC_REPLACE=True,
       FORK=True, PREEMPTION=True)
config.syms['CONF_WITH_PRG_LOADER'].set_value(0)
config.syms['CONF_WITH_EXEC_REPLACEMENT_TEST'].set_value(2)
expect(config, EXEC_REPLACE=False, FORK=False, EXEC_REPLACEMENT_TEST=False)
config.syms['CONF_WITH_PRG_LOADER'].set_value(2)
config.syms['CONF_WITH_EXEC_REPLACE'].set_value(0)
config.syms['CONF_WITH_PRIVATE_CLONE_TEST'].set_value(2)
expect(config, PRIVATE_CLONE=True, FORK=False, EXEC_REPLACEMENT_TEST=False)
config.syms['CONF_WITH_PREEMPTION'].set_value(0)
config.syms['CONF_WITH_IRQ_RESTART_TEST'].set_value(2)
expect(config, PREEMPTION=False, IRQ_RESTART_TEST=True, IRQ_RESTART=True)
config.syms['CONF_WITH_MULTITHREADING'].set_value(0)
expect(config, MULTITHREADING=False, PROCESS_FILES=False, PRIVATE_CLONE=False,
       EXEC_REPLACE=False, FORK=False, PREEMPTION=False, IRQ_RESTART=False)

for preset in ('falcon-aspace040', 'amiga-aspace040'):
    config = load(f'configs/{preset}_defconfig')
    config.syms['CONF_WITH_MFASTCALL'].set_value(0)
    expect(config, MULTITHREADING=True, PREEMPTION=True, PRIVATE_CLONE=False, FORK=False)
    config.syms['CONF_WITH_MFASTCALL'].set_value(2)
    expect(config, MULTITHREADING=True, PREEMPTION=False, PRIVATE_CLONE=False, FORK=False)

print(f'{len(presets)} defconfigs and native policy, loader, eager/ROM and refusal variants: PASS')
