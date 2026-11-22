#!/usr/bin/env python3
"""Execute the production native-lane configuration, including refusal gates."""
from pathlib import Path
import os
import tempfile
import kconfiglib

root = Path(__file__).resolve().parents[1]
os.chdir(root)
os.environ['CONFIG_'] = ''
source = (root / 'tools/ci-amiberry.sh').read_text()
start = source.index('import kconfiglib, os')
configure = source[start:source.index('\nPYCONFIG', start)]
with tempfile.TemporaryDirectory(prefix='ptos-amiberry-config-') as directory:
    os.environ['KCONFIG_CONFIG'] = str(Path(directory) / 'config')
    for preset, proof, preempt, restart, fastcall in [
            ('amiga-aspace', False, False, False, True),
            ('amiga-aspace040', False, False, False, True),
            ('amiga-aspace040', True, False, False, False),
            ('amiga-aspace', True, False, False, False),
            ('amiga-aspace040', True, True, False, False),
            ('amiga-aspace', True, True, False, False),
            ('amiga-aspace', False, False, True, False),
            ('amiga-aspace040', True, False, True, False),
            ('amiga-aspace040', False, False, True, False),
            ('amiga-aspace040', False, False, True, True)]:
        config = kconfiglib.Kconfig(warn_to_stderr=False)
        config.load_config('configs/' + preset + '_defconfig')
        if preset == 'amiga-aspace':
            assert config.syms['CONF_AMIGA_ROM_512K'].str_value == 'n'
        config.write_config()
        os.environ['IRQ_COPY_PROOF'] = str(int(proof))
        os.environ['NATIVE_PREEMPT'] = str(int(preempt))
        os.environ['IRQ_RESTART_TEST'] = str(int(restart))
        os.environ['IRQ_RESTART_FASTCALL'] = str(int(fastcall))
        scope = {}
        try:
            exec(compile(configure, 'ci-amiberry.sh/PYCONFIG', 'exec'), scope)
        except AssertionError:
            assert proof and preset == 'amiga-aspace', (preset, proof)
            continue
        assert not (proof and preset == 'amiga-aspace'), '030 copy proof accepted'
        configured = scope['k']
        assert configured.syms['CONF_WITH_MFASTCALL'].str_value == ('y' if fastcall else 'n')
        assert configured.syms['CONF_WITH_SAVED_RETURN'].str_value == ('y' if proof or (restart and not fastcall) else 'n')
        assert configured.syms['CONF_WITH_FORK'].str_value == 'n'
        assert configured.syms['CONF_WITH_PREEMPTION'].str_value == ('y' if preempt else 'n')
        assert configured.syms['CONF_WITH_IRQ_RESTART_TEST'].str_value == ('y' if restart else 'n')
        assert configured.syms['CONF_AMIGA_ROM_512K'].str_value == 'y'
print('Amiberry native config: cooperative, deferred IRQ, timer and unsupported stack/FPU profiles: PASS (configuration only)')
