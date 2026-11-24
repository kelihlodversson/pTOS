/* Native A hardware IRQ -> B user execution -> frozen A return. No Syield inside
 * either register-pattern loop; kernel-owned proof rejects mere selection. */
#if defined(TEST_WITH_IRQ_RESTART)
#include "irq_restart_probe.h"
#if defined(__mc68000__)
#define IRQ_RESTART_IMAGE "CLNABI.TOS"
#else
#define IRQ_RESTART_IMAGE "RUNTESTS.TOS"
#endif
#if defined(__mc68000__)
extern long clone_irq_registers_alt(long);
#else
extern long clone_irq_registers_alt(void);
#endif
struct irq_restart_work { volatile long error, done; int pattern, private_space, refuse, check_arch; };
static int irq_restart_resources_equal(const MAPPING_LIFETIME_SNAPSHOT *a,
                                       const MAPPING_LIFETIME_SNAPSHOT *b)
{
    unsigned i;
    for(i=0;i<MLT_FIELDS;i++) if(a->value[i]!=b->value[i]) return 0;
    return 1;
}
static struct irq_restart_work irq_restart_workers[2];
static volatile unsigned long irq_restart_private_cell;
static long irq_restart_pattern(int alternate)
{
#if defined(__mc68000__)
    return alternate?clone_irq_registers_alt(has_fpu):clone_irq_registers(has_fpu);
#else
    return alternate?clone_irq_registers_alt():clone_irq_registers();
#endif
}
#if defined(__x86_64__)
static int irq_restart_arch_refusal(struct irq_restart_args *args)
{
    unsigned short saved_fs, live_fs, user_data;
    long result;
    if(pclone_trap(args,IRT_NO_CREDIT))return 0;
    __asm__ volatile("movw %%fs,%0; movw %%ss,%1; movw %1,%%fs"
                     : "=&r"(saved_fs), "=&r"(user_data) :: "memory");
    result=clone_irq_registers();
    __asm__ volatile("movw %%fs,%0; movw %1,%%fs" : "=&r"(live_fs) : "r"(saved_fs) : "memory");
    return pclone_trap(0,IRT_FINISH)==-32L && !result && user_data && live_fs==user_data;
}
#elif defined(__arm__)
extern long clone_irq_thumb(void);
extern char clone_irq_thumb_begin[],clone_irq_thumb_end[];
static int irq_restart_arch_refusal(struct irq_restart_args *args)
{
    unsigned long start,elapsed;
    long result;
    irq_restart_prepare_args(args,(uintptr_t)clone_irq_thumb_begin,(uintptr_t)clone_irq_thumb_end);
    if(pclone_trap(args,IRT_NO_CREDIT))return 0;
    start=(unsigned long)Ssystem(S_GETLVAL,0x4baL,0L);
    result=clone_irq_thumb();
    elapsed=(unsigned long)Ssystem(S_GETLVAL,0x4baL,0L)-start;
    /* Thumb cannot produce a resumable capture. Time advances across the
     * stackless, trap-free Thumb loop and the normal IRQ return preserves it. */
    return pclone_trap(0,IRT_FINISH)==-36L && !result && elapsed>=2;
}

static volatile unsigned long irq_exclusive_word[1024] __attribute__((aligned(4096)));
static volatile unsigned long irq_exclusive_stage[1024] __attribute__((aligned(4096)));
extern long clone_irq_exclusive_a(volatile unsigned long *,volatile unsigned long *);
extern long clone_irq_exclusive_b(volatile unsigned long *,volatile unsigned long *);
extern char clone_irq_exclusive_a_begin[],clone_irq_exclusive_a_end[];
extern char clone_irq_exclusive_b_begin[],clone_irq_exclusive_b_end[];

