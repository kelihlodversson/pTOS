/* Production libaes adapter with modeled PROCESS and receipt transports. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "gem.h"
#include "gemerror.h"
#include "string.h"
#include "../../lib/aes/children.c"
#define VERIFY(c) do { if (!(c)) { fprintf(stderr,"child adapter:%d: %s\n",__LINE__,#c); abort(); } } while (0)
static struct {
    PROCESS_COMPLETION completion;
    ULONG state;
    BOOL open;
    WORD packet[8];
} slots[CUIS_RECEIPT_SLOTS];
static LONG publication_error, launch_error, snapshot_error;
static unsigned published, launches, closes;
static ULONG last_function;
static UQUAD last[6];
static const LONG session_id=71;
LONG cuis_call6(ULONG function,UQUAD a,UQUAD b,UQUAD c,UQUAD d,UQUAD e,UQUAD f)
{
    CUIS_RECEIPT_RECORD *record=(CUIS_RECEIPT_RECORD *)(uintptr_t)c;
    unsigned index, i;
    VERIFY(a==(UQUAD)session_id);
    last_function=function;
    last[0]=a; last[1]=b; last[2]=c; last[3]=d; last[4]=e; last[5]=f;
    if (function==CUIS_LAUNCH_ATTACH) {
        if (launch_error) return launch_error;
        launches++;
        return 123;
    }
    VERIFY(function==CUIS_STREAM_RECEIPT && d==sizeof(*record) && !e && !f);
    index=record->slot;
    VERIFY(index<CUIS_RECEIPT_SLOTS && record->source==a && record->source_low==a);
    if (b==CUIS_RECEIPT_QUERY) {
        record->tag=index+100;
        record->state=slots[index].state;
        return E_OK;
    }
    VERIFY(record->tag==index+100);
    if (b==CUIS_RECEIPT_PREPARE) {
        uintptr_t high=(uintptr_t)record & ~(uintptr_t)0xffffffffUL;
        const WORD *message=(const WORD *)(high | record->packet);
        VERIFY(record->bytes==16 && slots[index].state==CUIS_RECEIPT_BOUND);
        VERIFY(message[0]==CH_EXIT && !message[1] && !message[2]);
        VERIFY(message[3]==slots[index].completion.pid);
        VERIFY((UWORD)message[4]==(slots[index].completion.kind==PROCESS_COMPLETION_SIGNAL ?
            (UWORD)slots[index].completion.signal<<8:slots[index].completion.status));
        for (i=5;i<8;i++) VERIFY(!message[i]);
        memcpy(slots[index].packet,message,16);
        slots[index].state=CUIS_RECEIPT_PREPARED;
        return E_OK;
    }
    if (b==CUIS_RECEIPT_COMMIT) {
        VERIFY(slots[index].state==CUIS_RECEIPT_PREPARED);
        if (publication_error) return publication_error;
        slots[index].state=CUIS_RECEIPT_COMMITTED;
        published++;
        return 16;
    }
    if (b==CUIS_RECEIPT_ACK) {
        VERIFY(slots[index].state==CUIS_RECEIPT_COMMITTED);
        slots[index].state=CUIS_RECEIPT_ACKNOWLEDGED;
        return E_OK;
    }
    VERIFY(b==CUIS_RECEIPT_RECLAIM && !slots[index].open);
    slots[index].state=CUIS_RECEIPT_RECLAIMED;
    return E_OK;
}
UQUAD aes_gemdos_call4(ULONG function,UQUAD a,UQUAD b,UQUAD c,UQUAD d)
{
    VERIFY(function==0x4b && a==PE_ASYNC_LOADGO);
    launches++; last_function=function;
    last[0]=a; last[1]=b; last[2]=c; last[3]=d;
    return 321;
}
UQUAD aes_gemdos_call(ULONG function,UQUAD mode,UQUAD address,UQUAD size)
{
    PROCESS_EXEC_REQUEST *request=(PROCESS_EXEC_REQUEST *)(uintptr_t)address;
    ULONG index=request->handle-100;
    VERIFY(function==0x154 && (QUAD)mode==PROCESS_EXEC_SSYSTEM && size==sizeof(*request));
    if (request->operation==PROCESS_EXEC_SNAPSHOT || request->operation==PROCESS_EXEC_CLOSE) {
        VERIFY(index<CUIS_RECEIPT_SLOTS);
        if (!slots[index].open) return (QUAD)EIHNDL;
        if (request->operation==PROCESS_EXEC_SNAPSHOT) {
            if (snapshot_error) return (QUAD)snapshot_error;
            request->completion=slots[index].completion;
        } else {
            slots[index].open=FALSE;
            closes++;
        }
        return E_OK;
    }
    VERIFY((request->operation==PROCESS_EXEC_DEFER || request->operation==PROCESS_EXEC_START) &&
           !request->policy && !request->handle);
    last_function=request->operation;
    last[0]=request->path; last[1]=request->tail;
    last[2]=request->environment; last[3]=request->directory;
    if (request->operation==PROCESS_EXEC_START) { launches++; return 123; }
    return E_OK;
}
static void child(unsigned index, WORD pid, ULONG high, ULONG low)
{
    slots[index].state=CUIS_RECEIPT_BOUND;
    slots[index].open=TRUE;
    slots[index].completion.pid=pid;
    slots[index].completion.state=PROCESS_COMPLETED;
    slots[index].completion.kind=PROCESS_COMPLETION_NORMAL;
    slots[index].completion.status=0xffff;
    slots[index].completion.order_high=high;
    slots[index].completion.order_low=low;
}
int main(void)
{
    const char path[]="CHILD.PRG", tail[]={0}, env[]={0,0}, directory[]="C:\\APPS";
    child(0,41,2,1);
    slots[0].completion.kind=PROCESS_COMPLETION_SIGNAL;
    slots[0].completion.signal=4;
    child(1,42,1,0xfffffffeUL);
    snapshot_error=EIMBA;
    VERIFY(aes_child_messages(session_id)==EIMBA && !published);
    snapshot_error=0;
    publication_error=EAGAIN;
    VERIFY(aes_child_messages(session_id)==E_OK && !published && slots[1].state==CUIS_RECEIPT_PREPARED &&
           slots[0].state==CUIS_RECEIPT_BOUND);
    publication_error=EIMBA;
    VERIFY(aes_child_messages(session_id)==EIMBA && !published);
    publication_error=0;
    VERIFY(aes_child_messages(session_id)==E_OK && published==2 && closes==2);
    VERIFY(aes_child_messages(session_id)==E_OK && published==2 && closes==2);
    child(2,43,3,1); slots[2].state=CUIS_RECEIPT_COMMITTED;
    child(3,44,3,2); slots[3].state=CUIS_RECEIPT_ACKNOWLEDGED; slots[3].open=FALSE;
    child(4,45,0,0); slots[4].completion.state=PROCESS_ABANDONED;
    VERIFY(aes_child_messages(session_id)==E_OK && published==2 && closes==4);
    VERIFY(slots[2].state==CUIS_RECEIPT_RECLAIMED && slots[3].state==CUIS_RECEIPT_RECLAIMED &&
           slots[4].state==CUIS_RECEIPT_RECLAIMED);
    launch_error=EAGAIN;
    VERIFY(aes_child_launch(session_id,path,tail,env,NULL,FALSE,TRUE)==EAGAIN && !launches);
    launch_error=0;
    VERIFY(aes_child_launch(session_id,path,tail,env,NULL,FALSE,TRUE)==123);
    VERIFY(last_function==CUIS_LAUNCH_ATTACH && last[1]==CUIS_LAUNCH_OBSERVE && launches==1);
    VERIFY(last[2]==(uintptr_t)path && last[3]==(uintptr_t)tail && last[4]==(uintptr_t)env && !last[5]);
    VERIFY(aes_child_launch(session_id,path,tail,env,directory,TRUE,TRUE)==123);
    VERIFY(last_function==CUIS_LAUNCH_ATTACH && last[1]==(CUIS_LAUNCH_OBSERVE | CUIS_LAUNCH_CONSOLE) &&
           last[5]==(uintptr_t)directory && launches==2);
    VERIFY(aes_child_launch(session_id,path,tail,env,directory,FALSE,FALSE)==123);
    VERIFY(last_function==PROCESS_EXEC_START && last[3]==(ULONG)(uintptr_t)directory && launches==3);
    VERIFY(aes_child_launch(session_id,path,tail,env,NULL,TRUE,FALSE)==123);
    VERIFY(last_function==CUIS_LAUNCH_ATTACH && last[1]==CUIS_LAUNCH_CONSOLE && !last[5] && launches==4);
    VERIFY(aes_child_launch(session_id,path,tail,env,NULL,FALSE,FALSE)==321);
    VERIFY(last_function==0x4b && launches==5);
    VERIFY(aes_child_defer(path,tail,env,directory)==E_OK);
    VERIFY(last_function==PROCESS_EXEC_DEFER && last[0]==(ULONG)(uintptr_t)path &&
           last[1]==(ULONG)(uintptr_t)tail && last[2]==(ULONG)(uintptr_t)env &&
           last[3]==(ULONG)(uintptr_t)directory);
    VERIFY(aes_child_defer(path,tail,env,NULL)==E_OK && !last[3]);
    puts("Production libaes CHILD policy: canonical status/order, failed publication, receipt recovery and launch routing: PASS");
    return 0;
}
