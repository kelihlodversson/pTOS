/* Real producer/sampler/commit paths: no queue snapshots are restored. */
static CUIS_CLIENT *transaction_client;
static LONG transaction_session;
static int transaction_action, transaction_bad;
static UQUAD transaction_first, transaction_last;
static void transaction_hook(void)
{
    ULONG i;
    if (!transaction_client->wait_operation || transaction_client->references!=2 ||
        transaction_client->input_wait!=&transaction_client->active_input) transaction_bad=1;
    if (!cuis_pointer_packet(0,0,1,NULL,NULL) || !cuis_pointer_packet(0,0,0,NULL,NULL)) transaction_bad=1;
    transaction_first=transaction_client->button_sequence-1;
    transaction_last=transaction_client->button_sequence;
    if (transaction_client->edge_count!=2 || transaction_client->active_input.count!=1 ||
        transaction_client->active_input.history_first!=transaction_first) transaction_bad=1;
    if (transaction_action==1) {
        if (cuis_input_config(CUIS_INPUT_BUTTON,transaction_session,1,0,1,0,0)) transaction_bad=1;
    } else if ((transaction_action==2 || transaction_action==5)) {
        for (i=0;i<CUIS_BUTTON_HISTORY_CAPACITY;i++)
            if (!cuis_pointer_packet(0,0,!(i&1),NULL,NULL)) transaction_bad=1;
        transaction_last=transaction_client->button_sequence;
        transaction_client->keys[0]=0x12345678UL; transaction_client->key_head=0;
        transaction_client->key_count=1; keyboard_owner=transaction_client; keyboard_depth=1;
        if (transaction_action==5) cancel_pending=TRUE;
    } else if (transaction_action==3) {
        if (cuis_mouse_lock(transaction_session,CUIS_MOUSE_FRESH,0)) transaction_bad=1;
        if (transaction_client->saved_count!=2 || transaction_client->edge_count) transaction_bad=1;
        if (!cuis_pointer_packet(0,0,1,NULL,NULL) || !cuis_pointer_packet(0,0,0,NULL,NULL)) transaction_bad=1;
    } else if ((transaction_action==4 || transaction_action==6)) {
        transaction_client->keys[0]=0x12345678UL; transaction_client->key_head=0;
        transaction_client->key_count=1; keyboard_owner=transaction_client; keyboard_depth=1;
        if (transaction_action==6) input_ticks+=1000; /* Button and timer become ready too. */
        cancel_pending=TRUE; /* Readiness must lose before commit. */
    } else {
        /* A legitimate policy invalidation aborts before input commit. */
        if (cuis_input_config(CUIS_INPUT_BUTTON,transaction_session,3,3,1,0,0)) transaction_bad=1;
    }
}
static int input_transaction_checks(void)
{
    LONG window, result;
    int action, cycle;
    ULONG saved_ticks=input_ticks, saved_sleeps=input_sleeps, saved_interval=input_interval, visits;
    UQUAD sequence;
    run=&first;
    for (cycle=0;cycle<3;cycle++) for (action=0;action<7;action++) {
        transaction_session=cuis_service(CUIS_SESSION_START,0);
        CHECK(transaction_session>=0); transaction_client=*find_owner(run);
        pointer_buttons=0; pointer_suppressed=pointer_legacy_drag=FALSE; input_pointer_source=FALSE;
        window=cuis_window_service(CUIS_WINDOW_CREATE,0,0,300,300,0);
        CHECK(window>=0 && !cuis_window_service(CUIS_WINDOW_POLICY,window,1,0,0,0) &&
              !cuis_window_service(CUIS_WINDOW_SHOW,window,0,0,0,0));
        refresh_pointer_bounds();
        CHECK(cuis_pointer_packet((WORD)(10-pointer_x),(WORD)(10-pointer_y),0,NULL,NULL));
        CHECK(cuis_input_config(CUIS_INPUT_BUTTON,transaction_session,1,1,2,0,0)==0);
        CHECK(cuis_input_config(CUIS_INPUT_INTERVAL,transaction_session,1000,1,0,0,0)==1000);
        transaction_client->input_result[0]=123; transaction_client->input_result[1]=456;
        cancellation_owner=transaction_client;
        visits=cancellation_visits; cancel_on_wait=1;
        CHECK(cuis_session_wait(transaction_session,0,CUIS_READY_BUTTON,0,0,0)==EACCDN &&
              !cancel_on_wait && cancellation_visits==visits+1 && !cancellation_bad &&
              !transaction_client->wait_operation && !transaction_client->input_wait &&
              transaction_client->references==1 && !transaction_client->edge_count &&
              transaction_client->input_result[0]==123 && transaction_client->input_result[1]==456);
        visits=cancellation_visits;
        transaction_action=action; input_operation_hook=transaction_hook;
        result=cuis_session_wait(transaction_session,0,CUIS_READY_BUTTON|CUIS_READY_KEY|
                                 (action==6?CUIS_READY_TIMER:0),action==6?1:0,0,0);
        cancel_pending=FALSE; cancellation_owner=NULL;
        CHECK(result==(action==0||action==1||action==3?EAGAIN:action==2?ERANGE:EACCDN));
        CHECK(cancellation_visits==visits+(action>=4) && !cancellation_bad);
        CHECK(!transaction_bad && !input_operation_hook && input_irqs &&
              !transaction_client->wait_operation && !transaction_client->input_wait &&
              transaction_client->references==1 && transaction_client->input_result[0]==123 &&
              transaction_client->input_result[1]==456);
        if (action==2 || action==4 || action==5 || action==6) {
            CHECK(transaction_client->key_count==1);
            CHECK(cuis_session_wait(transaction_session,0,CUIS_READY_KEY,0,0,0)==CUIS_READY_KEY &&
                  transaction_client->input_result[0]==0x12345678UL && !transaction_client->key_count);
            end_keyboard();
        }
        if (action==3) {
            CHECK(transaction_client->saved_count==2 && transaction_client->edge_count==2);
            CHECK(cuis_input_config(CUIS_INPUT_BUTTON,transaction_session,1,1,1,0,0)==0);
            CHECK(cuis_session_wait(transaction_session,0,CUIS_READY_BUTTON,0,0,0)==CUIS_READY_BUTTON);
            CHECK(cuis_input_config(CUIS_INPUT_BUTTON,transaction_session,1,0,1,0,0)==0);
            CHECK(cuis_session_wait(transaction_session,0,CUIS_READY_BUTTON,0,0,0)==CUIS_READY_BUTTON &&
                  !transaction_client->edge_count);
            CHECK(cuis_mouse_lock(transaction_session,0,0)==0);
            CHECK(transaction_client->edge_count==2 &&
                  transaction_client->edge_sequence[transaction_client->edge_head]==transaction_first);
        }
        CHECK(transaction_client->edge_count==((action==2||action==5)?CUIS_POINTER_CAPACITY:2));
        if (!action) CHECK(cuis_pointer_packet(0,0,1,NULL,NULL)); /* New IRQ after rollback. */
        sequence=transaction_client->edge_sequence[transaction_client->edge_head];
        CHECK(sequence==((action==2||action==5)?transaction_last-CUIS_POINTER_CAPACITY+1:transaction_first));
        /* Retrying reads the actual journal payload selected by this view,
         * including older normal input restored after newer modal commits. */
        CHECK(cuis_input_config(CUIS_INPUT_BUTTON,transaction_session,1,1,1,0,0)==0);
        CHECK(cuis_session_wait(transaction_session,0,CUIS_READY_BUTTON,0,0,0)==CUIS_READY_BUTTON &&
              transaction_client->input_result[1]==1 && (transaction_client->input_result[4]&1));
        CHECK(transaction_client->edge_sequence[transaction_client->edge_head]==sequence+1);
        CHECK(cuis_input_config(CUIS_INPUT_BUTTON,transaction_session,1,0,1,0,0)==0);
        CHECK(cuis_session_wait(transaction_session,0,CUIS_READY_BUTTON,0,0,0)==CUIS_READY_BUTTON &&
              !(transaction_client->input_result[4]&1));
        if (!action) {
            CHECK(transaction_client->edge_count==1 &&
                  transaction_client->edge_sequence[transaction_client->edge_head]==transaction_last+1);
            CHECK(cuis_input_config(CUIS_INPUT_BUTTON,transaction_session,1,1,1,0,0)==0);
            CHECK(cuis_session_wait(transaction_session,0,CUIS_READY_BUTTON,0,0,0)==CUIS_READY_BUTTON &&
                  !transaction_client->edge_count);
            cancel_pending=TRUE;
            CHECK(cuis_session_wait(transaction_session,0,CUIS_READY_BUTTON,0,0,0)==EACCDN &&
                  !transaction_client->edge_count && transaction_client->input_result[1]==1);
            cancel_pending=FALSE; /* A later request cannot retract committed input. */
        }
        CHECK(cuis_service(CUIS_SESSION_END,transaction_session)==0 && allocations==frees);
    }
    input_ticks=saved_ticks; input_sleeps=saved_sleeps; input_interval=saved_interval;
    next_handle=next_window_handle=0; allocations=frees=0;
    __builtin_printf("CUIS transactional cancellation, key/overflow precedence, newer IRQ eligibility and modal view commit: PASS (host)\n");
    return 0;
}
