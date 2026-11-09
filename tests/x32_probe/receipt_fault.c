/* Real SAME_PROCESS publisher #UD and sibling continuation. The MEMTEST-only
 * Syield admission hook calls the production receipt runtime; no receipt trap
 * selector or production user ABI is introduced by this fixture. */
#include "config.h"
#include "pclone.h"
#if CONF_WITH_CUIS_RECEIPT_TEST && CONF_WITH_PCLONE && CONF_WITH_PTHJOIN
extern long long x32_probe_call(unsigned long long,long long,long long,long long);
#define RF_CALL(n,a,b,c) x32_probe_call(n,(long long)(unsigned long)(a),(long long)(unsigned long)(b),(long long)(unsigned long)(c))
static void receipt_publisher(void *argument)
{
    (void)argument;
    RF_CALL(0xff,0,0,0); /* snapshot A then stall before packet commit */
    RF_CALL(0xff,0,0,0); /* test hook commits A before this syscall returns */
    __asm__ volatile("ud2"); /* no userspace delivery-progress write */
}
static void receipt_survivor(void *argument)
{
    (void)argument;
    RF_CALL(0xff,0,0,0); /* try B while A is stalled: canonical FIFO blocks it */
    RF_CALL(0xff,0,0,0); /* waits for publisher stop, drains, recovers A then B */
}
int x32_receipt_fault_checks(void);
int x32_receipt_fault_checks(void)
{
    struct ptos_clone_args args={0};
    long long stack[2],result;
    int32_t pid[2],tid[2];
    unsigned i;
    RF_CALL(0xff,0,0,0); /* establish BOUND child and terminal status first */
    for(i=0;i<2;i++) {
        stack[i]=RF_CALL(0x48,8192,0,0);
        if(stack[i]<=0)return 51;
        args.flags=CLONE_VM|CLONE_SAME_PROCESS;
        args.entry=i ? receipt_survivor:receipt_publisher;
        args.stack_base=(void *)(unsigned long)stack[i];args.stack_size=8192;
        args.child_pid=&pid[i];args.child_tid=&tid[i];
        result=RF_CALL(GEMDOS_PCLONE,&args,sizeof(args),0);
        if(result<=0 || result!=tid[i] || pid[i]!=RF_CALL(0x10b,0,0,0))return 52;
    }
    result=RF_CALL(0x11e,tid[1],0,0);
    if(result)return 53;
    for(i=0;i<2;i++)if(RF_CALL(0x49,stack[i],0,0))return 54;
    return 0;
}
#endif
