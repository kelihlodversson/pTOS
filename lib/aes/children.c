/* GEM child launch/notification adapter; shell policy stays in shell.c. */
#include "emutos.h"
#include "libaes.h"
#include "application.h"
#include "transport.h"
#include "gemerror.h"
#include "bdosdefs.h"
#include "process_exec.h"
#include "cuis_receipt.h"

static LONG process_request(PROCESS_EXEC_REQUEST *request)
{
    return (LONG)aes_gemdos_call(0x154,(UQUAD)(QUAD)PROCESS_EXEC_SSYSTEM,
                                  (uintptr_t)request,sizeof(*request));
}
static LONG receipt_call(LONG session, ULONG action, CUIS_RECEIPT_RECORD *receipt)
{
    return cuis_call6(CUIS_STREAM_RECEIPT,session,action,(uintptr_t)receipt,sizeof(*receipt),0,0);
}
static BOOL receipt_retry(LONG result)
{
    return result==EAGAIN || result==EIHNDL;
}
static LONG settle_receipt(LONG session, CUIS_RECEIPT_RECORD *receipt)
{
    PROCESS_EXEC_REQUEST request={0};
    LONG result;
    if (receipt->state==CUIS_RECEIPT_COMMITTED) {
        result=receipt_call(session,CUIS_RECEIPT_ACK,receipt);
        if (result!=E_OK) return result;
    }
    request.operation=PROCESS_EXEC_CLOSE;
    request.handle=receipt->tag;
    /* Reclaim independently checks that the generic right no longer exists.
     * CLOSE may report a cleanup failure after consuming the exact right. */
    (void)process_request(&request);
    return receipt_call(session,CUIS_RECEIPT_RECLAIM,receipt);
}
static LONG find_notice(LONG session, CUIS_RECEIPT_RECORD *selected, PROCESS_COMPLETION *completed)
{
    UQUAD earliest=~(UQUAD)0;
    ULONG i;
    for (i=0;i<CUIS_RECEIPT_SLOTS;i++) {
        PROCESS_EXEC_REQUEST request={0};
        CUIS_RECEIPT_RECORD receipt={0};
        UQUAD order;
        LONG result;
        receipt.source=receipt.source_low=(ULONG)session;
        receipt.slot=i;
        result=receipt_call(session,CUIS_RECEIPT_QUERY,&receipt);
        if (result!=E_OK) return result;
        if ((receipt.state & ~CUIS_RECEIPT_COMMITTED_OUTCOME)==CUIS_RECEIPT_RECLAIMED ||
            receipt.state==CUIS_RECEIPT_UNBOUND) continue;
        if (receipt.state < CUIS_RECEIPT_COMMITTED) {
            request.operation=PROCESS_EXEC_SNAPSHOT;
            request.handle=receipt.tag;
            result=process_request(&request);
            if (result!=EIHNDL && result!=E_OK) return result;
            if (result==E_OK && request.completion.state!=PROCESS_ABANDONED) {
                order=((UQUAD)request.completion.order_high << 32) | request.completion.order_low;
                if (request.completion.state==PROCESS_COMPLETED && order && (!selected->tag || order<earliest)) {
                    *selected=receipt;
                    *completed=request.completion;
                    earliest=order;
                }
                continue;
            }
        }
        result=settle_receipt(session,&receipt);
        if (result!=E_OK && !receipt_retry(result)) return result;
    }
    return selected->tag ? 1 : E_OK;
}

LONG aes_child_messages(LONG session)
{
    ULONG i;
    for (i=0;i<CUIS_RECEIPT_SLOTS;i++) {
        CUIS_RECEIPT_RECORD receipt={0};
        PROCESS_COMPLETION completion={0};
        short message[8]={CH_EXIT,0,0,0,0,0,0,0};
        LONG result=find_notice(session,&receipt,&completion);
        if (result<=0) return result;
        message[3]=(short)completion.pid;
        message[4]=(short)(completion.kind==PROCESS_COMPLETION_SIGNAL ?
                           (UWORD)completion.signal << 8 : completion.status);
        receipt.packet=(ULONG)(uintptr_t)message;
        receipt.bytes=sizeof(message);
        if (receipt.state==CUIS_RECEIPT_BOUND) {
            result=receipt_call(session,CUIS_RECEIPT_PREPARE,&receipt);
            if (receipt_retry(result)) return E_OK;
            if (result!=E_OK) return result;
        }
        result=receipt_call(session,CUIS_RECEIPT_COMMIT,&receipt);
        if (receipt_retry(result)) return E_OK;
        if (result<0) return result;
        receipt.state=CUIS_RECEIPT_COMMITTED;
        result=settle_receipt(session,&receipt);
        if (receipt_retry(result)) return E_OK;
        if (result!=E_OK) return result;
    }
    return E_OK;
}

/* Both attaches and unobserved launches use the generic PROCESS decoder. */
LONG aes_child_launch(LONG session, PROCESS_EXEC_REQUEST *request, BOOL console, BOOL observed)
{
    UQUAD authorization = CUIS_LAUNCH_VECTORS | (console ? CUIS_LAUNCH_CONSOLE : 0);
    if (!observed && !console) {
        request->operation = PROCESS_EXEC_START;
        return process_request(request);
    }
    if (observed) {
        LONG result = aes_child_messages(session);
        if (result < 0) return result;
        authorization |= CUIS_LAUNCH_OBSERVE;
    }
    request->operation = PROCESS_EXEC_PREPARE;
    return cuis_call6(CUIS_LAUNCH_ATTACH,session,authorization,
                      (uintptr_t)request,sizeof(*request),0,0);
}
LONG aes_child_defer(PROCESS_EXEC_REQUEST *request)
{
    request->operation = PROCESS_EXEC_DEFER;
    return process_request(request);
}