static void irq_restart_exclusive_worker(void *argument)
{
    int peer=(uintptr_t)argument!=0;
    struct irq_restart_args args;
    long result;
    unsigned spin;
    irq_restart_prepare_args(&args,
        (uintptr_t)(peer?clone_irq_exclusive_b_begin:clone_irq_exclusive_a_begin),
        (uintptr_t)(peer?clone_irq_exclusive_b_end:clone_irq_exclusive_a_end));
    if(pclone_trap(&args,IRT_BEGIN)) { Pterm(87);return; }
    for(spin=0;peer && irq_exclusive_stage[0]!=1 && spin<128;spin++) (void)Syield();
    if(spin==128) { Pterm(87);return; }
    result=(peer?clone_irq_exclusive_b:clone_irq_exclusive_a)(irq_exclusive_word,irq_exclusive_stage);
    Pterm(result || pclone_trap(0,IRT_FINISH) ? 87:0);
}

static int irq_restart_exclusive_ownership(void)
{
    int peer,okay=1;
    long tid[2];
    irq_exclusive_word[0]=irq_exclusive_stage[0]=0;
    for(peer=0;peer<2;peer++) {
        struct ptos_clone_args args={0};
        args.flags=CLONE_VM|CLONE_SAME_PROCESS;
        args.entry=irq_restart_exclusive_worker;
        args.argument=(void *)(uintptr_t)peer;
        args.stack_base=runnable_stacks[peer];args.stack_size=RUNNABLE_STACK_SIZE;
        tid[peer]=pclone_trap(&args,sizeof(args));
    }
    for(peer=0;peer<2;peer++) {
        int32_t status=-1;
        if(tid[peer]<=0 || join_tid(tid[peer],&status) || status)okay=0;
    }
    return okay && irq_exclusive_word[0]==7 && irq_exclusive_stage[0]==3;
}
#endif
static void irq_restart_entry(void *argument)
{
    volatile struct irq_restart_work *w=argument;
    struct irq_restart_args args;
    long value;
    uintptr_t code=w->pattern?(uintptr_t)clone_irq_window_alt_begin:(uintptr_t)clone_irq_window_begin;
    uintptr_t end=w->pattern?(uintptr_t)clone_irq_window_alt_end:(uintptr_t)clone_irq_window_end;
    unsigned long private_pattern=w->pattern?0x72407240UL:0x71307130UL;
    MAPPING_LIFETIME_SNAPSHOT before,after;
    irq_restart_prepare_args(&args,code,end);
    /* Warm both copyout destinations before taking the conservation baseline
     * (a private child may have to resolve its own stack COW pages). */
    if(mlt_snapshot(&before) || mlt_snapshot(&after) || mlt_snapshot(&before)) w->error|=128;
    /* Both rollback prefixes must free every private owner before retry. */
    if (pclone_trap(&args,IRT_FAIL_CODE)!=-39L) w->error|=1;
    if(mlt_snapshot(&after) || !irq_restart_resources_equal(&before,&after))w->error|=128;
    if (pclone_trap(&args,IRT_FAIL_STACK)!=-39L) w->error|=2;
    if(mlt_snapshot(&after) || !irq_restart_resources_equal(&before,&after))w->error|=128;
    if(w->private_space) irq_restart_private_cell=private_pattern;
    /* The first two cases must not be accepted as a delayed return. */
    if(pclone_trap(0,IRT_ABORT)!=-36L) w->error|=256;
    value=pclone_trap(&args,IRT_BEGIN);
    if(value || pclone_trap(&args,IRT_BEGIN)!=-36L || pclone_trap(0,IRT_ABORT) ||
       pclone_trap(0,IRT_ABORT)!=-36L) w->error|=256;
    value=pclone_trap(&args,IRT_CAPTURE_ONLY);
    if(value || irq_restart_pattern(w->pattern) || pclone_trap(0,IRT_FINISH)!=1) w->error|=32;
    if(w->refuse) {
        /* EINVFN requires a real IRQ in this window and no handoff. EAGAIN
         * denotes a handoff with credit suppressed, EACCDN no capture. */
        value=pclone_trap(&args,IRT_NO_CREDIT);
        if(value || irq_restart_pattern(w->pattern) || pclone_trap(0,IRT_FINISH)!=-32L)w->error|=512;
        goto done;
    }
    value=pclone_trap(&args,IRT_BEGIN);
    if (value) {
        char message[96];
        sprintf(message,"IRQ restart prepare failed %ld code=%lx/%lu stack=%lx\r\n",value,
                (unsigned long)args.code,(unsigned long)args.code_bytes,(unsigned long)args.stack);
        conws(message); w->error|=4;
    } else {
        /* Both peers prepare before either enters its bounded IRQ window.
         * This is a setup barrier; no yield occurs in the register probe. */
        (void)Syield();
        if (irq_restart_pattern(w->pattern)) w->error|=8;
        if(w->private_space && irq_restart_private_cell!=private_pattern) w->error|=64;
        value=pclone_trap(0,IRT_FINISH);
        if(value) {
            char message[64];sprintf(message,"IRQ restart finish failed %ld\r\n",value);conws(message);
            w->error|=16;
        }
    }
    /* Force the no-credit branch even if B executes. Native register/FPU
     * checks must still pass: restoration must never depend on proof credit.
     * FINISH refuses this test unless an actual root handoff occurred. */
    value=pclone_trap(&args,IRT_NO_CREDIT);
    if(value) w->error|=512;
    else {
        (void)Syield();
        if(irq_restart_pattern(w->pattern) || pclone_trap(0,IRT_FINISH)!=-326L)w->error|=512;
    }
#if defined(__x86_64__) || defined(__arm__)
    if(w->check_arch && !irq_restart_arch_refusal(&args))w->error|=1024;
#endif
done:
    if(w->error) {
        char message[80];
        sprintf(message,"IRQ restart peer pattern=%d private=%d errors=%lx\r\n",
                w->pattern,w->private_space,(unsigned long)w->error);
        conws(message);
    }
    w->done=1;
    Pterm(w->error?87:0);
}
#if defined(TEST_WITH_MULTITHREADING)
enum irq_restart_exit { ARMED_EXIT, ACKED_EXIT, SUSPENDED_CANCEL, ARMED_FAULT, ACKED_FAULT };
static volatile int irq_restart_exit_mode,irq_restart_cancel_ready;
static volatile int irq_restart_cancel_seen,irq_restart_cancel_user;
static void irq_restart_cancel_peer(void *unused)
{
    int spin;
    (void)unused;
    for(spin=0;spin<128 && !irq_restart_cancel_ready;spin++) (void)Syield();
    if(!irq_restart_cancel_ready) { Pterm(87);return; }
    irq_restart_cancel_seen=1;
    if(irq_restart_exit_mode==SUSPENDED_CANCEL) {
        (void)term_group(61);
        Pterm(87);return;
    }
    Pterm(0);
}
static void irq_restart_exit_entry(void *unused)
{
    struct irq_restart_args args;
    struct ptos_clone_args peer={0};
    int mode=irq_restart_exit_mode;
    (void)unused;
    irq_restart_prepare_args(&args,(uintptr_t)clone_irq_window_begin,(uintptr_t)clone_irq_window_end);
    if(mode!=ARMED_EXIT && mode!=ARMED_FAULT) {
        peer.flags=CLONE_VM|CLONE_SAME_PROCESS;peer.entry=irq_restart_cancel_peer;
        peer.stack_base=runnable_stacks[1];peer.stack_size=RUNNABLE_STACK_SIZE;
        if(pclone_trap(&peer,sizeof(peer))<=0) { Pterm(87);return; }
    }
    if(pclone_trap(&args,IRT_BEGIN)) { Pterm(87);return; }
    if(mode==ARMED_EXIT) { Pterm(62);return; }
    if(mode==ARMED_FAULT) {
        *(volatile unsigned long *)0x80001000UL=1;
        Pterm(87);return;
    }
    irq_restart_cancel_ready=1;
    if(irq_restart_pattern(0)) { Pterm(87);return; }
    irq_restart_cancel_user=1;
    /* Mode 2 must stop via existing cancellation before returning to USER.
     * Mode 1 verifies ACKED ownership remains attached until off-stack exit. */
    if(mode==SUSPENDED_CANCEL || pclone_trap(0,IRT_QUERY)!=IRT_ACKED || !irq_restart_cancel_seen)
        { Pterm(87);return; }
    if(mode==ACKED_FAULT) {
        *(volatile unsigned long *)0x80001000UL=1;
        Pterm(87);return;
    }
    Pterm(63);
}
static int irq_restart_test_retirement(void)
{
    static const unsigned expected[]={62,63,61,0x0b00,0x0b00};
    int mode,good=1;
    for(mode=ARMED_EXIT;mode<=ACKED_FAULT;mode++) {
        struct ptos_clone_args args={0};
        MAPPING_LIFETIME_SNAPSHOT before,after;
        int32_t pid=0;
        long child,status;
        memset(&before,0,sizeof(before));memset(&after,0,sizeof(after));
        irq_restart_exit_mode=mode;
        irq_restart_cancel_seen=irq_restart_cancel_ready=irq_restart_cancel_user=0;
        {
            int stack;unsigned offset;
            /* The preceding private-root cases made these driver-owned pages
             * COW again. Resolve their writable backing before measuring an
             * exit; this is unrelated to retained IRQ ownership. */
            for(stack=0;stack<2;stack++) for(offset=0;offset<RUNNABLE_STACK_SIZE;offset+=4096) {
                volatile unsigned char *p=runnable_stacks[stack]+offset;
                unsigned char value=*p;*p=value;
            }
        }
        if(mlt_snapshot(&before)) good=0;
        args.flags=CLONE_VM|CLONE_COPY_STANDARD_HANDLES;
        args.entry=irq_restart_exit_entry;args.stack_base=runnable_stacks[0];
        args.stack_size=RUNNABLE_STACK_SIZE;args.child_pid=&pid;
        child=pclone_trap(&args,sizeof(args));
        status=child>0?Pwaitpid(pid,0,0):-1;
        if(child<=0 || (status>>16)!=pid || (status&65535)!=expected[mode] ||
           Pwaitpid(pid,1,0)!=-33L ||
           ((mode!=ARMED_EXIT && mode!=ARMED_FAULT) && !irq_restart_cancel_seen) ||
           (mode==SUSPENDED_CANCEL && irq_restart_cancel_user)) {
            printf("IRQ retirement mode %d unexpected wait status %lx\r\n",mode,(unsigned long)status);
            good=0;
        }
        if(mlt_snapshot(&after) || !irq_restart_resources_equal(&before,&after)) {
            char message[96];sprintf(message,"IRQ retirement mode=%d child=%ld pid=%ld status=%lx seen=%d user=%d\r\n",
                mode,child,(long)pid,(unsigned long)status,irq_restart_cancel_seen,irq_restart_cancel_user);conws(message);
            mlt_print("IRQ retirement before",&before);mlt_print("IRQ retirement after",&after);
            good=0;
        }
    }
    if(good) conws("native retained IRQ armed/acknowledged exit and suspended group cancellation: PASS\r\n");
    return good;
}
#endif
static int irq_restart_peers(int mode, int cycles, int refuse)
{
    int cycle,i,good=1;
    for(cycle=0;cycle<cycles;cycle++) {
        int32_t pid[2]={0,0},tid[2]={0,0},status;
        irq_restart_private_cell=0;
        for(i=0;i<2;i++) {
            struct ptos_clone_args args={0};
            long created;
            irq_restart_workers[i].error=irq_restart_workers[i].done=0;
            irq_restart_workers[i].pattern=i;
            irq_restart_workers[i].private_space=mode==2;
            irq_restart_workers[i].refuse=refuse;
            irq_restart_workers[i].check_arch=(mode==0 && !cycle && !i);
            args.flags=CLONE_VM|(mode==1?CLONE_SAME_PROCESS:CLONE_COPY_STANDARD_HANDLES);
            args.entry=irq_restart_entry;args.argument=&irq_restart_workers[i];
            args.stack_base=runnable_stacks[i];args.stack_size=RUNNABLE_STACK_SIZE;
            /* The private test transport intentionally disallows copyout
             * pointers into a different root; the result itself is the PID. */
            if(mode!=2) {args.child_pid=&pid[i];args.child_tid=&tid[i];}
            created=pclone_trap(&args,(mode==2?0xc8920000UL:0)|sizeof(args));
            if(mode==2 && created>0)pid[i]=created;
            if(created<=0) {
                char message[80];
                sprintf(message,"IRQ restart peer create failed mode=%d peer=%d error=%ld\r\n",mode,i,created);
                conws(message);good=0;
            }
            ptest_assert_msg(created>0,"publish interrupted-return peer without dispatch");
        }
        for(i=0;i<2;i++) {
            if(!pid[i]) {good=0;continue;}
            if(mode==1) {
#if defined(TEST_WITH_MULTITHREADING)
                long joined;
                status=-99;
                joined=join_tid(tid[i],&status);
                if(joined || status)good=0;
                ptest_assert_msg(joined==0 && status==0,"deferred IRQ sibling returns through authentic user epilogue");
#else
                int spin;
                for(spin=0;spin<128 && !irq_restart_workers[i].done;spin++) (void)Syield();
#endif
            } else {
                long result=Pwaitpid(pid[i],0,0);
                if((result>>16)!=pid[i] || (result&65535))good=0;
                ptest_assert_msg((result>>16)==pid[i] && !(result&65535),"deferred IRQ PROCESS returns through authentic user epilogue");
            }
            if(mode!=2 && (!irq_restart_workers[i].done || irq_restart_workers[i].error))good=0;
            ptest_assert_msg(mode==2 || (irq_restart_workers[i].done && !irq_restart_workers[i].error),
                             refuse?"unsupported profile returns normally from a genuine IRQ":
                             "actual B USER execution observed between A freeze and owned register return");
        }
        if(mode==2) {
            if(irq_restart_private_cell)good=0;
            ptest_assert_msg(!irq_restart_private_cell,"private peers preserve parent and independent same-VA data");
        }
    }
    return good;
}

