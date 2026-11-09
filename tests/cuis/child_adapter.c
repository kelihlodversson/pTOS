/* Production libaes adapter with a modeled CUIS transport. This checks policy
 * and retry boundaries, not kernel completion ownership or native scheduling. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "gem.h"
#include "gemerror.h"
#include "string.h"
#include "../../lib/aes/children.c"
#define VERIFY(c) do { if (!(c)) { fprintf(stderr,"child adapter:%d: %s\n",__LINE__,#c); abort(); } } while (0)
static LONG notices[CUIS_CHILDREN_PER_SESSION], publication_error, reserve_result;
static unsigned count, consumed, published, attempts, calls;
static ULONG last_function;
static UQUAD last[6];
static const LONG session_id = 71;
LONG cuis_call6(ULONG function,UQUAD a,UQUAD b,UQUAD c,UQUAD d,UQUAD e,UQUAD f)
{
    const short *message;
    unsigned i;
    VERIFY(a == (UQUAD)session_id);
    calls++; last_function=function;
    last[0]=a; last[1]=b; last[2]=c; last[3]=d; last[4]=e; last[5]=f;
    if (function == CUIS_CHILD_RESULT) {
        VERIFY(!b && !c && !d && !e && !f);
        return consumed < count ? notices[consumed] : 0;
    }
    if (function == CUIS_CHILD_PUBLISH) {
        VERIFY(consumed < count && b == (ULONG)notices[consumed]);
        VERIFY(d == 16 && !e && !f);
        message=(const short *)(uintptr_t)c;
        VERIFY(message[0] == CH_EXIT && !message[1] && !message[2]);
        VERIFY((UWORD)message[3] == (UWORD)((ULONG)b >> 16));
        VERIFY((UWORD)message[4] == (UWORD)b);
        for (i=5;i<8;i++) VERIFY(!message[i]);
        attempts++;
        if (publication_error) return publication_error;
        consumed++; published++; return E_OK;
    }
    if (function == CUIS_CHILD_WATCH) {
        VERIFY(b == CUIS_CHILD_RESERVE && !c && !d && !e && !f);
        return reserve_result;
    }
    return 123;
}
UQUAD aes_gemdos_call4(ULONG function,UQUAD a,UQUAD b,UQUAD c,UQUAD d)
{
    VERIFY(function == 0x4b && a == PE_ASYNC_LOADGO);
    calls++; last_function=function;
    last[0]=a; last[1]=b; last[2]=c; last[3]=d;
    return 321;
}
static void reset(void)
{
    memset(notices,0,sizeof(notices)); count=consumed=published=attempts=calls=0;
    publication_error=0; reserve_result=17;
}
int main(void)
{
    const char path[]="CHILD.PRG", tail[]={0}, env[]={0,0}, directory[]="C:\\APPS";
    unsigned i, before;
    reset();
    /* Completion order, not launch/PID order. Full words include normal ffff
     * and legacy GEM's signal encoding. No status sign test may discard ffff. */
    notices[0]=((LONG)42 << 16)|0xffff;
    notices[1]=((LONG)41 << 16)|0x0400;
    count=2; publication_error=EAGAIN;
    VERIFY(aes_child_messages(session_id) == E_OK);
    VERIFY(attempts == 1 && !consumed && !published);
    publication_error=EIMBA;
    VERIFY(aes_child_messages(session_id) == EIMBA);
    VERIFY(attempts == 2 && !consumed && !published);
    publication_error=0;
    VERIFY(aes_child_messages(session_id) == E_OK && consumed == 2 && published == 2);
    before=attempts;
    VERIFY(aes_child_messages(session_id) == E_OK && attempts == before);
    reset(); count=CUIS_CHILDREN_PER_SESSION;
    for (i=0;i<count;i++) notices[i]=((LONG)(i+1)<<16)|i;
    VERIFY(aes_child_messages(session_id) == E_OK && published == count);
    VERIFY(aes_child_messages(session_id) == E_OK && published == count);
    reset(); reserve_result=EAGAIN;
    VERIFY(aes_child_launch(session_id,path,tail,env,NULL,FALSE,TRUE) == EAGAIN);
    VERIFY(last_function == CUIS_CHILD_WATCH); /* Never publish without a slot. */
    reset();
    VERIFY(aes_child_launch(session_id,path,tail,env,NULL,FALSE,TRUE) == 123);
    VERIFY(last_function == CUIS_CHILD_LAUNCH && last[1] == 17);
    VERIFY(last[2] == (uintptr_t)path && last[3] == (uintptr_t)tail && last[4] == (uintptr_t)env && !last[5]);
    reset();
    VERIFY(aes_child_launch(session_id,path,tail,env,directory,TRUE,TRUE) == 123);
    VERIFY(last_function == CUIS_CONSOLE_LAUNCH_DIRECTORY && last[1] == 17 && last[5] == (uintptr_t)directory);
    reset();
    VERIFY(aes_child_launch(session_id,path,tail,env,directory,FALSE,FALSE) == 123);
    VERIFY(last_function == CUIS_CHILD_LAUNCH_DIRECTORY && !last[1] && calls == 1);
    reset();
    VERIFY(aes_child_launch(session_id,path,tail,env,NULL,TRUE,FALSE) == 123);
    VERIFY(last_function == CUIS_CHILD_LAUNCH && !last[1] && last[5] == TRUE && calls == 1);
    reset();
    VERIFY(aes_child_launch(session_id,path,tail,env,NULL,FALSE,FALSE) == 321);
    VERIFY(last_function == 0x4b && calls == 1); /* Ordinary Pexec stays independent. */
    VERIFY(aes_child_defer(session_id,path,tail,env,directory) == 123);
    VERIFY(last_function == CUIS_PROCESS_DEFER_DIRECTORY && last[4] == (uintptr_t)directory);
    VERIFY(aes_child_defer(session_id,path,tail,env,NULL) == 123);
    VERIFY(last_function == CUIS_PROCESS_DEFER && !last[4]);
    puts("Production libaes CHILD policy: status words, completion order, publication retry, exactly-once and launch routing: PASS");
    return 0;
}
