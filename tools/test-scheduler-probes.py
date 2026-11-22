#!/usr/bin/env python3
"""Deterministic probe-window and actual regression-shell guard tests.

This is source-level/host evidence, not native IRQ or scheduling execution.
"""
from itertools import product
from pathlib import Path
from irq_restart_evidence import DEFERRED, REFUSED, missing_irq_evidence
import re
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'tests/pclone/irq_registers.S').read_text()
# Expand the actual original/alternate assembly macros, then validate each
# observation window. This catches a marker accidentally reinstalled after
# the loop without coupling the guard to spelling of the source macros.
for alternate, marker in [(False, '0x71307132'), (True, '0x72407242')]:
    command = shlex.split(os.environ.get('HOSTCC', 'cc')) + [
        '-E', '-P', '-x', 'assembler-with-cpp', '-U__x86_64__',
        '-D__arm__', '-D__ARM_NEON', '-DTEST_WITH_IRQ_RESTART']
    if alternate:
        command.append('-DIRQ_ALTERNATE')
    arm = subprocess.run(command + ['-'], input=source, check=True,
                         text=True, capture_output=True).stdout
    constants = re.findall(r'\bldr\s+r12,\s*=(0x[0-9a-fA-F]+)', arm)
    assert constants.count(marker) == 1, 'ARM probe reopens sentinel during comparisons'
    assert arm.index('ldr r12,='+marker) < arm.index('1:') < arm.index('mov r12,#0') < arm.index('ldr r1,[sp,#4]')
    assert 'cmp r1,r0' in arm
print('ARM original/alternate IRQ probes: one sentinel window before comparison scratch: PASS (source model)')

script = (root / 'tools/run-regression-tests.sh').read_text()
# Exercise the actual portable deferred-return guard. Capture or an
# immediate copy-back cannot satisfy a delayed-restart requirement.
start = script.index("if grep -q '^#define CONF_WITH_IRQ_RESTART_TEST 1'")
end = script.index('\nfi', start) + 3
block = script[start:end]
with tempfile.TemporaryDirectory(prefix='ptos-deferred-guards-') as directory:
    top = Path(directory)
    (top / 'obj').mkdir()
    log = top / 'suite.log'
    verdicts = (
        ('native delayed IRQ restart', 'native interrupted USER A IRQ / B USER execution / saved A restart: PASS'),
        ('native IRQ proof classification', 'native retained IRQ capture-only and uncredited root return: PASS'),
        ('native IRQ exact-child wait', 'native retained IRQ with managed exact-child wait: PASS'),
        ('native retained IRQ retirement', 'native retained IRQ armed/acknowledged exit and suspended group cancellation: PASS'),
        ('native retained IRQ fault retirement', 'native retained IRQ armed and acknowledged user faults: PASS'),
        ('native retained IRQ orphan continuation', 'native retained IRQ exact-wait cancellation and orphan continuation: PASS'),
    )
    for enabled, cancellation in product((False, True), repeat=2):
        (top / 'obj/autoconf.h').write_text(
            ('#define CONF_WITH_IRQ_RESTART_TEST 1\n' if enabled else '') +
            ('#define CONF_WITH_MULTITHREADING 1\n' if cancellation else ''))
        for present in product((False, True), repeat=len(verdicts)):
            log.write_text('native ARM fresh owned IRQ image round trip: PASS\n' +
                           ''.join(marker + '\n' for (_, marker), seen in zip(verdicts, present) if seen))
            shell = 'top=$1; suite_log=$2\nrecord() { echo "$1"; }\n' + block
            result = subprocess.run(['sh', '-c', shell, 'probe', str(top), str(log)],
                                    check=True, text=True, capture_output=True)
            required = [enabled]*3 + [enabled and cancellation]*3
            missing = [label for (label, _), seen, needed in zip(verdicts, present, required) if needed and not seen]
            assert result.stdout.splitlines() == missing, (enabled, cancellation, present, result.stdout)
print('Native delayed IRQ guards: 256 feature/verdict cases, immediate copy-back evidence refused: PASS (host shell)')
for profile, markers in (('deferred', DEFERRED), ('refused', (REFUSED,))):
    assert not missing_irq_evidence('\r\n'.join(markers), profile)
    assert missing_irq_evidence('\n'.join(DEFERRED + (REFUSED,)), profile)
    for missing in markers:
        assert missing_irq_evidence('\n'.join(m for m in markers if m != missing), profile) == ['missing '+missing]
