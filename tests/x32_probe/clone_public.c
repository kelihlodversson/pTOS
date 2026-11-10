/* Real ILP32 GEMDOS Pclone integration; no kernel-only yield hook. */
#include "pclone.h"
#include "mman.h"
#include "config.h"
#if CONF_WITH_PCLONE && CONF_WITH_NATIVE_SCHED
typedef long long S64;
extern S64 x32_probe_call(unsigned long long, S64, S64, S64);
extern S64 x32_probe_clone_stack(void);
#define CALL(n,a,b,c) x32_probe_call(n,(S64)(unsigned long)(a),(S64)(unsigned long)(b),(S64)(unsigned long)(c))
static volatile int entered;
static volatile int mode;
static volatile int child_seen;
static volatile int early;
static volatile int resource_result;
static char parent_path[128], child_path[128];
static unsigned resource_flags;
static void entry(void *argument)
{
    child_seen = (int)CALL(0x10b,0,0,0);
    if ((unsigned long)argument != 0xfedcba98UL) CALL(0x4c,99,0,0);
    entered++;
    if (mode == 3) {
        int same=1, i;
        S64 seek=x32_probe_call(0x42,0,1,0);
        if ((resource_flags & CLONE_COPY_STANDARD_HANDLES) ? seek<0 : seek>=0) resource_result |= 1;
        if (CALL(0x47,child_path,3,0)) resource_result |= 2;
        for (i=0;i<128;i++) {
            if (parent_path[i]!=child_path[i]) { same=0; break; }
            if (!parent_path[i]) break;
        }
        if ((resource_flags & CLONE_COPY_CWD) ? !same : same) resource_result |= 4;
    }
    if (mode == 5) CALL(0x31,4096,0x468a,0);
    if (mode == 2) __asm__ volatile("ud2");
    if (mode == 1) CALL(0x4c,0x3579,0,0);
    if (!early) {
        CALL(0xff,0,0,0);
        CALL(0xff,0,0,0);
    }
    if (mode == 4) {
        if (CALL(0x40,1,2,"OK")!=2) CALL(0x4c,98,0,0);
    }
    /* A public void return must ignore arbitrary EAX contents. */
    __asm__ volatile("movl $0x7bad, %%eax" ::: "eax");
}
int x32_public_clone_single(int expected);
int x32_public_clone_single(int expected)
{
    struct ptos_clone_args args={0};
    int32_t tid=-7,pid=-9;
    S64 stack=CALL(0x48,8192,0,0),result;
    if(stack<=0) return 1;
    args.flags=CLONE_VM;args.entry=entry;args.argument=(void *)0xfedcba98UL;
    args.stack_base=(void *)(unsigned long)stack;args.stack_size=8192;
    args.child_pid=&pid;args.child_tid=&tid;
    *(volatile unsigned long *)((unsigned long)stack+8184)=0x12345678UL;
    mode=expected ? 0 : 5;entered=0;
    result=CALL(GEMDOS_PCLONE,&args,32,0);
    if(expected) {
        if(result!=expected || tid!=-7 || pid!=-9 || entered ||
            *(volatile unsigned long *)((unsigned long)stack+8184)!=0x12345678UL) return 2;
    } else {
        if(result<=0 || result!=tid || pid<=0 ||
            (x32_probe_call(0x13a,pid,0,0)&0xffff)!=0x8a) return 3;
    }
    return CALL(0x49,stack,0,0) ? 4 : 0;
}
int x32_public_clone_orphan(void);
int x32_public_clone_orphan(void)
{
    struct ptos_clone_args args={0};
    S64 stack=CALL(0x48,8192,0,0),file=CALL(0x3c,"C:\\PCLORPH.TMP",0,0);
    if(stack<=0 || file<6 || CALL(0x46,1,file,0)) return 1;
    args.flags=CLONE_VM|CLONE_COPY_STANDARD_HANDLES;
    args.entry=entry;args.argument=(void *)0xfedcba98UL;
    args.stack_base=(void *)(unsigned long)stack;args.stack_size=8192;
    mode=4;
    if(CALL(GEMDOS_PCLONE,&args,32,0)<=0) return 2;
    CALL(0x4c,77,0,0);
    return 3;
}
int x32_public_clone_checks(void);
int x32_public_clone_checks(void)
{
    struct ptos_clone_args args = {0};
    int32_t tid, pid;
    S64 stack = CALL(0x48,8192,0,0), result, waited;
    unsigned size;
    int m;
    unsigned long stacks[12];
    int32_t pids[12];
    int i;
    if (stack <= 0) return 1;
    args.entry=entry; args.argument=(void *)0xfedcba98UL;
    args.stack_base=(void *)(unsigned long)stack; args.stack_size=8192;
    args.child_tid=&tid; args.child_pid=&pid;
    for (m=0;m<3;m++) {
        for (size=28;size<=36;size++) {
            tid=-7; pid=-9; entered=0; child_seen=0; mode=m;
            args.flags=CLONE_VM;
            result=CALL(GEMDOS_PCLONE,&args,size,0);
            if (result<=0) return 0x100-(int)result;
            if (result!=tid) return 0x200;
            if (size<32 ? pid!=-9 : pid<=0) return 0x201;
            if (entered) return 3; /* no dispatch during publication */
            /* Wait for any child when the old prefix omitted PID. */
            waited=x32_probe_call(0x13a,size<32 ? -1 : pid,0,0);
            if (waited<0) return 4;
            if ((waited>>16)!=child_seen || (size>=32 && child_seen!=pid)) return 5;
            if ((waited&0xffff)!=(m==0 ? 0 : m==1 ? 0x79 : 0x400)) return 6;
            if (m!=2 && entered!=1) return 7;
        }
    }
    args.flags=CLONE_VM|CLONE_SAME_PROCESS|CLONE_COPY_CWD;
    tid=-7;pid=-9;
    if (CALL(GEMDOS_PCLONE,&args,32,0)!=-25 || tid!=-7 || pid!=-9) return 8;
    args.flags=0x80000000UL;
    if (CALL(GEMDOS_PCLONE,&args,32,0)!=-25 || tid!=-7 || pid!=-9) return 9;
    args.flags=CLONE_VM; args.tls=(void *)1;
    if (CALL(GEMDOS_PCLONE,&args,32,0)!=-25 || tid!=-7 || pid!=-9) return 10;
    args.tls=0; args.child_tid=(int32_t *)((unsigned long)entry & ~3UL);
    if(CALL(GEMDOS_PCLONE,&args,32,0)!=-40 || tid!=-7 || pid!=-9) return 21;
    args.child_tid=&tid; args.child_pid=&tid;
    if(CALL(GEMDOS_PCLONE,&args,32,0)!=-25 || tid!=-7 || pid!=-9) return 22;
    args.child_pid=&pid;
    if(CALL(GEMDOS_PCLONE,&args,27,0)!=-25 || tid!=-7 || pid!=-9) return 23;
    args.flags=0;
    if(CALL(GEMDOS_PCLONE,&args,32,0)!=-32 || tid!=-7 || pid!=-9) return 24;
    args.flags=CLONE_VM;
    args.child_tid=0;args.child_pid=0;mode=0;
    result=CALL(GEMDOS_PCLONE,&args,32,0);
    if (result<=0 || x32_probe_call(0x13a,-1,0,0)<0) return 11;
    {
        S64 file=CALL(0x3c,"C:\\PCLONE.TMP",0,0);
        if (file<6 || CALL(0x46,1,file,0)) return 17;
        if (CALL(0x39,"C:\\PCLONE",0,0) || CALL(0x3b,"C:\\PCLONE",0,0) ||
            CALL(0x47,parent_path,3,0)) return 18;
        mode=3;
        for (i=0;i<4;i++) {
            resource_flags=(i&1 ? CLONE_COPY_STANDARD_HANDLES : 0) | (i&2 ? CLONE_COPY_CWD : 0);
            args.flags=CLONE_VM|resource_flags;args.child_tid=&tid;args.child_pid=&pid;
            resource_result=0;
            result=CALL(GEMDOS_PCLONE,&args,32,0);
            if(result<=0) return 0x400-(int)result;
            if(x32_probe_call(0x13a,pid,0,0)<0) return 0x500;
            if(resource_result) return 0x300+(i<<4)+resource_result;
        }
        if (CALL(0x3e,1,0,0) || CALL(0x46,1,-1,0) || CALL(0x3e,file,0,0) ||
            CALL(0x3b,"C:\\",0,0) || CALL(0x3a,"C:\\PCLONE",0,0) ||
            CALL(0x41,"C:\\PCLONE.TMP",0,0)) return 20;
        args.flags=CLONE_VM;mode=0;
    }
#if CONF_WITH_MMAP
    {
        S64 mapped=x32_probe_clone_stack();
        if(mapped<=0) return 25;
        args.stack_base=(void *)(unsigned long)mapped;
        result=CALL(GEMDOS_PCLONE,&args,32,0);
        if(result<=0 || CALL(GEMDOS_MPROT,mapped,8192,PROT_READ)!=-36 ||
            CALL(GEMDOS_MUNMAP,mapped,8192,0)!=-36) return 26;
        if(x32_probe_call(0x13a,pid,0,0)<0 || CALL(GEMDOS_MUNMAP,mapped,8192,0)) return 27;
        args.stack_base=(void *)(unsigned long)stack;
    }
#endif
    /* Grow past the bootstrap arena without yielding, then wait/reclaim every
     * independently identified PROCESS. */
    early=1;
    args.child_tid=&tid; args.child_pid=&pid;
    for (i=0;i<11;i++) {
        S64 allocated=CALL(0x48,8192,0,0);
        if (allocated<=0) return 13;
        stacks[i]=(unsigned long)allocated;
        args.stack_base=(void *)stacks[i];
        if (CALL(GEMDOS_PCLONE,&args,32,0)<=0) return 14;
        pids[i]=pid;
    }
    args.stack_base=(void *)(unsigned long)stack;
    tid=-7;pid=-9;
    *(volatile unsigned long *)((unsigned long)stack+8184)=0x12345678UL;
    result=CALL(GEMDOS_PCLONE,&args,32,0);
    if (result<=0) return 0x1500-(int)result;
    if (tid!=result || pid<=0) return 15;
    pids[11]=pid;
    for (i=0;i<12;i++) {
        S64 finished=x32_probe_call(0x13a,pids[i],0,0);
        if (finished<0 || (finished&0xffff) || (i<11 && CALL(0x49,stacks[i],0,0))) return 16;
    }
    if (CALL(0x49,stack,0,0)) return 12;
    return 0;
}
#endif
