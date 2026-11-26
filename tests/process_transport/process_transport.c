/* Real Ssystem trap/packed-call coverage on supported native MMU kernels. */
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
    unsigned long operation,handle,policy,flags;
    unsigned long path,tail,environment,directory;
    COMPLETION completion;
    unsigned long reserved_tail[2];
} __attribute__((aligned(4))) REQUEST;
#ifdef TEST_WITH_PROCESS_EXEC_DISPATCH
static void request_launch(REQUEST *request, const char *path, int fault)
{
#ifdef TEST_NATIVE_ELF_ENTRY
    static const char *const vectors[][3] = {
        { "PROCXIT.TOS", 0, 0 }, { "PROCXIT.TOS", "F", 0 }
    };
    static const char *const environment[] = { 0 };

    request->flags = 1; /* PROCESS_EXEC_VECTOR_ARGS */
    request->tail = (unsigned long)vectors[fault];
#else
    static const char tails[][3] = { { 0 }, { 1, 'F', 0 } };
    static const char environment[] = { 0, 0 };

    request->tail = (unsigned long)tails[fault];
#endif
    request->path = (unsigned long)path;
    request->environment = (unsigned long)environment;
}
#endif

void test_process_transport(void);
void test_process_transport(void)
{
    ptest_begin("process_transport");
#ifdef TEST_WITH_PROCESS_EXEC
    ptest_assert_msg(Ssystem(-1,PROCEXEC,0L)==0,
                     "production PROCESS transport must be advertised");
#else
    ptest_assert_msg(Ssystem(-1,PROCEXEC,0L)==EINVFN,
                     "unpublished PROCESS transport must not be advertised");
#endif
#ifndef TEST_WITH_PROCESS_EXEC_DISPATCH
    ptest_assert_msg(Ssystem(PROCEXEC,0L,72L)==EINVFN,
                     "unsupported kernel exposed PROCESS dispatch");
#else
    {
        REQUEST r;
        long handle,pid,result;
        unsigned i;
        ptest_assert_msg(sizeof(r)==72 && sizeof(r.completion)==32,"PROCESS transport wire layout");
        memset(&r,0,sizeof(r)); r.operation=3;
        ptest_assert_msg(Ssystem(PROCEXEC,(long)&r,71L)==EINVAL,"short native request size");
        ptest_assert_msg(Ssystem(PROCEXEC,(long)&r,-1L)==EINVAL,"negative native request size");
        ptest_assert_msg(Ssystem(PROCEXEC,-1L,72L)==EIMBA,"wrapped native request pointer");
        ptest_assert_msg(Ssystem(PROCEXEC,0L,72L)==EIMBA,"unmapped native request pointer");
        ptest_assert_msg(Ssystem(PROCEXEC,(long)&r+1,72L)==EINVAL,"misaligned native request pointer");
        ptest_assert_msg(Ssystem(PROCEXEC,(long)&r,72L)==EIHNDL,"zero owned handle must be rejected");
        memset(&r,0,sizeof(r));
        request_launch(&r, "C:\\NOEXEC.TOS", 0);
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
        memset(&r,0,sizeof(r));
        request_launch(&r, "C:\\PROCXIT.TOS", 0);
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
        {
            memset(&r,0,sizeof(r));
            request_launch(&r, "C:\\PROCXIT.TOS", 1);
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
        memset(&r,0,sizeof(r)); r.operation=5;
        request_launch(&r, "C:\\PROCXIT.TOS", 0);
        ptest_assert_msg(Ssystem(PROCEXEC,(long)&r,72L)==0,
                         "schedule a detached successor without a GUI registration");
        Syield();
        ptest_assert_msg(Pwaitpid(-1,1,0)==EFILNF,
                         "unpublished successor is outside the public child wait domain");
        r.path=(unsigned long)"C:\\NOEXEC.TOS";
        ptest_assert_msg(Ssystem(PROCEXEC,(long)&r,72L)==EFILNF,
                         "failed deferred replacement preserves the constructor error");
        memset(&r,0,sizeof(r)); r.operation=4;
        ptest_assert_msg(Ssystem(PROCEXEC,(long)&r,72L)==0 &&
                         Ssystem(PROCEXEC,(long)&r,72L)==0,"idempotent CANCEL_DEFERRED");
    }
#endif
    ptest_pass();
}
