#ifndef PROCESS_EXEC_REQUEST_CASES_H
#define PROCESS_EXEC_REQUEST_CASES_H
/* Production request decoder with modeled faultable user memory. */
#include "../bdos/process_exec_request.c"
static union { ULONG align; UBYTE bytes[65536]; } request_memory;
static ULONG read_failure, output_prefix;
static BOOL output_failure;
static LONG input_error=EIMBA;
static unsigned output_copies;
BOOL kproc_copy_from_user(void *out,VADDR address,ULONG bytes)
{
    if(address>sizeof(request_memory.bytes) || bytes>sizeof(request_memory.bytes)-address) return FALSE;
    if(read_failure && address<=read_failure && bytes>read_failure-address) return FALSE;
    memcpy(out,request_memory.bytes+(ULONG)address,bytes); return TRUE;
}
BOOL kproc_copy_to_user(VADDR address,const void *in,ULONG bytes)
{
    output_copies++;
    if(address>sizeof(request_memory.bytes) || bytes>sizeof(request_memory.bytes)-address) return FALSE;
    if(output_failure) {
        CHECK(output_prefix<=bytes);
        memcpy(request_memory.bytes+(ULONG)address,in,output_prefix); return FALSE;
    }
    memcpy(request_memory.bytes+(ULONG)address,in,bytes); return TRUE;
}
LONG kproc_copy_from_user_checked(void *out,VADDR address,ULONG bytes)
{ return kproc_copy_from_user(out,address,bytes) ? E_OK : input_error; }
LONG kproc_copy_to_user_checked(VADDR address,const void *in,ULONG bytes)
{ return kproc_copy_to_user(address,in,bytes) ? E_OK : EIMBA; }
static PROCESS_EXEC_REQUEST *request_fixture(void)
{
    PROCESS_EXEC_REQUEST *r=(PROCESS_EXEC_REQUEST *)(request_memory.bytes+0x1000);
    memset(&request_memory,0,sizeof(request_memory));
    r->path=0x1100; r->tail=0x1200; r->environment=0x1300; r->directory=0x1400;
    memcpy(request_memory.bytes+r->path,"program",8);
    memcpy(request_memory.bytes+r->tail,"tail",5);
    memcpy(request_memory.bytes+r->environment,"A=B\0",5);
    memcpy(request_memory.bytes+r->directory,"C:\\DIR",7);
    run->p_env=PTR_TO_USERPTR_UNCHECKED((char *)(uintptr_t)r->environment);
    read_failure=0; input_error=EIMBA; output_failure=FALSE; output_prefix=0; output_copies=0;
    return r;
}
static void test_request_engine(KPROC *owner)
{
    PROCESS_EXEC_REQUEST *r;
    PROCESS_COMPLETION completed;
    LONG handle;
    unsigned i, before_loads=loads, before_publications=publications;
    request_mode=TRUE;
    r=request_fixture();
    CHECK(proc_exec_request(owner,0x1000,71)==EINVAL);
    CHECK(proc_exec_request(owner,0x1000,0x100000048ULL)==EINVAL);
    CHECK(proc_exec_request(owner,0x1001,72)==EINVAL);
    CHECK(proc_exec_request(owner,0xfffffff0ULL,72)==EIMBA);
    CHECK(proc_exec_request(owner,0x100001000ULL,72)==EIMBA);
    r->reserved=1; CHECK(proc_exec_request(owner,0x1000,72)==EINVAL);
    r->reserved=0; r->reserved_tail[1]=1; CHECK(proc_exec_request(owner,0x1000,72)==EINVAL);
    r->reserved_tail[1]=0; r->handle=5; CHECK(proc_exec_request(owner,0x1000,72)==EINVAL);
    r->handle=0; r->policy=2; CHECK(proc_exec_request(owner,0x1000,72)==EINVAL);
    r->policy=0; r->operation=999; CHECK(proc_exec_request(owner,0x1000,72)==EINVFN);
    for(i=0;i<5;i++) {
        r=request_fixture(); read_failure=i ? 0x1000+i*0x100 : 0x1020;
        CHECK(proc_exec_request(owner,0x1000,72)==EIMBA && allocations==frees && loads==before_loads);
    }
    r=request_fixture(); memset(request_memory.bytes+r->path,'x',1024);
    CHECK(proc_exec_request(owner,0x1000,72)==ERANGE && allocations==frees);
    r=request_fixture(); memset(request_memory.bytes+r->tail,'x',PDCLSIZE);
    CHECK(proc_exec_request(owner,0x1000,72)==ERANGE && allocations==frees);
    r=request_fixture(); memset(request_memory.bytes+r->environment,'x',32766);
    CHECK(proc_exec_request(owner,0x1000,72)==ERANGE && allocations==frees);
    for(i=0;i<5;i++) {
        r=request_fixture(); fail_after=(int)i;
        CHECK(proc_exec_request(owner,0x1000,72)==ENSMEM && allocations==frees && loads==before_loads);
    }
    fail_after=-1; r=request_fixture(); r->environment=0; /* bounded inherited environment */
    output_failure=TRUE; /* PREPARE accepts readable, non-writable request memory. */
    handle=proc_exec_request(owner,0x1000,72);
    CHECK(handle>0 && !output_copies && loads==before_loads+1 && publications==before_publications);
    memset(r,0,sizeof(*r)); r->operation=PROCESS_EXEC_CANCEL_DEFERRED; r->handle=handle;
    CHECK(proc_exec_request(owner,0x1000,72)==EINVAL && !cancel_requests);
    r->handle=0;
    CHECK(proc_exec_request(owner,0x1000,72)==E_OK && cancel_requests==1);
    r->operation=PROCESS_EXEC_SNAPSHOT; r->handle=handle;
    memset(&r->completion,0xaa,sizeof(r->completion)); output_prefix=0;
    CHECK(proc_exec_request(owner,0x1000,72)==EIMBA && r->completion.state==0xaaaaaaaaUL);
    output_prefix=16;
    CHECK(proc_exec_request(owner,0x1000,72)==EIMBA && r->operation==PROCESS_EXEC_SNAPSHOT && r->handle==(ULONG)handle);
    output_failure=FALSE;
    CHECK(proc_exec_request(owner,0x1000,72)==E_OK && r->completion.state==PROCESS_UNSTARTED);
    r->operation=PROCESS_EXEC_COMMIT; output_failure=TRUE; i=output_copies;
    CHECK(proc_exec_request(owner,0x1000,72)==42 && output_copies==i);
    CHECK(proc_exec_request(owner,0x1000,72)==EACCDN && publications==before_publications+1);
    children[before_loads].resources=FALSE; children[before_loads].refs--;
    children[before_loads].completion.state=PROCESS_COMPLETED;
    children[before_loads].completion.kind=PROCESS_COMPLETION_NORMAL;
    children[before_loads].completion.status=0xffff; children[before_loads].completion.order_low=7;
    r->operation=PROCESS_EXEC_SNAPSHOT;
    CHECK(proc_exec_request(owner,0x1000,72)==EIMBA);
    output_failure=FALSE;
    CHECK(proc_exec_request(owner,0x1000,72)==E_OK && r->completion.status==0xffff && r->completion.order_low==7);
    completed=r->completion;
    CHECK(proc_exec_request(owner,0x1000,72)==E_OK && !memcmp(&r->completion,&completed,sizeof(completed)));
    r->operation=PROCESS_EXEC_CLOSE; i=output_copies;
    CHECK(proc_exec_request(owner,0x1000,72)==E_OK && output_copies==i && allocations==frees);
    CHECK(proc_exec_request(owner,0x1000,72)==EIHNDL && !children[before_loads].refs);
    for (i=0;i<2;i++) {
        r=request_fixture(); read_failure=r->environment+2;
        input_error=i ? EREADF:ENSMEM;
        CHECK(proc_exec_request(owner,0x1000,72)==input_error && allocations==frees && !cleanup_nesting);
    }
    request_mode=FALSE;
    puts("Production PROCESS request: bounded copies, malformed ABI, partial copyout, scalar commit and retryable completion: PASS");
}

#endif
