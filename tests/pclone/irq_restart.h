/* Native A hardware IRQ -> B user execution -> frozen A return. No Syield inside
 * either register-pattern loop; kernel-owned proof rejects mere selection. */
#if defined(TEST_WITH_IRQ_RESTART)
#define IRT_BEGIN 0xc713b200UL
#define IRT_FINISH 0xc713b201UL
#define IRT_ABORT 0xc713b202UL
#define IRT_QUERY 0xc713b204UL
#define IRT_NO_CREDIT 0xc713b205UL
#define IRT_CAPTURE_ONLY 0xc713b203UL
#define IRT_FAIL_CODE 0xc713b211UL
#define IRT_FAIL_STACK 0xc713b212UL
extern char clone_irq_registers_end[];
extern char clone_irq_registers_alt_end[];
#if defined(__mc68000__)
extern long clone_irq_registers_alt(long);
#else
extern long clone_irq_registers_alt(void);
#endif
struct irq_restart_work { volatile long error, done; int pattern, private_space; };
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
static int irq_restart_resources_equal(const MAPPING_LIFETIME_SNAPSHOT *a,
                                       const MAPPING_LIFETIME_SNAPSHOT *b)
{
    unsigned i;
    for(i=0;i<MLT_FIELDS;i++) if(a->value[i]!=b->value[i]) return 0;
    return 1;
}
#endif
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
static void irq_restart_entry(void *argument)
{
    volatile struct irq_restart_work *w=argument;
    struct { uint64_t code, code_bytes, stack, stack_bytes; } args;
    unsigned long page=(unsigned long)(uintptr_t)&args & ~4095UL;
    long value;
    uintptr_t code=w->pattern?(uintptr_t)clone_irq_registers_alt:(uintptr_t)clone_irq_registers;
    uintptr_t end=w->pattern?(uintptr_t)clone_irq_registers_alt_end:(uintptr_t)clone_irq_registers_end;
    unsigned long private_pattern=w->pattern?0x72407240UL:0x71307130UL;
    unsigned long touch;
    args.code=(uint64_t)code;
    args.code_bytes=(uint64_t)(end-code);
    args.stack=(uint64_t)(page-8192UL); args.stack_bytes=12288;
    /* These pages are inside this worker's allocated 32 KiB stack. Resolve
     * private COW writes in USER context before the kernel's resident-only
     * preparation; neither admission nor capture may fault pages in. */
    for(touch=page-8192UL;touch<=page;touch+=4096UL) {
        volatile unsigned char *byte=(volatile unsigned char *)touch;
        unsigned char value=*byte;*byte=value;
    }
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    MAPPING_LIFETIME_SNAPSHOT before,after;
    /* Warm both copyout destinations before taking the conservation baseline
     * (a private child may have to resolve its own stack COW pages). */
    if(pclone_trap(&before,MLT_SNAPSHOT) || pclone_trap(&after,MLT_SNAPSHOT) ||
       pclone_trap(&before,MLT_SNAPSHOT)) w->error|=128;
#endif
    /* Both rollback prefixes must free every private owner before retry. */
    if (pclone_trap(&args,IRT_FAIL_CODE)!=-39L) w->error|=1;
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    if(pclone_trap(&after,MLT_SNAPSHOT) || !irq_restart_resources_equal(&before,&after))w->error|=128;
#endif
    if (pclone_trap(&args,IRT_FAIL_STACK)!=-39L) w->error|=2;
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    if(pclone_trap(&after,MLT_SNAPSHOT) || !irq_restart_resources_equal(&before,&after))w->error|=128;
#endif
    if(w->private_space) irq_restart_private_cell=private_pattern;
    /* The first two cases must not be accepted as a delayed return. */
    if(pclone_trap(0,IRT_ABORT)!=-36L) w->error|=256;
    value=pclone_trap(&args,IRT_BEGIN);
    if(value || pclone_trap(&args,IRT_BEGIN)!=-36L || pclone_trap(0,IRT_ABORT) ||
       pclone_trap(0,IRT_ABORT)!=-36L) w->error|=256;
    value=pclone_trap(&args,IRT_CAPTURE_ONLY);
    if(value || irq_restart_pattern(w->pattern) || pclone_trap(0,IRT_FINISH)!=1) w->error|=32;
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
    if(w->error) {
        char message[80];
        sprintf(message,"IRQ restart peer pattern=%d private=%d errors=%lx\r\n",
                w->pattern,w->private_space,(unsigned long)w->error);
        conws(message);
    }
    w->done=1;
    Pterm(w->error?87:0);
}
#if defined(TEST_WITH_PTERMGRP)
static volatile int irq_restart_exit_mode,irq_restart_cancel_ready;
static volatile int irq_restart_cancel_seen,irq_restart_cancel_user;
static void irq_restart_cancel_peer(void *unused)
{
    int spin;
    (void)unused;
    for(spin=0;spin<128 && !irq_restart_cancel_ready;spin++) (void)Syield();
    if(!irq_restart_cancel_ready) { Pterm(87);return; }
    irq_restart_cancel_seen=1;
    if(irq_restart_exit_mode==2) {
        (void)term_group(61);
        Pterm(87);return;
    }
    Pterm(0);
}
static void irq_restart_exit_entry(void *unused)
{
    struct { uint64_t code,code_bytes,stack,stack_bytes; } args;
    struct ptos_clone_args peer={0};
    uintptr_t code=(uintptr_t)clone_irq_registers;
    unsigned long page=(unsigned long)(uintptr_t)&args & ~4095UL,touch;
    (void)unused;
    args.code=code;args.code_bytes=(uintptr_t)clone_irq_registers_end-code;
    args.stack=page-8192UL;args.stack_bytes=12288;
    for(touch=page-8192UL;touch<=page;touch+=4096UL) {
        volatile unsigned char *p=(volatile unsigned char *)touch;
        unsigned char value=*p;*p=value;
    }
    if(irq_restart_exit_mode) {
        peer.flags=CLONE_VM|CLONE_SAME_PROCESS;peer.entry=irq_restart_cancel_peer;
        peer.stack_base=runnable_stacks[1];peer.stack_size=RUNNABLE_STACK_SIZE;
        if(pclone_trap(&peer,sizeof(peer))<=0) { Pterm(87);return; }
    }
    if(pclone_trap(&args,IRT_BEGIN)) { Pterm(87);return; }
    if(!irq_restart_exit_mode) { Pterm(62);return; } /* ARMED, never frozen */
    irq_restart_cancel_ready=1;
    if(irq_restart_pattern(0)) { Pterm(87);return; }
    irq_restart_cancel_user=1;
    /* Mode 2 must stop via existing cancellation before returning to USER.
     * Mode 1 verifies ACKED ownership remains attached until off-stack exit. */
    if(irq_restart_exit_mode==2 || pclone_trap(0,IRT_QUERY)!=5 || !irq_restart_cancel_seen)
        { Pterm(87);return; }
    Pterm(63);
}
static int irq_restart_test_retirement(void)
{
    int mode,good=1;
    for(mode=0;mode<3;mode++) {
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
        if(child<=0 || (status>>16)!=pid || (status&65535)!=(mode==2?61:62+mode) ||
           (mode && !irq_restart_cancel_seen) || (mode==2 && irq_restart_cancel_user)) good=0;
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
static void irq_restart_driver(void *unused)
{
    int mode,cycle,i,good=1;
(void)unused;
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    int modes=3;
#else
    int modes=2;
#endif
    conws("IRQ restart owned driver entered\r\n");
    for(mode=0;mode<modes;mode++) for(cycle=0;cycle<3;cycle++) {
        int32_t pid[2]={0,0},tid[2]={0,0},status;
        irq_restart_private_cell=0;
        for(i=0;i<2;i++) {
            struct ptos_clone_args args={0};
            long created;
            irq_restart_workers[i].error=irq_restart_workers[i].done=0;
            irq_restart_workers[i].pattern=i;
            irq_restart_workers[i].private_space=mode==2;
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
#if defined(TEST_WITH_PTHJOIN)
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
                             "actual B USER execution observed between A freeze and owned register return");
        }
        if(mode==2) {
            if(irq_restart_private_cell)good=0;
            ptest_assert_msg(!irq_restart_private_cell,"private peers preserve parent and independent same-VA data");
        }
    }
#if defined(TEST_WITH_PTERMGRP)
    if(!irq_restart_test_retirement())good=0;
#endif
    if(good && !ptest_failures()) conws("native interrupted USER A IRQ / B USER execution / saved A restart: PASS\r\n");
    Pterm(good?0:87);
}
static void test_irq_restart(void)
{
    struct ptos_clone_args args={0};
    long child,status;
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
    if((status>>16)==child && !(status&65535)) {
        conws("native interrupted USER A IRQ / B USER execution / saved A restart: PASS\r\n");
#if defined(TEST_WITH_PTERMGRP)
        conws("native retained IRQ armed/acknowledged exit and suspended group cancellation: PASS\r\n");
#endif
    }
}
#endif