#ifndef TEST_M68K_ROM_WINDOW
static void irq_restart_driver(void *unused)
{
    int mode,good=1;
    (void)unused;
    conws("IRQ restart owned driver entered\r\n");
    for(mode=0;mode<3;mode++) if(!irq_restart_peers(mode,3,0))good=0;
#if defined(__arm__)
    if(!irq_restart_exclusive_ownership())good=0;
#endif
#if defined(TEST_WITH_MULTITHREADING)
    if(!irq_restart_test_retirement())good=0;
#endif
    Pterm(good && !ptest_failures()?0:87);
}
#endif

#if defined(TEST_WITH_MULTITHREADING) && !defined(TEST_M68K_ROM_WINDOW)
#define IRQ_ORPHAN_READY "IRQWAIT.ON"
#define IRQ_ORPHAN_DONE "IRQDONE.OUT"

static int irq_restart_file(const char *name, int create)
{
    long fd=create?Fcreate(name,0):Fopen(name,0);
    return fd>=0 && Fclose((short)fd)==0;
}

/* The exact waiter's sibling is the only runnable USER peer when A enters
 * its IRQ window. It requests group exit, then the child must finish B2 and
 * run independently of the released parent claim. */
static void irq_restart_orphan_killer(void *unused)
{
    unsigned spin;
    (void)unused;
    for(spin=0;spin<1024;spin++) {
        if(irq_restart_file(IRQ_ORPHAN_READY,0)) {
            if(Pwaitpid(-1,1,0)==-36L)(void)term_group(61);
            break;
        }
        (void)Syield();
    }
    Pterm(87);
}

