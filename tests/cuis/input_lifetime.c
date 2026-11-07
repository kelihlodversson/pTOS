/* Production admission/publication, retirement and cancellation cleanup. */
static CUIS_CLIENT *operation_client;
static LONG operation_handle, operation_replacement;
static int operation_action, operation_bad;
static UQUAD operation_sequence;
static void operation_retire(void)
{
    ULONG before=frees;
    if (cuis_service(CUIS_SESSION_END,operation_handle)) operation_bad=1;
    if (!operation_client->retired || operation_client->owner || operation_client->input_wait ||
        operation_client->references!=1 || frees!=before) operation_bad=1;
    cancel_pending=FALSE; /* replacement operation belongs to a different session */
    operation_replacement=cuis_service(CUIS_SESSION_START,0);
    if (operation_replacement<=operation_handle || *find_owner(run)==operation_client) operation_bad=1;
    if (cuis_session_wait(operation_replacement,0,CUIS_READY_TIMER,0)!=CUIS_READY_TIMER) operation_bad=1;
}
static void operation_hook(void)
{
    CUIS_INPUT_WAIT snapshot=operation_client->active_input;
    ULONG refs=operation_client->references;
    UQUAD seq=operation_client->wait_sequence;
    ULONG edges=operation_client->edge_count;
    BOOL resumed=operation_client->button_resume_active;
    if (refs!=2 || !operation_client->wait_operation ||
        operation_client->input_wait!=&operation_client->active_input) operation_bad=1;
    if (cuis_session_wait(operation_handle,0,CUIS_READY_KEY|CUIS_READY_TIMER,0)!=EACCDN ||
        cuis_session_wait(operation_handle,0,CUIS_READY_TIMER,0)!=EACCDN ||
        operation_client->input_wait!=&operation_client->active_input ||
        operation_client->references!=refs || operation_client->wait_sequence!=seq ||
        memcmp(&snapshot,&operation_client->active_input,sizeof(snapshot)) ||
        operation_client->edge_count!=edges || operation_client->button_resume_active!=resumed)
        operation_bad=1;
    if (operation_action==1) operation_retire();
    else if (operation_action==2) {
        /* Admitted cancellation drops publication before its checkpoint;
         * no later callback is needed for progress. */
        cancel_pending=TRUE;
    } else {
        /* New input arriving while an owner waits remains a normal event. */
        operation_client->keys[0]=0x12345678UL; operation_client->key_head=0;
        operation_client->key_count=1; keyboard_owner=operation_client; keyboard_depth=1;
    }
}
static int input_lifetime_checks(void)
{
    ULONG before;
    int cycle;
    run=&first;
    for (cycle=0;cycle<8;cycle++) {
        operation_handle=cuis_service(CUIS_SESSION_START,0);
        CHECK(operation_handle>=0);
        operation_client=*find_owner(run);
        cancel_pending=TRUE; before=allocations;
        CHECK(cuis_session_wait(operation_handle,0,CUIS_READY_KEY,0)==EACCDN);
        CHECK(!operation_client->wait_operation && !operation_client->input_wait &&
              operation_client->references==1 && !operation_client->wait_sequence && allocations==before);
        cancel_pending=FALSE;
        operation_client->references=0xffffffffUL;
        CHECK(cuis_session_wait(operation_handle,0,CUIS_READY_KEY,0)==EACCDN);
        CHECK(!operation_client->wait_operation && !operation_client->wait_sequence);
        operation_client->references=1;
        operation_client->wait_sequence=~0ULL;
        CHECK(cuis_session_wait(operation_handle,0,CUIS_READY_KEY,0)==ERANGE);
        CHECK(!operation_client->wait_operation && operation_client->references==1);
        operation_client->wait_sequence=0;
        fail_allocation=TRUE;
        CHECK(cuis_session_wait(operation_handle,0,CUIS_READY_TIMER,0)==CUIS_READY_TIMER);
        fail_allocation=FALSE;
        CHECK(allocations==before && !operation_client->wait_operation);
        operation_sequence=operation_client->wait_sequence;
        operation_action=0; input_operation_hook=operation_hook;
        CHECK(cuis_session_wait(operation_handle,0,CUIS_READY_KEY,0)==CUIS_READY_KEY);
        CHECK(!operation_bad && !operation_client->input_wait && !operation_client->wait_operation &&
              !operation_client->key_count && operation_client->input_result[0]==0x12345678UL &&
              operation_client->references==1 && operation_client->wait_sequence==operation_sequence+1);
        end_keyboard();
        /* A zero-timer yield must also retain and unpublish safely on retire. */
        operation_action=1; input_operation_hook=operation_hook;
        CHECK(cuis_session_wait(operation_handle,0,CUIS_READY_KEY|CUIS_READY_TIMER,0)==EACCDN);
        CHECK(!operation_bad && !input_operation_hook && (*find_owner(run))->handle==(ULONG)operation_replacement);
        CHECK(cuis_service(CUIS_SESSION_QUERY,operation_handle)==EACCDN);
        CHECK(cuis_service(CUIS_SESSION_END,operation_replacement)==0 && allocations==frees);
        operation_handle=cuis_service(CUIS_SESSION_START,0); operation_client=*find_owner(run);
        operation_action=2; input_operation_hook=operation_hook;
        CHECK(cuis_session_wait(operation_handle,0,CUIS_READY_KEY,0)==EACCDN);
        cancel_pending=FALSE;
        CHECK(!operation_bad && !input_operation_hook && input_irqs);
        CHECK(operation_client->references==1 && !operation_client->input_wait &&
              !operation_client->wait_operation);
        CHECK(cuis_service(CUIS_SESSION_END,operation_handle)==0 && allocations==frees);
    }
    __builtin_printf("CUIS exclusive combined input operation, owned sampler and retirement: PASS (host)\n");
    next_handle=next_window_handle=0; allocations=frees=0;
    return 0;
}