print('Native ROM IRQ verdicts: incomplete and contradictory evidence refused: PASS (host parser)')

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
            for handoff in (False, True):
                for memtest in (False, True):
                    for present in (False, True):
                        enabled = {'CONF_WITH_MULTITHREADING': sched,
                                   'CONF_WITH_EXIT_HANDOFF_TEST': handoff, 'CONF_WITH_X86_64_MEMTEST': memtest}
                        (top / 'obj/autoconf.h').write_text(''.join('#define ' + k + ' 1\n' for k,v in enabled.items() if v))
                        log.write_text(marker + '\n' if present else '')
                        shell = 'top=$1; suite_log=$2; machine=pc-x86_64\nrecord() { printf "%s" "$1"; }\n' + block
                        result = subprocess.run(['sh', '-c', shell, 'probe', str(top), str(log)], check=True, text=True, capture_output=True)
                        required = sched and ((handoff or memtest) if label == 'native user IRQ capture' else handoff)
                        assert result.stdout == (label if required and not present else ''), (label, enabled, present, result.stdout)
print('Production regression guards: 48 feature/marker cases, required missing evidence still fails: PASS (host shell)')

# The disabled/eager MEMTEST baseline can enable the scheduler without any
# executable asynchronous fixture. Run the actual six-marker shell block.
mmap_script = (root / 'tools/test-mmap-boot.sh').read_text()
start = mmap_script.index("if grep -q '^CONF_WITH_MULTITHREADING *= *y'")
end = mmap_script.index('\nfi', start) + 3
block = mmap_script[start:end]
required_markers = re.findall(r"grep -q '([^']+: PASS)'", block)
assert len(required_markers) == 6
with tempfile.TemporaryDirectory(prefix='ptos-memtest-probes-') as directory:
    top = Path(directory)
    (top / 'obj').mkdir()
    log = top / 'boot.log'
    for mask in range(8):
        enabled = {name: bool(mask & (1 << i)) for i, name in enumerate(
            ['MULTITHREADING', 'MMAP', 'LIBAES'])}
        (top / 'obj/auto.conf').write_text(''.join('CONF_WITH_' + k + '=y\n' for k,v in enabled.items() if v))
        required = enabled['MULTITHREADING']
        for missing in [None] + required_markers:
            log.write_text('\n'.join(m for m in required_markers if m != missing) + '\n')
            result = subprocess.run(['sh', '-ec', 'log=$1\n' + block, 'probe', str(log)], cwd=top)
            assert (result.returncode != 0) == bool(required and missing), (enabled, missing, result.returncode)
print('Production MEMTEST guards: 56 fixture/marker cases, enabled missing evidence still fails: PASS (host shell)')

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
            for present in (False, True):
                (top / 'obj/autoconf.h').write_text('#define CONF_WITH_MULTITHREADING %d\n' % sched)
                log.write_text('native clone first-instruction state: PASS\n' if present else '')
                shell = 'machine=$1; top=$2; suite_log=$3\nrecord() { printf "%s" "$1"; }\n' + block
                r = subprocess.run(['sh', '-c', shell, 'probe', machine, str(top), str(log)], text=True, capture_output=True, check=True)
                required = machine != 'pc-x86_64' and sched and not present
                assert r.stdout == ('native clone initial register state' if required else ''), (machine, sched, present, r.stdout)
print('Native initial-entry production guards: all machine alternatives and disabled gates: PASS (host shell)')

# Nested ownership always requires its three native verdicts. Reclamation
# cancellation additionally requires the optional group-termination feature.
start = script.index('# Configured nested ownership checks')
end = script.index('# The experimental VM test', start)
block = script[start:end]
nested_markers = [
    'native nested Pexec identity/exit modes/restoration: PASS',
    'native nested Pexec construction rollback: PASS',
    'native nested Pexec resource conservation: PASS',
    'native nested Pexec cancellation during reclamation: PASS',
]
with tempfile.TemporaryDirectory(prefix='ptos-nested-probes-') as directory:
    top = Path(directory)
    (top / 'obj').mkdir()
    log = top / 'suite.log'
    for mask in range(4):
        names = ['CONF_WITH_PRIVATE_CLONE_TEST', 'CONF_WITH_MULTITHREADING']
        (top / 'obj/autoconf.h').write_text(''.join(
            '#define ' + name + ' 1\n' for i, name in enumerate(names) if mask & (1 << i)))
        for missing in [None] + nested_markers:
            log.write_text('\n'.join(m for m in nested_markers if m != missing) + '\n')
            shell = 'top=$1; suite_log=$2\nrecord() { printf "%s" "$1"; }\n' + block
            result = subprocess.run(['sh', '-c', shell, 'probe', str(top), str(log)],
                                    check=True, text=True, capture_output=True)
            required = (mask & 3) == 3 and missing is not None
            expected = ('native nested continuation cancellation' if missing == nested_markers[-1]
                        else 'native nested continuation ownership') if required else ''
            assert result.stdout == expected, (mask, missing, result.stdout)
