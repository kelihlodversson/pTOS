/* Real Ssystem trap/packed-call coverage; only explicit test kernels accept -9. */
#include "test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <string.h>
#define EINVFN (-32L)
#define EINVAL (-25L)
#define EIMBA (-40L)
#define EIHNDL (-37L)
#define EFILNF (-33L)
#define EACCDN (-36L)
#define PROCEXEC (-9)
/* Native userland is ILP32 on all targets. Kernel layout assertions are
 * compiled independently from include/process_exec.h on all three ABIs. */
typedef struct {
    unsigned long number_low,number_high;
    long pid;
    unsigned long state,kind;
    unsigned short status;
    short signal;
    unsigned long order_low,order_high;
} __attribute__((aligned(4))) COMPLETION;
typedef struct {
    unsigned long operation,handle,policy,reserved;
    unsigned long path,tail,environment,directory;
    COMPLETION completion;
    unsigned long reserved_tail[2];
} __attribute__((aligned(4))) REQUEST;
void test_process_transport(void);
void test_process_transport(void)
{
    ptest_begin("process_transport");
    ptest_assert_msg(Ssystem(-1,PROCEXEC,0L)==EINVFN,
                     "unpublished PROCESS transport must not be advertised");
#ifndef TEST_WITH_PROCESS_EXEC_TEST
    ptest_assert_msg(Ssystem(PROCEXEC,0L,72L)==EINVFN,
                     "normal/unsupported kernel exposed test-only PROCESS dispatch");
#else
    {
        REQUEST r;
        long handle,pid,result;
        unsigned i;
        char tail[]={0},env[]={0,0};
        ptest_assert_msg(sizeof(r)==72 && sizeof(r.completion)==32,"PROCESS transport wire layout");
        memset(&r,0,sizeof(r)); r.operation=3;
        ptest_assert_msg(Ssystem(PROCEXEC,(long)&r,71L)==EINVAL,"short native request size");
        ptest_assert_msg(Ssystem(PROCEXEC,(long)&r,-1L)==EINVAL,"negative native request size");
        ptest_assert_msg(Ssystem(PROCEXEC,-1L,72L)==EIMBA,"wrapped native request pointer");
        ptest_assert_msg(Ssystem(PROCEXEC,0L,72L)==EIMBA,"unmapped native request pointer");
        ptest_assert_msg(Ssystem(PROCEXEC,(long)&r+1,72L)==EINVAL,"misaligned native request pointer");
        ptest_assert_msg(Ssystem(PROCEXEC,(long)&r,72L)==EIHNDL,"zero owned handle must be rejected");
        memset(&r,0,sizeof(r));
        r.path=(unsigned long)"C:\\NOEXEC.TOS"; r.tail=(unsigned long)tail; r.environment=(unsigned long)env;
        ptest_assert_msg(Ssystem(PROCEXEC,(long)&r,72L)==EFILNF,"PREPARE preserves missing-file constructor error");
        r.path=(unsigned long)"C:\\PROCXIT.TOS";
        handle=Ssystem(PROCEXEC,(long)&r,72L);
        ptest_assert_msg(handle>0,"prepare ordinary PROCESS without GUI registration");
        if(handle<=0) { ptest_pass(); return; }
        memset(&r,0,sizeof(r)); r.operation=2; r.handle=(unsigned long)handle;
        ptest_assert_msg(Ssystem(PROCEXEC,(long)&r,72L)==0 && r.completion.state==0,
                         "prepared completion remains unstarted");
        r.operation=3;
        ptest_assert_msg(Ssystem(PROCEXEC,(long)&r,72L)==0 &&
                         Ssystem(PROCEXEC,(long)&r,72L)==EIHNDL,"CLOSE consumes preparation exactly once");
        memset(&r,0,sizeof(r)); r.path=(unsigned long)"C:\\PROCXIT.TOS";
        r.tail=(unsigned long)tail; r.environment=(unsigned long)env;
        handle=Ssystem(PROCEXEC,(long)&r,72L);
        ptest_assert_msg(handle>0,"prepare runnable PROCESS");
        if(handle<=0) { ptest_pass(); return; }
        memset(&r,0,sizeof(r)); r.operation=1; r.handle=(unsigned long)handle;
        pid=Ssystem(PROCEXEC,(long)&r,72L);
        ptest_assert_msg(pid>0 && Ssystem(PROCEXEC,(long)&r,72L)==EACCDN,"exactly one COMMIT");
        r.operation=2; result=0;
        for(i=0;i<512;i++) {
            result=Ssystem(PROCEXEC,(long)&r,72L);
            if(result || r.completion.state==2)break;
            Syield();
        }
        ptest_assert_msg(result==0 && r.completion.state==2 && r.completion.kind==1 &&
                         r.completion.status==0xffff && r.completion.signal==0,
                         "non-consuming normal PROCESS completion");
        ptest_assert_msg(Pwaitpid((int)pid,1,0)>0,"observation must not consume public Pwaitpid");
        ptest_assert_msg(Ssystem(PROCEXEC,(long)&r,72L)==0 && r.completion.state==2,
                         "public reaping must not consume observation");
        r.operation=3;
        ptest_assert_msg(Ssystem(PROCEXEC,(long)&r,72L)==0,"CLOSE completed observation");
        /* Reverse the consumption order and distinguish normal ffff from
         * an actual fatal fault: public Pwait first, then retained snapshot. */
        tail[0]=0;
        {
            char fault_tail[]={1,'F',0};
            memset(&r,0,sizeof(r)); r.path=(unsigned long)"C:\\PROCXIT.TOS";
            r.tail=(unsigned long)fault_tail; r.environment=(unsigned long)env;
            handle=Ssystem(PROCEXEC,(long)&r,72L);
            ptest_assert_msg(handle>0,"prepare faulting PROCESS");
            if(handle<=0) { ptest_pass(); return; }
            memset(&r,0,sizeof(r)); r.operation=1; r.handle=(unsigned long)handle;
            pid=Ssystem(PROCEXEC,(long)&r,72L);
            result=0;
            for(i=0;i<512 && result==0;i++) {
                Syield(); result=Pwaitpid((int)pid,1,0);
            }
            ptest_assert_msg(pid>0 && result>0,"public wait observes fault before snapshot");
            r.operation=2;
            ptest_assert_msg(Ssystem(PROCEXEC,(long)&r,72L)==0 && r.completion.state==2 &&
                             r.completion.kind==2 && r.completion.signal>0 && r.completion.status==0,
                             "early public reaping preserves distinct retained fault completion");
            r.operation=3;
            ptest_assert_msg(Ssystem(PROCEXEC,(long)&r,72L)==0,"CLOSE faulted observation");
        }
        memset(&r,0,sizeof(r)); r.operation=4;
        ptest_assert_msg(Ssystem(PROCEXEC,(long)&r,72L)==0 &&
                         Ssystem(PROCEXEC,(long)&r,72L)==0,"idempotent CANCEL_DEFERRED");
    }
#endif
    ptest_pass();
}
