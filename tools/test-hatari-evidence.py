#!/usr/bin/env python3
"""Fail-closed TT timer verdict consumption; host I/O model, no CPU proof."""
import contextlib
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch

source = Path(__file__).with_name('run-hatari-tests.py')
spec = importlib.util.spec_from_file_location('hatari_evidence', source)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
markers = (
    'native m68k nonzero FPIAR: PASS',
    'native timer CPU-bound PROCESS/THREAD progress without Syield: PASS',
    'native timer file-backed executable progress without Syield: PASS',
    'native timer busy USER and blocked sleeper wake: PASS',
    'native timer busy USER and mapping-owner progress/conservation: PASS',
    'native timer independent-image/root GPR/FPU patterns: PASS',
    'native timer original/alternate GPR/FPU delayed restoration: PASS',
    'native timer suspended-group cancellation and exact retirement: PASS',
)
summary = '--- Summary: 29 passed, 0 failed, 29 total ---\n'
passed = '\n'.join(markers) + '\n' + summary

with tempfile.TemporaryDirectory(prefix='ptos-tt-evidence-') as directory:
    root = Path(directory)
    disk = root / 'disk.img'
    disk.write_bytes(b'host model')
    arguments = ['run-hatari-tests.py', '--tos', str(root / 'rom.img'),
                 '--disk', str(disk), '--mode', 'tt040a',
                 '--require-native-preemption', '--halt', '0x80001000',
                 '--panic', '0x80002000']

    def check(payload, code, expected, raw='', stale=False):
        state = {'log': passed, 'cleared': False}

        def clear(_):
            state['log'] = ''
            state['cleared'] = True

        class Child:
            def __init__(self, cmd, env, stdout, stderr):
                assert state['cleared'], 'old verdicts must be removed before boot'
                assert '--parse' in cmd, 'halt/panic observation is mandatory'
                if not stale:
                    state['log'] = payload
                stdout.write(raw)
                stdout.flush()
                self.returncode = code

            def poll(self):
                return self.returncode

            def wait(self):
                return self.returncode

            def kill(self):
                self.returncode = -9

        with patch.object(sys, 'argv', arguments), \
             patch.object(runner, 'clear_test_log', clear), \
             patch.object(runner, 'read_test_log', lambda _: state['log']), \
             patch.object(runner.subprocess, 'Popen', Child), \
             contextlib.redirect_stdout(io.StringIO()), \
             contextlib.redirect_stderr(io.StringIO()):
            assert runner.main() == expected, (code, expected, payload, raw)

    check(passed, runner.HALT_EXIT, 0)
    for missing in markers:
        check(passed.replace(missing, ''), runner.HALT_EXIT, 1)
    check('', runner.HALT_EXIT, 1, raw=passed)  # console cannot replace the file
    check(passed, runner.HALT_EXIT, 1, stale=True)
    check(passed, 0, 1)                      # ordinary exit is not a halt ACK
    check(passed, -11, 1)                    # crash after writing a summary
    check(passed, runner.PANIC_EXIT, 1)      # panic after writing a summary
    check(passed, runner.HALT_EXIT, 1, raw='Panic: final retirement failed\n')
    check(passed.replace(summary, ''), runner.HALT_EXIT, 1)
    check(passed.replace('0 failed', '1 failed'), runner.HALT_EXIT, 1)

print('TT timer evidence: fresh log, exact halt ACK, missing verdicts, crashes and final panics: PASS (host I/O model)')
