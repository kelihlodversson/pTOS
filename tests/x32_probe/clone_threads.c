/* SAME_PROCESS tests execute through the public ILP32 GEMDOS trap. */
#include "config.h"
#include "pclone.h"
#include "threadcalls.h"
#if CONF_WITH_MULTITHREADING
typedef long long S64;
extern S64 x32_probe_call(unsigned long long,S64,S64,S64);
extern unsigned x32_thread_registers(unsigned seed);
extern long clone_irq_registers(void);
#define CALL(n,a,b,c) x32_probe_call(n,(S64)(unsigned long)(a),(S64)(unsigned long)(b),(S64)(unsigned long)(c))
#define THREADS 3
struct worker { unsigned long stack; int32_t tid,pid; unsigned seed; int kind; };
static struct worker workers[12];
static volatile unsigned count,ready,errors,release_workers,leader_stopped;
static int32_t group_leader_tid;
static int process_pid, file_handle, check_cwd;
static volatile int failure_status;
static int fail(int code) { failure_status=code; release_workers=1; return code; }
static int wait_failure(unsigned variant,S64 status)
{
    const char *labels[]={"thread wait variant ","thread wait status ","thread wait errors "};
    unsigned values[]={variant,(unsigned)status,errors},i,n;
    for(i=0;i<3;i++) {
        char text[]="0x00000000\r\n";
        for(n=0;n<8;n++)text[2+n]="0123456789abcdef"[(values[i]>>(28-4*n))&15];
        CALL(9,labels[i],0,0);CALL(9,text,0,0);
    }
    return fail(34);
}
static void yield(void) { CALL(0xff,0,0,0); }
static void finish(int status) { CALL(0x4c,status,0,0); for (;;) ; }
static void increment(volatile unsigned *value)
{
#if CONF_WITH_PREEMPTION
    (void)__sync_fetch_and_add(value,1);
#else
    (*value)++;
#endif
}
static void error(unsigned bits)
{
#if CONF_WITH_PREEMPTION
    (void)__sync_fetch_and_or(&errors,bits);
#else
    errors|=bits;
#endif
}
static S64 create(struct worker *w,int kind,unsigned flags);
static void worker(void *argument)
{
    struct worker *w=argument;
    volatile unsigned canary=w->seed;
    unsigned long local=(unsigned long)&canary;
    if (CALL(0x10b,0,0,0)!=process_pid || w->pid!=process_pid || w->tid<=0 || local<w->stack || local>=w->stack+8192) error(1);
    if (clone_irq_registers()) error(4);
    if (x32_thread_registers(w->seed) || canary!=w->seed) error(2);
    increment(&ready);
#if CONF_WITH_PREEMPTION
    if(w->kind==7) {
        /* Observe actual initial-TID completion, not its last USER store.
         * This sibling remains live while the parent checks WNOHANG. */
        if(CALL(GEMDOS_PTHJOIN,group_leader_tid,0,0)) error(16);
        leader_stopped=1;
    }
#endif
    while(!release_workers) yield();
    increment(&count);
    if (failure_status) finish(failure_status);
#if CONF_WITH_PREEMPTION
    if(w->kind==8 || w->kind==9) {
        /* Keep the intended status/fault as the final THREAD event even if
         * a timer interrupts the ordinary sibling just before its return. */
        if(CALL(GEMDOS_PTHJOIN,workers[0].tid,0,0)) error(32);
    }
#endif
    if (w->kind==6) {
        static const char empty_tail[2]={0,0};
        if(CALL(0x4b,0,"C:\\X32HELLO.TOS",empty_tail)!=0) finish(94);
        if(create(&workers[1],CONF_WITH_PREEMPTION ? 9 : 3,
                  CLONE_VM|CLONE_SAME_PROCESS)<=0) finish(93);
        finish(0x44);
    }
    if (w->kind==1) finish(0x1234);
    if (w->kind==2 || w->kind==8) __asm__ volatile("ud2");
    if (w->kind==3 || w->kind==9) {
        char path[128];
        if (errors || CALL(0x47,path,3,0) || CALL(0x40,file_handle,2,"OK")!=2) finish(90);
        finish(0x66);
    }
    if (w->kind==4) {
        if(check_cwd && CALL(0x3b,"C:\\THRCWD",0,0)) error(8);
        if (CALL(0x31,4096,0x67,0)!=-36) error(4);
    }
    if (w->kind==5) {
        if (errors || CALL(0x40,file_handle,2,"OK")!=2) finish(91);
        CALL(0x31,4096,0x68,0);
        finish(92);
    }
}
static S64 create(struct worker *w,int kind,unsigned flags)
{
    struct ptos_clone_args args={0};
    S64 stack=CALL(0x48,8192,0,0);
    if(stack<=0) return stack;
    w->stack=(unsigned long)stack;w->seed=w->stack^0x45671234U;w->kind=kind;
    w->pid=-7;w->tid=-9;
    args.flags=flags;args.entry=worker;args.argument=w;
    args.stack_base=(void *)w->stack;args.stack_size=8192;
    args.child_pid=&w->pid;args.child_tid=&w->tid;
    return CALL(GEMDOS_PCLONE,&args,sizeof(args),0);
}
static unsigned char invalid_stack[8192] __attribute__((aligned(16)));
static void group_entry(void *argument)
{
    struct ptos_clone_args invalid={0};
    int32_t tid=-7,pid=-9;
    int final_fault=(unsigned long)argument != 0;
    process_pid=(int)CALL(0x10b,0,0,0);
    file_handle=(int)CALL(0x3c,"C:\\THRGROUP.TMP",0,0);
    if(file_handle<6) finish(40);
    /* Rejected sibling construction must not revoke this NEW_PROCESS's
     * existing compatibility/controller ownership. */
    invalid.flags=CLONE_VM|CLONE_SAME_PROCESS;invalid.entry=worker;
    invalid.stack_base=invalid_stack;invalid.stack_size=sizeof(invalid_stack);
    invalid.child_tid=&tid;invalid.child_pid=&pid;
    if(CALL(GEMDOS_PCLONE,&invalid,32,0)!=-36 || tid!=-7 || pid!=-9) finish(41);
    if(create(&workers[0],CONF_WITH_PREEMPTION ? 7 : 0,CLONE_VM|CLONE_SAME_PROCESS)<=0 ||
       create(&workers[1],final_fault ? (CONF_WITH_PREEMPTION ? 8 : 2) :
                                      (CONF_WITH_PREEMPTION ? 9 : 3),
              CLONE_VM|CLONE_SAME_PROCESS)<=0) finish(42);
#if !CONF_WITH_PREEMPTION
    leader_stopped=1;
#endif
    /* Public void return stops this initial TID only. */
}
int x32_same_threads(int test);
int x32_same_threads(int test)
{
    unsigned i,cycle;
    S64 result;
    process_pid=(int)CALL(0x10b,0,0,0);
    if(test==6 || test==7) {
        unsigned variant;
        int parent_pid=process_pid;
        for(variant=0;variant<(test==6 ? 2U : 1U);variant++) {
            struct ptos_clone_args args={0};
            int32_t pid=-9;
            unsigned usage[2],before[8],after[8];
            S64 stack=CALL(0x48,8192,0,0),status;
            if(stack<=0) return fail(30);
            leader_stopped=0;release_workers=0;count=ready=errors=0;group_leader_tid=-7;
            args.flags=CLONE_VM;args.entry=group_entry;args.argument=(void *)(unsigned long)variant;
            args.stack_base=(void *)(unsigned long)stack;args.stack_size=8192;
            args.child_tid=&group_leader_tid;args.child_pid=&pid;
            if(CALL(0x11e,before,0,0) || CALL(GEMDOS_PCLONE,&args,32,0)<=0) return fail(31);
            if(test==7) { release_workers=1;finish(0x55); }
            while(!leader_stopped) yield();
            yield();
            if(x32_probe_call(0x13a,pid,1,0)!=0) return fail(32);
            if(CALL(0x10b,0,0,0)!=parent_pid) return fail(33);
            release_workers=1;
            status=x32_probe_call(0x13a,pid,0,(unsigned long)usage);
            if(status<0 || (status>>16)!=pid || (status&0xffff)!=(variant ? 0x400 : 0x66) || errors)
                return wait_failure(variant,status);
            if(CALL(0x11e,after,0,0) || after[3]-before[3]!=usage[0]) return fail(36);
            if(x32_probe_call(0x13a,pid,1,0)!=-33) return fail(37);
            if(CALL(0x11e,before,0,0) || before[3]!=after[3]) return fail(38);
            if(CALL(0x49,stack,0,0) || CALL(0x49,workers[0].stack,0,0) || CALL(0x49,workers[1].stack,0,0))
                return fail(35);
        }
        return 0;
    }
    if (test==1 || test==2 || test==3) {
        file_handle=(int)CALL(0x3c,"C:\\THRLEAD.TMP",0,0);
        if(file_handle<6) { return fail(1); }
        release_workers=1;
        result=create(&workers[0],test==3 ? 5 : test==1 ? 6 : 3,CLONE_VM|CLONE_SAME_PROCESS);
        if(result<=0 || result!=workers[0].tid || workers[0].pid!=process_pid) { return fail(2); }
        if(test==2) __asm__ volatile("ud2");
        finish(0x77); /* final worker's 0x66/0x68 must become PROCESS status */
    }
    if(test==4 || test==5) {
        struct ptos_clone_args args={0};
        S64 stack=CALL(0x48,8192,0,0);
        int32_t tid=-7,pid=-9;
        if(stack<=0) { return fail(3); }
        args.flags=CLONE_VM|CLONE_SAME_PROCESS;args.entry=worker;args.argument=&workers[0];
        args.stack_base=(void *)(unsigned long)stack;args.stack_size=8192;
        args.child_pid=&pid;args.child_tid=&tid;
        *(volatile unsigned long *)((unsigned long)stack+8184)=0x13572468;
        result=CALL(GEMDOS_PCLONE,&args,32,0);
        if(result!=(test==4 ? -39 : -326) || tid!=-7 || pid!=-9 ||
            *(volatile unsigned long *)((unsigned long)stack+8184)!=0x13572468) { return fail(4); }
        return CALL(0x49,stack,0,0) ? 5 : 0;
    }
    if(CALL(0x39,"C:\\THRCWD",0,0)) return fail(18);
    for(cycle=0;cycle<12;cycle++) {
        check_cwd=cycle==0;
        count=ready=errors=0;release_workers=0;
        for(i=0;i<THREADS;i++) {
            result=create(&workers[i],i==0 ? 4 : i,CLONE_VM|CLONE_SAME_PROCESS);
            if(result<=0 || result!=workers[i].tid || workers[i].pid!=process_pid) { return fail(6); }
            if(i && workers[i].tid==workers[i-1].tid) { return fail(7); }
            if(CALL(0x49,workers[i].stack,0,0)!=-1) { return fail(8); }
        }
        result=CALL(0x31,4096,0,0);
        if(result!=-36) { return fail((int)result&0xffff); } /* no TSR mutation with siblings */
        if(x32_probe_call(0x13a,process_pid,1,0)!=-33) { return fail(10); } /* TIDs are not waitable PIDs */
        while(ready<THREADS) yield();
        if(errors) { return fail(0x100+errors); }
        release_workers=1;
#if CONF_WITH_PREEMPTION
        for(i=0;i<THREADS;i++)
            if(CALL(GEMDOS_PTHJOIN,workers[i].tid,0,0)) return fail(12);
        if(count!=THREADS) return fail(12);
#else
        while(count<THREADS) yield();
        yield();
#endif
        for(i=0;i<THREADS;i++) if(CALL(0x49,workers[i].stack,0,0)) { return fail(12); }
        if(cycle==0) {
            char path[128];
            static const char expected[]="\\THRCWD";
            if(CALL(0x47,path,3,0)) return fail(19);
            for(i=0;i<sizeof(expected);i++) if(path[i]!=expected[i]) return fail(20);
            if(CALL(0x3b,"C:\\",0,0) || CALL(0x3a,"C:\\THRCWD",0,0)) return fail(21);
        }
        if(errors || CALL(0x10b,0,0,0)!=process_pid) { return fail(13); }
    }
    count=ready=errors=0;release_workers=0;
    for(i=0;i<12;i++) if(create(&workers[i],0,CLONE_VM|CLONE_SAME_PROCESS)<=0) { return fail(14); }
    {
        /* Invalid resource policy cannot disturb twelve already-published siblings. */
        struct ptos_clone_args args={0};
        int32_t tid=-7,pid=-9;
        args.flags=CLONE_VM|CLONE_SAME_PROCESS|CLONE_COPY_CWD;args.entry=worker;
        args.stack_base=(void *)workers[0].stack;args.stack_size=8192;
        args.child_pid=&pid;args.child_tid=&tid;
        result=CALL(GEMDOS_PCLONE,&args,32,0);
        if(result!=-25) return fail(0x1500-(int)result);
        if(tid!=-7 || pid!=-9) return fail(15);
    }
    release_workers=1;
#if CONF_WITH_PREEMPTION
    for(i=0;i<12;i++)
        if(CALL(GEMDOS_PTHJOIN,workers[i].tid,0,0)) return fail(16);
    if(count!=12) return fail(16);
#else
    while(count<12) yield();
    yield();
#endif
    for(i=0;i<12;i++) if(CALL(0x49,workers[i].stack,0,0)) { return fail(16); }
    return errors ? fail(17) : 0;
}
#endif
