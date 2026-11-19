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
    for preset, proof, preempt in [('amiga-aspace', False, False),
                                    ('amiga-aspace040', False, False),
                                    ('amiga-aspace040', True, False),
                                    ('amiga-aspace', True, False),
                                    ('amiga-aspace040', True, True),
                                    ('amiga-aspace', True, True)]:
        config = kconfiglib.Kconfig(warn_to_stderr=False)
        config.load_config('configs/' + preset + '_defconfig')
        if preset == 'amiga-aspace':
            assert config.syms['CONF_AMIGA_ROM_512K'].str_value == 'n'
        config.write_config()
        os.environ['IRQ_COPY_PROOF'] = str(int(proof))
        os.environ['NATIVE_PREEMPT'] = str(int(preempt))
        scope = {}
        try:
            exec(compile(configure, 'ci-amiberry.sh/PYCONFIG', 'exec'), scope)
        except AssertionError:
            assert proof and preset == 'amiga-aspace', (preset, proof)
            continue
        assert not (proof and preset == 'amiga-aspace'), '030 copy proof accepted'
        configured = scope['k']
        assert configured.syms['CONF_WITH_MFASTCALL'].str_value == ('n' if proof else 'y')
        assert configured.syms['CONF_WITH_SAVED_RETURN'].str_value == ('y' if proof else 'n')
        assert configured.syms['CONF_WITH_PFORK'].str_value == 'n'
        assert configured.syms['CONF_WITH_NATIVE_PREEMPT'].str_value == ('y' if preempt else 'n')
        assert configured.syms['CONF_WITH_IRQ_RESTART_TEST'].str_value == 'n'
        assert configured.syms['CONF_AMIGA_ROM_512K'].str_value == 'y'
print('Amiberry native config: ordinary fastcall retained, 040 stack-ABI proof and timer policy, 030 refusal: PASS (configuration only)')
