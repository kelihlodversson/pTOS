#!/usr/bin/env python3
"""Deterministic probe-window and actual regression-shell guard tests.

This is source-level/host evidence, not native IRQ or scheduling execution.
"""
from pathlib import Path
import re
import os
import shlex
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

# The disabled/eager MEMTEST baseline can enable the scheduler without any
# executable asynchronous fixture. Run the actual six-marker shell block.
mmap_script = (root / 'tools/test-mmap-boot.sh').read_text()
start = mmap_script.index("if grep -q '^CONF_WITH_NATIVE_SCHED *= *y'")
end = mmap_script.index('\nfi', start) + 3
block = mmap_script[start:end]
required_markers = re.findall(r"grep -q '([^']+: PASS)'", block)
assert len(required_markers) == 6
with tempfile.TemporaryDirectory(prefix='ptos-memtest-probes-') as directory:
    top = Path(directory)
    (top / 'obj').mkdir()
    log = top / 'boot.log'
    for mask in range(16):
        enabled = {name: bool(mask & (1 << i)) for i, name in enumerate(
            ['NATIVE_SCHED', 'MMAP', 'PCLONE', 'LIBAES'])}
        (top / 'obj/auto.conf').write_text(''.join('CONF_WITH_' + k + '=y\n' for k,v in enabled.items() if v))
        required = enabled['NATIVE_SCHED'] and any(enabled[k] for k in ['MMAP', 'PCLONE', 'LIBAES'])
        for missing in [None] + required_markers:
            log.write_text('\n'.join(m for m in required_markers if m != missing) + '\n')
            result = subprocess.run(['sh', '-ec', 'log=$1\n' + block, 'probe', str(log)], cwd=top)
            assert (result.returncode != 0) == bool(required and missing), (enabled, missing, result.returncode)
print('Production MEMTEST guards: 112 fixture/marker cases, enabled missing evidence still fails: PASS (host shell)')

# Compile the real AUTO user program with bounded hosted endpoints. The
# previous OS-version selector never advances and trips the yield limit.
with tempfile.TemporaryDirectory(prefix='ptos-scheduler-clock-') as directory:
    top = Path(directory)
    (top / 'mint').mkdir()
    (top / 'mint/basepage.h').write_text('typedef struct { unsigned char p_cmdlin[128]; void *p_env; } BASEPAGE;\nextern BASEPAGE *_base;\n')
    (top / 'mint/osbind.h').write_text("""long Fopen(const char *,short); long Fcreate(const char *,short);
long Fread(long,long,void *); long Fwrite(long,long,const void *); long Fclose(long);
long Cconws(const char *); long Cconout(short); void Pterm(short);
long Pexec(short,const char *,const void *,const void *); long Mfree(void *);
""")
    (top / 'mint/mintbind.h').write_text("""#define S_GETLVAL 0x000a
long Ssystem(short,long,long); long Syield(void); long Pgetpid(void); long Pgetppid(void);
long Pwaitpid(short,short,long);
""")
    command = shlex.split(os.environ.get('HOSTCC', 'cc')) + ['-std=gnu90', '-O2', '-Wall', '-Wextra', '-Werror',
              '-I' + str(top), str(root / 'tests/global_scheduler/clock.c'), '-o', str(top / 'clock')]
    subprocess.run(command, check=True)
    subprocess.run([str(top / 'clock')], check=True)

# Execute the independent production case, including alternatives absent from
# the older enclosing managed-worker case. Missing evidence must fail.
block = script.split('# Native initial-entry marker:', 1)[1]
block = block[block.index('\ncase '):].split('# End native initial-entry marker.', 1)[0]
with tempfile.TemporaryDirectory(prefix='ptos-initial-probe-') as directory:
    top = Path(directory)
    (top / 'obj').mkdir()
    log = top / 'suite.log'
    for machine in ('virt-arm', 'virt-m68k-aspace', 'virt-m68k-mmu', 'raspi2b-aspace', 'pc-x86_64'):
        for sched in (0, 1):
            for clone in (0, 1):
                for present in (False, True):
                    (top / 'obj/autoconf.h').write_text('#define CONF_WITH_NATIVE_SCHED %d\n#define CONF_WITH_PCLONE %d\n' % (sched, clone))
                    log.write_text('native clone first-instruction state: PASS\n' if present else '')
                    shell = 'machine=$1; top=$2; suite_log=$3\nrecord() { printf "%s" "$1"; }\n' + block
                    r = subprocess.run(['sh', '-c', shell, 'probe', machine, str(top), str(log)], text=True, capture_output=True, check=True)
                    required = machine != 'pc-x86_64' and sched and clone and not present
                    assert r.stdout == ('native clone initial register state' if required else ''), (machine, sched, clone, present, r.stdout)
print('Native initial-entry production guards: all machine alternatives and disabled gates: PASS (host shell)')