print('Native nested ownership guards: 20 feature/marker cases, cancellation and missing evidence: PASS (host shell)')

start = script.index('# Configured shared-root lifetime probes')
end = script.index('# The experimental VM test', start)
block = script[start:end]
mapping_markers = [
    'native shared-root common/scoped/late mapping lifetime: PASS',
    'native shared-root peer/last-THREAD/orphan safety: PASS',
    'native shared-root exact mapping resource conservation: PASS',
    'native shared-root guarded peer contention: PASS',
    'native shared-root guarded waiter cancellation: PASS',
]
with tempfile.TemporaryDirectory(prefix='ptos-mapping-probes-') as directory:
    top = Path(directory)
    (top / 'obj').mkdir()
    log = top / 'suite.log'
    for mask in range(8):
        names = ['CONF_WITH_PRIVATE_CLONE_TEST', 'CONF_WITH_MULTITHREADING', 'CONF_WITH_MMAP']
        (top / 'obj/autoconf.h').write_text(''.join(
            '#define ' + name + ' 1\n' for i, name in enumerate(names) if mask & (1 << i)))
        for missing in [None] + mapping_markers:
            log.write_text('\n'.join(m for m in mapping_markers if m != missing) + '\n')
            shell = 'top=$1; suite_log=$2\nrecord() { printf "%s" "$1"; }\n' + block
            result = subprocess.run(['sh', '-c', shell, 'probe', str(top), str(log)],
                                    check=True, text=True, capture_output=True)
            required = (mask & 7) == 7 and missing is not None
            expected = ('native shared-root waiter cancellation' if missing == mapping_markers[-1]
                        else 'native shared-root mapping ownership') if required else ''
            assert result.stdout == expected, (mask, missing, result.stdout)
print('Native shared-root mapping guards: 48 feature/marker cases, missing execution evidence fails: PASS (host shell)')

# Execute the final production guard against every collected console/QEMU
# log. A resumed guest panic must still fail after later successful runs.
block = script.split('# Final kernel panic guard:', 1)[1]
block = block[block.index('\nwhile '):].split('# End final kernel panic guard.', 1)[0]
with tempfile.TemporaryDirectory(prefix='ptos-panic-probes-') as directory:
    top = Path(directory)
    names = ['suite', 'cuis', 'poweroff', 'reboot', 'coldboot', 'halt', 'sysfont']
    log_paths = [top / (name + suffix) for name in names for suffix in ('.log', '.qemu')]
    collected = top / 'logs'
    collected.write_text(''.join(str(top / (name + '.log')) + '\n' for name in names))
    for panic_path in [None] + log_paths:
        for path in log_paths:
            path.write_text('Panic: resumed test fault\n' if path == panic_path else 'PASS\n')
        shell = 'logs=$1\nrecord() { printf "%s|%s" "$1" "$2"; }\n' + block
        result = subprocess.run(['sh', '-c', shell, 'probe', str(collected)],
                                check=True, text=True, capture_output=True)
        assert result.stdout == ('kernel panic|FAIL' if panic_path else ''), (panic_path, result.stdout)
print('Native final panic guard: 15 console/QEMU cases, resumed panics in every run rejected: PASS (host shell)')

block = script.split('# Native revoked-result marker.', 1)[1].split('# End native revoked-result marker.', 1)[0]
with tempfile.TemporaryDirectory(prefix='ptos-result-revocation-probes-') as directory:
    top = Path(directory)
    (top / 'obj').mkdir()
    log = top / 'suite.log'
    marker = 'native shared-VM private result Mprot/Munmap during wait and exact conservation: tested'
    for mask in range(8):
        names = ['CONF_WITH_SHARED_VM_PEXEC', 'CONF_WITH_PRIVATE_CLONE_TEST', 'CONF_WITH_MMAP']
        (top / 'obj/autoconf.h').write_text(''.join(
            '#define ' + name + ' 1\n' for i, name in enumerate(names) if mask & (1 << i)))
        for present in (False, True):
            log.write_text(marker + '\n' if present else 'PASS\n')
            shell = 'top=$1; suite_log=$2\nrecord() { printf "%s" "$1"; }\n' + block
            result = subprocess.run(['sh', '-c', shell, 'probe', str(top), str(log)],
                                    check=True, text=True, capture_output=True)
            assert result.stdout == ('native shared-Pexec result revocation' if mask == 7 and not present else '')
