#!/usr/bin/env python3
"""Deterministic probe-window and actual regression-shell guard tests.

This is source-level/host evidence, not native IRQ or scheduling execution.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'tests/pclone/irq_registers.S').read_text()
arm = source.split('#elif defined(__arm__) && defined(__ARM_NEON)', 1)[1].split('#elif', 1)[0]
# Enumerate writes of a constant to the sentinel register. A live IRQ can
# arrive on any instruction, including comparisons after the loop. One
# observation window only: installed last, invalidated before scratch checks.
constants = re.findall(r'\bldr\s+r12,\s*=(0x[0-9a-fA-F]+)', arm)
assert constants.count('0x71307132') == 1, 'ARM probe reopens sentinel during post-loop comparisons'
assert arm.index('ldr r12,=0x71307132') < arm.index('1:') < arm.index('mov r12,#0') < arm.index('ldr r1,[sp,#4]')
assert 'cmp r1,r0' in arm, 'saved sentinel must be checked without reopening the IRQ window'
print('ARM IRQ probe: one sentinel window, closed before comparison scratch: PASS (source model)')

script = (root / 'tools/run-regression-tests.sh').read_text()
markers = {
    'native completion sequence': 'native completion sequence check/block retry: PASS',
    'native owner generation wake': 'native owner generation wake with false predicate: PASS',
    'native user IRQ capture': 'native x32 interrupted-user GPR/FXSAVE/iret capture: PASS',
}
with tempfile.TemporaryDirectory(prefix='ptos-scheduler-probes-') as directory:
    top = Path(directory)
    (top / 'obj').mkdir()
    log = top / 'suite.log'
    for label, marker in markers.items():
        # Execute the production block, including its feature predicates and
        # failure path, rather than reimplementing the driver in the test.
        end = script.index('\nfi', script.index('record "' + label + '"')) + 3
        start = script.rfind('\nif ', 0, end) + 1
        block = script[start:end]
        for sched in (False, True):
            for clone in (False, True):
                for handoff in (False, True):
                    for memtest in (False, True):
                        for present in (False, True):
                            enabled = {'CONF_WITH_NATIVE_SCHED': sched, 'CONF_WITH_PCLONE': clone,
                                       'CONF_WITH_EXIT_HANDOFF_TEST': handoff, 'CONF_WITH_X86_64_MEMTEST': memtest}
                            (top / 'obj/autoconf.h').write_text(''.join('#define ' + k + ' 1\n' for k,v in enabled.items() if v))
                            log.write_text(marker + '\n' if present else '')
                            shell = 'top=$1; suite_log=$2; machine=pc-x86_64\nrecord() { printf "%s" "$1"; }\n' + block
                            result = subprocess.run(['sh', '-c', shell, 'probe', str(top), str(log)], check=True, text=True, capture_output=True)
                            required = sched and ((clone and (handoff or memtest)) if label == 'native user IRQ capture' else handoff)
                            assert result.stdout == (label if required and not present else ''), (label, enabled, present, result.stdout)
print('Production regression guards: 96 feature/marker cases, required missing evidence still fails: PASS (host shell)')