static void irq_restart_orphan_waiter(void *unused)
{
    int32_t pid=0,tid=0;
    (void)unused;
    if(launch_entry(irq_restart_orphan_killer,CLONE_VM|CLONE_SAME_PROCESS,
                    runnable_stacks[1],&pid,&tid)>0)
        (void)Pexec(0,IRQ_RESTART_IMAGE,"\007--irq O",0);
    Pterm(87); /* the exact wait must end through cancellation */
}

static void irq_restart_orphan_worker(void *unused)
{
    struct irq_restart_args args;
    long parent=Pgetppid();
    unsigned spin;
    (void)unused;
    irq_restart_prepare_args(&args,(uintptr_t)clone_irq_window_begin,(uintptr_t)clone_irq_window_end);
    if(parent<=0 || pclone_trap(&args,IRT_BEGIN) || !irq_restart_file(IRQ_ORPHAN_READY,1) ||
       Pgetppid()!=parent || irq_restart_pattern(0) || pclone_trap(0,IRT_FINISH)) {
        Pterm(87);return;
    }
    for(spin=0;spin<1024 && Pgetppid()==parent;spin++) (void)Syield();
    Pterm(Pgetppid()==0?0:87);
}

static long irq_restart_orphan_image(void)
{
    struct ptos_clone_args args={0};
    long memory=Malloc(RUNNABLE_STACK_SIZE),tid;
    int32_t status=-1;
    if(memory<=0)return 87;
    args.flags=CLONE_VM|CLONE_SAME_PROCESS;
    args.entry=irq_restart_orphan_worker;
    args.stack_base=(void *)memory;
    args.stack_size=RUNNABLE_STACK_SIZE;
    tid=pclone_trap(&args,sizeof(args));
    if(tid<=0 || join_tid(tid,&status) || status || Mfree((void *)memory))return 87;
    return irq_restart_file(IRQ_ORPHAN_DONE,1)?0:87;
}

