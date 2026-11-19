#!/usr/bin/env python3
"""Execute the native timer fixtures with deterministic host interleavings.

These are fixture/compilation models, not native IRQ or scheduler evidence.
The x86 fault model injects another ready writer at the actual RMW boundary;
the wake model runs the actual verdict after early, overlapping or late wake.
"""
from pathlib import Path
import os
import platform
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
preempt = (root / 'tests/pclone/preempt.h').read_text()
pclone = (root / 'tests/pclone/pclone.c').read_text()


def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'


cc = shlex.split(os.environ.get('HOSTCC', 'cc'))
failed = []

def check(label, okay, message):
    if not okay:
        failed.append(label)
        print(label + ': FAIL\n' + message)

with tempfile.TemporaryDirectory(prefix='ptos-timer-review-') as directory:
    top = Path(directory)

    def run(name, source):
        path, executable = top / (name + '.c'), top / name
        path.write_text(source)
        built = subprocess.run(cc + [
            '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
            '-iquote', str(root / 'tests/pclone'), str(path), '-o', str(executable),
        ], capture_output=True, text=True, timeout=30)
        if built.returncode:
            return False, built.stderr[-1400:]
        result = subprocess.run([str(executable)], capture_output=True,
                                text=True, timeout=10)
        return result.returncode == 0, result.stderr[-1400:]

    ready = function(pclone, 'static void group_ready_bit(')
    atomic = '''#define _GNU_SOURCE
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#define TEST_WITH_NATIVE_PREEMPT 1
static struct {volatile long ready;} *shared;
#define group (*shared)
''' + ready + '''
static volatile sig_atomic_t faults;
static void inject(int sig,siginfo_t *info,void *context)
{
 (void)sig;(void)context;
 if(faults++ || info->si_addr!=(void *)&shared->ready)_exit(90);
 if(mprotect(shared,4096,PROT_READ|PROT_WRITE))_exit(91);
 group_ready_bit(2);
}
int main(void)
{
 struct sigaction sa={0};
 shared=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 if(shared==MAP_FAILED)return 92;
 sa.sa_sigaction=inject;sa.sa_flags=SA_SIGINFO;sigemptyset(&sa.sa_mask);
 if(sigaction(SIGSEGV,&sa,0)||mprotect(shared,4096,PROT_READ))return 93;
 group_ready_bit(1);
 if(shared->ready!=3||faults!=1){fprintf(stderr,"interrupted readiness update lost a bit: ready=%ld faults=%d\\n",shared->ready,faults);return 1;}
 return munmap(shared,4096)!=0;
}
'''
    ready_checked = (platform.system() == 'Linux' and
                     platform.machine().lower() in ('x86_64', 'amd64'))
    if ready_checked:
        okay, message = run('ready', atomic)
        check('interrupted x32 readiness writer', okay, message)
    else:
        print('x32 readiness fault model: SKIP (requires Linux x86-64 host)')

    mapping = (root / 'tests/pclone/mapping_lifetime.h').read_text()
    guard = next(line for line in mapping.splitlines() if line.startswith('#if '))
    declarations = (guard + '\n#include "../../include/mapping_lifetime_test.h"\n' +
                    function(mapping, 'static long mlt_snapshot(') +
                    function(mapping, 'static void mlt_print(') + '#endif\n')
    prefix = (preempt[:preempt.index('static volatile unsigned long preempt_turn')] +
              '\n#endif\n')
    for private, mmap in ((0, 0), (0, 1), (1, 0), (1, 1)):
        flags = '#define TEST_WITH_NATIVE_PREEMPT 1\n#define TEST_WITH_NATIVE_PREEMPT_PROBES 1\n'
        if private:
            flags += '#define TEST_WITH_PRIVATE_CLONE_TEST 1\n'
        if mmap:
            flags += '#define TEST_WITH_MMAP 1\n'
        code = '#include <stdio.h>\n' + flags + '''
static long pclone_trap(const void *p,unsigned long n){(void)p;(void)n;return 0;}
static void conws(const char *s){(void)s;}
''' + declarations + prefix + '''
int main(void){MAPPING_LIFETIME_SNAPSHOT s={0};mlt_print("",&s);return mlt_snapshot(&s)!=0;}
'''
        okay, message = run('mapping%d%d' % (private, mmap), code)
        check('timer helper private=%d mmap=%d' % (private, mmap), okay, message)

    spin = function(preempt, 'static void preempt_wait_spinner(')
    assert spin.count('1000000000UL') == 1, 'keep the modeled window bounded'
    spin = spin.replace('1000000000UL', '8UL')
    sleeper = function(preempt, 'static void preempt_wait_sleeper(')
    probe = function(preempt, 'static void preempt_blocked_wakeup(')
    wake = '''#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CLONE_VM 1
#define CLONE_SAME_PROCESS 2
static volatile unsigned long preempt_wait_started,preempt_wait_woke,preempt_wait_progress;
static unsigned mode,reads,failures,passes,launches;
static unsigned char stacks[2][8];
static unsigned char *runnable_stacks[2]={stacks[0],stacks[1]};
static long launch_entry(void (*entry)(void *),unsigned flags,void *stack,int32_t *pid,int32_t *tid);
static long join_tid(int32_t tid,void *status);
static long pclone_trap(const void *arg,unsigned long operation){(void)arg;(void)operation;return mode==2?-1:0;}
static void ptest_assert_msg(int yes,const char *message){(void)message;if(!yes)failures++;}
static void conws(const char *message){if(strstr(message,"PASS"))passes++;}
static void preempt_wait_sleeper(void *);
static unsigned long observe_wake(void){if(++reads==3 && (mode==1||mode==2))preempt_wait_sleeper(0);return preempt_wait_woke;}
#define preempt_wait_woke observe_wake()
''' + spin + '\n#undef preempt_wait_woke\n' + sleeper + probe + '''
static long launch_entry(void (*entry)(void *),unsigned flags,void *stack,int32_t *pid,int32_t *tid)
{(void)entry;(void)flags;(void)stack;*pid=1;*tid=++launches;return *tid;}
static long join_tid(int32_t tid,void *status)
{
 (void)status;
 if(tid==1){if(mode==3)preempt_wait_sleeper(0);preempt_wait_spinner(0);}
 else if(!preempt_wait_woke)preempt_wait_sleeper(0);
 return 0;
}
int main(void)
{
 for(mode=0;mode<4;mode++){
  reads=failures=passes=launches=0;preempt_blocked_wakeup();
  if((mode==1 &&(failures||passes!=1))||(mode!=1&&(!failures||passes))){fprintf(stderr,"wake timing mode %u falsely accepted/rejected: failures=%u passes=%u progress=%lu\\n",mode,failures,passes,preempt_wait_progress);return 1;}
 }
 return 0;
}
'''
    okay, message = run('wake', wake)
    check('busy-window wake verdict', okay, message)

if failed:
    raise SystemExit('Native timer fixture regressions: ' + ', '.join(failed))
print('Native timer fixtures: four helper profiles and four wake orderings: PASS (host models)')
if ready_checked:
    print('Native timer readiness: interrupted x32 read/modify/write retains both bits: PASS (host fault model)')
