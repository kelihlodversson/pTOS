/* Actual SESSION_WAIT preflight/predicate/cleanup; only PROCESS helpers modeled. */
#if CONF_WITH_CUIS_OBSERVER_TEST
/* Native admission injection is inactive in this hosted scheduler fixture. */
void x86_64_memtest_observer_wait_admitted(void) {}
static struct { ULONG leases; BOOL ready; } wait_targets[2];
static ULONG wait_imports, wait_releases;
static BOOL wait_array_copied, wait_right_closed;
static int wait_action;
static LONG wait_endpoint, wait_replacement;
static void (*wait_copy_hook)(void);
static BOOL wait_partial_copy;
LONG kproc_copy_from_user_checked(void *out,VADDR address,ULONG bytes)
{
    BOOL copied;
    wait_array_copied=FALSE;
    if (!input_irqs || !cleanup_depth) __builtin_abort();
    if (wait_copy_hook) { void (*hook)(void)=wait_copy_hook; wait_copy_hook=NULL; hook(); }
    if (wait_partial_copy) { memcpy(out,stream_input,4); return EIMBA; }
    copied=kproc_copy_from_user(out,address,bytes);
    wait_array_copied=copied;
    return copied ? E_OK:EIMBA;
}
LONG proc_exec_retain_observer(KPROC *owner,ULONG handle,BOOL prepared,PROCESS_OBSERVER **out)
{
    ULONG index;
#ifdef RECEIPT_RUNTIME_FIXTURE
    if (receipt_fixture_active) return receipt_fixture_import(owner,handle,prepared,out);
#endif
    if (!input_irqs || !cleanup_depth || !wait_array_copied || prepared || *out) __builtin_abort();
    if (owner!=(KPROC *)run) return EACCDN;
    if (handle==31) return EACCDN;
    if (handle==99 || (handle==17 && wait_right_closed)) return EIHNDL;
    if (handle!=17 && handle!=18 && handle!=19) return EIHNDL;
    index=handle==19;
    *out=(PROCESS_OBSERVER *)&wait_targets[index]; wait_targets[index].leases++; wait_imports++;
    return E_OK;
}
BOOL process_observer_ready(void *observer)
{
#ifdef RECEIPT_RUNTIME_FIXTURE
    if (receipt_fixture_active) return receipt_fixture_ready(observer);
#endif
    return observer==&wait_targets[0] ? wait_targets[0].ready:wait_targets[1].ready;
}
void process_observer_release(PROCESS_OBSERVER **out)
{
    ULONG index=*out==(PROCESS_OBSERVER *)&wait_targets[1];
#ifdef RECEIPT_RUNTIME_FIXTURE
    if (receipt_fixture_active) { receipt_fixture_release(out); return; }
#endif
    if (!input_irqs || !cleanup_depth || !*out || !wait_targets[index].leases) __builtin_abort();
    wait_targets[index].leases--; wait_releases++; *out=NULL;
}
static void observer_wait_hook(void)
{
    CUIS_CLIENT *client=owned_client(wait_endpoint);
    if (!client || !client->wait_operation || client->references!=2 || wait_targets[0].leases!=2)
        __builtin_abort();
    if (wait_action==1) cancel_pending=TRUE;
    else if (wait_action==2) {
        if (cuis_service(CUIS_SESSION_END,wait_endpoint)) __builtin_abort();
        wait_replacement=cuis_service(CUIS_SESSION_START,0);
    } else {
        wait_right_closed=TRUE;
        wait_targets[0].ready=TRUE; /* CLOSE releases right, not independent leases. */
    }
}
static void observer_retire_during_copy(void)
{
    if (cuis_service(CUIS_SESSION_END,wait_endpoint)) __builtin_abort();
    wait_replacement=cuis_service(CUIS_SESSION_START,0);
}
static int observer_wait_checks(void)
{
    ULONG handles[3]={17,18,19}, before, references, imported;
    LONG result;
    int action;
    run=&first; references=process_references;
    wait_endpoint=cuis_service(CUIS_SESSION_START,0); CHECK(wait_endpoint>=0);
    memcpy(stream_input,handles,sizeof(handles));
    CHECK(cuis_session_wait(wait_endpoint,0,CUIS_READY_CHILD,0,0x10000,17)==EINVAL);
    CHECK(cuis_session_wait(wait_endpoint,0,CUIS_READY_CHILD,0,0x10000,0)==EINVAL);
    CHECK(cuis_session_wait(wait_endpoint,0,CUIS_READY_TIMER,0,0x10000,1)==EINVAL);
    CHECK(cuis_session_wait(wait_endpoint,0,CUIS_READY_CHILD,0,0x10001,1)==EINVAL);
    CHECK(cuis_session_wait(wait_endpoint,0,CUIS_READY_CHILD,0,0x100010000ULL,1)==EIMBA);
    CHECK(cuis_session_wait(wait_endpoint,0,CUIS_READY_CHILD,0,0xfffffff0ULL,16)==EIMBA);
    CHECK(!wait_imports && !cleanup_depth && input_irqs);
    handles[1]=0; memcpy(stream_input,handles,sizeof(handles));
    CHECK(cuis_session_wait(wait_endpoint,0,CUIS_READY_CHILD,0,0x10000,2)==EINVAL && !wait_imports);
    handles[1]=17; memcpy(stream_input,handles,sizeof(handles));
    CHECK(cuis_session_wait(wait_endpoint,0,CUIS_READY_CHILD,0,0x10000,2)==EINVAL && !wait_imports);
    handles[1]=31; memcpy(stream_input,handles,sizeof(handles));
    CHECK(cuis_session_wait(wait_endpoint,0,CUIS_READY_CHILD,0,0x10000,2)==EACCDN);
    CHECK(wait_imports==1 && wait_releases==1 && !wait_targets[0].leases);
    handles[1]=99; memcpy(stream_input,handles,sizeof(handles));
    CHECK(cuis_session_wait(wait_endpoint,0,CUIS_READY_CHILD,0,0x10000,2)==EIHNDL);
    CHECK(wait_imports==2 && wait_releases==2 && !wait_targets[0].leases);
    handles[1]=18; memcpy(stream_input,handles,sizeof(handles)); before=wait_imports;
    wait_partial_copy=TRUE;
    CHECK(cuis_session_wait(wait_endpoint,0,CUIS_READY_CHILD,0,0x10000,2)==EIMBA && wait_imports==before);
    wait_partial_copy=FALSE;
    for (action=0;action<3;action++) {
        wait_action=action; wait_right_closed=FALSE; wait_targets[0].ready=FALSE;
        wait_array_copied=FALSE; lifetime_wait_hook=observer_wait_hook;
        result=cuis_session_wait(wait_endpoint,0,CUIS_READY_CHILD|CUIS_READY_TIMER,100,0x10000,2);
        CHECK(result==(action ? EACCDN:(LONG)CUIS_READY_CHILD));
        CHECK(!lifetime_wait_hook && !wait_targets[0].leases && wait_imports==wait_releases &&
              !cleanup_depth && input_irqs && !cancellation_bad);
        cancel_pending=FALSE;
        if (action==2) { CHECK(wait_replacement>wait_endpoint); wait_endpoint=wait_replacement; }
    }
    /* READY is level triggered and combines without consuming stream/key data. */
    wait_right_closed=FALSE; wait_targets[0].ready=TRUE;
    CHECK(cuis_stream_service(CUIS_STREAM_WRITE,wait_endpoint,0x10000,16,0,0)==16);
    result=cuis_session_wait(wait_endpoint,16,CUIS_READY_CHILD|CUIS_READY_STREAM|CUIS_READY_TIMER,0,0x10000,2);
    CHECK(result==(CUIS_READY_CHILD|CUIS_READY_STREAM|CUIS_READY_TIMER));
    CHECK(owned_client(wait_endpoint)->stream_bytes==16 && !wait_targets[0].leases);
    CHECK(cuis_session_wait(wait_endpoint,0,CUIS_READY_CHILD,0,0x10000,2)==CUIS_READY_CHILD);
    /* Exact explicit endpoint belongs to the same PROCESS, not its default. */
    {
        LONG explicit=cuis_service(CUIS_SESSION_START,CUIS_SESSION_NEW);
        CHECK(explicit>wait_endpoint);
        CHECK(cuis_session_wait(explicit,0,CUIS_READY_CHILD,0,0x10000,2)==CUIS_READY_CHILD);
        CHECK(cuis_service(CUIS_SESSION_END,explicit)==E_OK);
    }
    /* Fallible copy may retire/recreate the exact source endpoint. */
    imported=wait_imports; wait_copy_hook=observer_retire_during_copy;
    CHECK(cuis_session_wait(wait_endpoint,0,CUIS_READY_CHILD,0,0x10000,2)==EACCDN);
    CHECK(wait_replacement>wait_endpoint && wait_imports==wait_releases &&
          !cleanup_depth && input_irqs && !wait_copy_hook);
    (void)imported;
    CHECK(cuis_service(CUIS_SESSION_END,wait_replacement)==E_OK);
    CHECK(process_references==references && !wait_targets[0].leases && !wait_targets[1].leases);
    CHECK(!clients && allocations==frees);
    next_handle=0; allocations=frees=stream_ring_count=0;
    return 0;
}
#else
static int observer_wait_checks(void)
{
    LONG session=cuis_service(CUIS_SESSION_START,0);
    CHECK(session>=0);
    CHECK(cuis_session_wait(session,0,CUIS_READY_CHILD,0,0x10000,1)==EINVFN);
    CHECK(cuis_service(CUIS_SESSION_END,session)==E_OK);
    CHECK(!clients && allocations==frees);
    next_handle=0; allocations=frees=0;
    return 0;
}
#endif