static void irq_restart_test_orphan(void)
{
    struct ptos_clone_args args={0};
    MAPPING_LIFETIME_SNAPSHOT before,after;
    long child,result;
    int32_t pid=0;
    unsigned spin;
    int completed=0;
    (void)Fdelete(IRQ_ORPHAN_READY);
    (void)Fdelete(IRQ_ORPHAN_DONE);
    if(mlt_snapshot(&before)) { ptest_assert_msg(0,"snapshot before B2 orphan");return; }
    args.flags=CLONE_VM|CLONE_COPY_CWD|CLONE_COPY_STANDARD_HANDLES;
    args.entry=irq_restart_orphan_waiter;
    args.stack_base=runnable_stacks[0];
    args.stack_size=RUNNABLE_STACK_SIZE;
    args.child_pid=&pid;
    child=pclone_trap(&args,sizeof(args));
    result=child>0?Pwaitpid(pid,0,0):-1;
    for(spin=0;spin<1024 && !completed;spin++) {
        completed=irq_restart_file(IRQ_ORPHAN_DONE,0);
        if(!completed)(void)Syield();
    }
    ptest_assert_msg(child>0 && (result>>16)==pid && (result&65535)==61 &&
                     Pwaitpid(pid,1,0)==-33L && completed,
                     "cancel exact waiter while its child retains an IRQ and verify orphan completion");
    ptest_assert_msg(Fdelete(IRQ_ORPHAN_READY)==0 && Fdelete(IRQ_ORPHAN_DONE)==0,"remove B2 orphan receipts");
    ptest_assert_msg(!mlt_snapshot(&after) && irq_restart_resources_equal(&before,&after),
                     "B2 orphan, IRQ and exact claim resources return to baseline");
    if(!ptest_failures())conws("native retained IRQ exact-wait cancellation and orphan continuation: PASS\r\n");
}
#endif

