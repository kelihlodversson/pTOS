/* Actual dispatch + decoder, with modeled PROCESS and checked user memory. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "gemerror.h"
#include "string.h"
#include "../bdos/process_exec_dispatch.c"
#include "../bdos/process_exec_request.c"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"dispatch:%d: %s\n",__LINE__,#x); abort(); } } while(0)
static PD caller;
PD *run=&caller;
static unsigned depth, refs, copies, releases, closes, snapshots;
static BOOL deny_owner, fail_output, pending;
static UQUAD copied_address;
static PROCESS_EXEC_REQUEST wire;
void ksched_cleanup_begin(KSCHED_CLEANUP_SCOPE *s)
{ CHECK(!s->active); s->active=TRUE; s->depth=++depth; }
void ksched_cleanup_end(KSCHED_CLEANUP_SCOPE *s)
{ CHECK(s->active && s->depth==depth && (depth>1 || !refs)); s->active=FALSE; depth--; }
/* Inner backend scope ends while the outer adapter still retains its owner. */
KPROC *kproc_retain(PD *pd)
{ CHECK(pd==run && depth==1 && !refs); if(deny_owner)return NULL; refs++; return (KPROC *)&caller; }
void kproc_release(KPROC *p)
{ CHECK(p==(KPROC *)&caller && depth==1 && refs==1); refs--; releases++; }
PD *kproc_live_pd(const KPROC *p) { CHECK(p==(KPROC *)&caller); return run; }
void *kalloc(ULONG bytes) { (void)bytes; abort(); }
void kfree(void *p) { (void)p; abort(); }
LONG kproc_copy_from_user_checked(void *out,VADDR address,ULONG size)
{
    CHECK(depth==2 && refs==1); copies++; copied_address=address;
    if(address!=0x1000 || size!=sizeof(wire))return EIMBA;
    memcpy(out,&wire,size); return E_OK;
}
LONG kproc_copy_to_user_checked(VADDR address,const void *in,ULONG size)
{
    CHECK(depth==2 && refs==1 && address==0x1020 && size==32);
    if(fail_output)return EIMBA;
    memcpy(&wire.completion,in,size); return E_OK;
}
LONG proc_exec_handle_prepare(KPROC *o,char *p,char *t,char *e,char *d,ULONG policy)
{ (void)o;(void)p;(void)t;(void)e;(void)d;(void)policy; abort(); }
LONG proc_exec_handle_commit(KPROC *o,ULONG h)
{ CHECK(o==(KPROC *)&caller && h==17 && depth==2 && refs==1); pending=TRUE; return 42; }
LONG proc_exec_handle_close(KPROC *o,ULONG h)
{ CHECK(o==(KPROC *)&caller && h==17 && depth==2 && refs==1); closes++; pending=TRUE; return EWRITF; }
LONG proc_exec_handle_snapshot(KPROC *o,ULONG h,PROCESS_COMPLETION *out)
{
    CHECK(o==(KPROC *)&caller && h==17 && depth==2 && refs==1); snapshots++;
    memset(out,0,sizeof(*out)); out->state=PROCESS_COMPLETED;
    out->kind=PROCESS_COMPLETION_NORMAL; out->status=0xffff; out->order_low=7;
    return E_OK;
}
LONG ksched_defer_cancel(void) { CHECK(depth==2 && refs==1); return E_OK; }
static LONG call(long address,long size)
{
    LONG result=proc_exec_dispatch(address,size);
    CHECK(!depth && !refs); return result;
}
int main(void)
{
    unsigned before;
    memset(&wire,0,sizeof(wire)); wire.operation=PROCESS_EXEC_CLOSE; wire.handle=17;
    CHECK(call(0x1000,71)==EINVAL && !copies);
    CHECK(call(0x1000,-1L)==EINVAL && !copies);
    CHECK(call(0,72)==EIMBA && !copies);
    CHECK(call(0x1001,72)==EINVAL && !copies);
#if __SIZEOF_LONG__ > 4
    CHECK(call((long)0x100001000ULL,72)==EIMBA && !copies);
    CHECK(call(0x1000,(long)0x100000048ULL)==EINVAL && !copies);
#endif
    CHECK(call((long)0x80000000UL,72)==EIMBA && copied_address==0x80000000ULL);
    CHECK(call(0x1000,72)==EWRITF && closes==1 && pending);
    /* The adapter never clears pending cancellation or invokes a checkpoint.
     * Its caller's established osif boundary owns delivery after all cleanup. */
    wire.operation=PROCESS_EXEC_COMMIT;
    CHECK(call(0x1000,72)==42 && pending);
    wire.operation=PROCESS_EXEC_SNAPSHOT; fail_output=TRUE;
    CHECK(call(0x1000,72)==EIMBA && snapshots==1);
    fail_output=FALSE;
    CHECK(call(0x1000,72)==E_OK && snapshots==2 && wire.completion.status==0xffff);
    deny_owner=TRUE; before=copies;
    CHECK(call(0x1000,72)==EACCDN && copies==before && releases>0);
    puts("PROCESS dispatch: native-width bounds, protected owner lifetime, pending cancellation, consumed error and snapshot retry: PASS");
    return 0;
}