print('Native blocked-copyout guard: 16 feature/marker cases, missing execution rejected: PASS (host shell)')

# Run the complete production Phase C shell guard. Every required marker must
# fail independently when absent; disabled feature combinations stay optional.
start = script.index("if grep -q '^#define CONF_WITH_PREEMPTION 1'")
end = script.index('\nfi', start) + 3
block = script[start:end]
timer_markers = [
    'native timer CPU-bound PROCESS/THREAD progress without Syield: PASS',
    'native timer file-backed executable progress without Syield: PASS',
    'native timer busy USER and blocked sleeper wake: PASS',
    'native timer public-Pfork private-root GPR/FPU patterns: PASS',
    'native timer original/alternate GPR/FPU delayed restoration: PASS',
    'native timer suspended-group cancellation and exact retirement: PASS',
    'native timer independent-image/root GPR/FPU patterns: PASS',
    'native timer busy USER and mapping-owner progress/conservation: PASS',
]
with tempfile.TemporaryDirectory(prefix='ptos-timer-guards-') as directory:
    top = Path(directory)
    (top / 'obj').mkdir()
    log = top / 'suite.log'
    names = ['CONF_WITH_PREEMPTION', 'CONF_WITH_MMAP',
             'CONF_WITH_PRIVATE_CLONE_TEST', 'CONF_WITH_FORK', 'CONF_WITH_MULTITHREADING']
    for mask in range(32):
        (top / 'obj/autoconf.h').write_text(''.join(
            '#define ' + name + ' 1\n' for i, name in enumerate(names) if mask & (1 << i)))
        for missing in [None] + list(range(len(timer_markers))):
            log.write_text('\n'.join(m for i, m in enumerate(timer_markers) if i != missing) + '\n')
            shell = 'top=$1; suite_log=$2\nrecord() { echo "$1"; }\n' + block
            result = subprocess.run(['sh', '-c', shell, 'probe', str(top), str(log)],
                                    check=True, text=True, capture_output=True)
            required = bool(mask & 1) and missing is not None
            if missing == 1:
                required = required and bool(mask & 2)
            elif missing in (2, 3, 4, 5, 6, 7):
                required = required and bool(mask & 4)
                if missing in (3, 4):
                    required = required and bool(mask & 8)
                elif missing == 5:
                    required = required and bool(mask & 16)
            assert bool(result.stdout) == required, (mask, missing, result.stdout)
print('Production timer guards: 288 feature/marker cases, missing native proof fails: PASS (host shell)')

with tempfile.TemporaryDirectory(prefix='ptos-arm-exclusive-guards-') as directory:
    top = Path(directory)
    (top / 'obj').mkdir()
    log = top / 'suite.log'
    for mmap in (False, True):
        (top / 'obj/autoconf.h').write_text('#define CONF_WITH_PREEMPTION 1\n' +
                                         ('#define CONF_WITH_MMAP 1\n' if mmap else ''))
        for machine in ('virt-arm', 'raspi2b-aspace', 'pc-x86_64', 'virt-m68k-aspace'):
            for exclusive in (False, True):
                for boundary in (False, True):
                    text = timer_markers[0] + '\n' + timer_markers[1] + '\n'
                    if exclusive:
                        text += 'native timer ARM exclusive monitor ownership: PASS\n'
                    if boundary:
                        text += 'native timer ARM stack boundary ownership: PASS\n'
                    log.write_text(text)
                    shell = 'top=$1; suite_log=$2; machine=$3\nrecord() { echo "$1"; }\n' + block
                    result = subprocess.run(['sh', '-c', shell, 'probe', str(top), str(log), machine],
                                            check=True, text=True, capture_output=True)
                    required = machine in ('virt-arm', 'raspi2b-aspace') and (not exclusive or (mmap and not boundary))
                    assert bool(result.stdout) == required, (mmap, machine, exclusive, boundary, result.stdout)
print('Native ARM guards: 32 feature/verdict cases, missing exclusive/stack-boundary proof fails: PASS (host shell)')