/* An independent loader PROCESS with an exact synchronous parent wait.
 * Its two siblings use heap stacks so the regular B2 stack lease applies. */
long pclone_run_irq_image(int role);
long pclone_run_irq_image(int role)
{
    int i,ok=1;
#if defined(__mc68000__)
    if(Ssystem(S_GETCOOKIE,0x5f465055L,(long)&has_fpu)!=0)has_fpu=0;
#endif
#if defined(TEST_WITH_MULTITHREADING) && !defined(TEST_M68K_ROM_WINDOW)
    if(role=='O')return irq_restart_orphan_image();
#endif
    for(i=0;i<2;i++) {
        long memory=Malloc(RUNNABLE_STACK_SIZE);
        if(memory<=0) { ok=0;break; }
        runnable_stacks[i]=(void *)memory;
    }
    if(ok)ok=irq_restart_peers(role=='R'?0:1,1,role=='N');
#if defined(TEST_M68K_ROM_WINDOW) && defined(TEST_WITH_MULTITHREADING)
    if(ok && role=='R')ok=irq_restart_test_retirement();
#endif
    while(i)if(Mfree(runnable_stacks[--i]))ok=0;
    return ok && !ptest_failures()?0x713b:87;
}

static void irq_restart_test_exact_child(void)
{
    int32_t pid=0,tid=0,status=-1;
    long result,excluded;
    ptest_assert_msg(Pwaitpid(-1,1,0)==-33L,"isolated exact-child probe starts without children");
    anchored_thief_stop=anchored_thief_taken=anchored_thief_polls=0;
    if(launch_entry(anchored_thief,CLONE_VM|CLONE_SAME_PROCESS,
                    runnable_stacks[2],&pid,&tid)<=0) {
        ptest_assert_msg(0,"start competing B2 exact-child waiter");
        return;
    }
    while(!anchored_thief_polls)(void)Syield();
    excluded=anchored_thief_excluded;
    /* On m68k the ELF loader owns the text leaves. Legacy PRG text requires
     * the private-image authority separately tested by irq_restart_driver. */
    result=Pexec(0,IRQ_RESTART_IMAGE,"\007--irq A",0);
    anchored_thief_stop=1;
    ptest_assert_msg(join_tid(tid,&status)==0 && status==0,"join B2 competing waiter");
    ptest_assert_msg(result==0x713b && anchored_thief_excluded>excluded && !anchored_thief_taken &&
                     Pwaitpid(-1,1,0)==-33L,"retained IRQ coexists with exclusive full-status Pexec(0) wait");
    if(!ptest_failures())conws("native retained IRQ with managed exact-child wait: PASS\r\n");
}

static void test_irq_restart(void)
{
#if defined(TEST_M68K_ROM_WINDOW)
    long cpu=0,status;
    int supported=Ssystem(S_GETCOOKIE,0x5f435055L,(long)&cpu)==0 && cpu==40;
#if defined(TEST_WITH_KERNEL_MFASTCALL)
    supported=0;
#endif
    if(!supported) {
        status=Pexec(0,"CLNABI.TOS","\007--irq N",0);
        ptest_assert_msg(status==0x713b,"unsupported B2 profile rejects a real IRQ without switching");
        if(status==0x713b)conws("native retained IRQ unsupported CPU or ABI ordinary return: PASS\r\n");
        return;
    }
    irq_restart_test_exact_child();
    /* The ROM-window backend declines private cloning. An ELF loader child
     * supplies owned executable pages through its ordinary root instead. */
    status=Pexec(0,"CLNABI.TOS","\007--irq R",0);
    ptest_assert_msg(status==0x713b,"ROM-window ELF IRQ source and its PROCESS peers complete");
    if(status!=0x713b)return;
#else
    struct ptos_clone_args args={0};
    long child,status;
    irq_restart_test_exact_child();
#if defined(TEST_WITH_MULTITHREADING)
    irq_restart_test_orphan();
#endif
    /* In particular, legacy m68k PRG text is borrowed procmem, not private
     * code authority. Obtain the proof source through the existing owned
     * private-image transaction rather than weakening IRQ lease admission. */
    args.flags=CLONE_VM|CLONE_COPY_STANDARD_HANDLES;
    args.entry=irq_restart_driver;
    args.stack_base=runnable_stacks[2];args.stack_size=RUNNABLE_STACK_SIZE;
    child=pclone_trap(&args,0xc8920000UL|sizeof(args));
    ptest_assert_msg(child>0,"prepare independently owned IRQ restart source image");
    if(child<=0)return;
    status=Pwaitpid(child,0,0);
    { char message[80];sprintf(message,"IRQ restart source child=%ld status=%lx\r\n",child,(unsigned long)status);conws(message); }
    ptest_assert_msg((status>>16)==child && !(status&65535),"owned IRQ restart source and all peers complete");
    /* The owned private driver's buffered log is private as well. Publish its
     * checked verdict in the original harness log (Amiberry reads the disk). */
    if((status>>16)!=child || (status&65535))return;
#endif
    conws("native retained IRQ capture-only and uncredited root return: PASS\r\n");
    conws("native interrupted USER A IRQ / B USER execution / saved A restart: PASS\r\n");
#if defined(__x86_64__)
    conws("native retained IRQ x32 foreign FS selector returns normally: PASS\r\n");
#elif defined(__arm__)
    conws("native retained IRQ ARM Thumb refusal and exclusive monitor ownership: PASS\r\n");
#endif
#if defined(TEST_WITH_MULTITHREADING)
    conws("native retained IRQ armed/acknowledged exit and suspended group cancellation: PASS\r\n");
    conws("native retained IRQ armed and acknowledged user faults: PASS\r\n");
#endif
}
#endif
