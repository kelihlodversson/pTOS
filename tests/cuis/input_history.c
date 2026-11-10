/* Production routed producer, observational cursors and tentative quick check. */
static void history_reject_observation(void)
{
    (*find_owner(run))->active_input.history_read=BUTTON_HISTORY_GAP;
}
static int input_history_checks(void)
{
    CUIS_CLIENT *client;
    CUIS_BUTTON_CURSOR cursor, saved;
    CUIS_BUTTON_EDGE edge;
    CUIS_INPUT_WAIT tentative;
    CUIS_POINTER event;
    UQUAD cached_sequences[CUIS_POINTER_CAPACITY];
    LONG session, window, replacement;
    ULONG i, count, head, allocations_before;
    UQUAD sequence, policy;
    void *storage;
    run=&first;
    session=cuis_service(CUIS_SESSION_START,0);
    CHECK(session>=0);
    client=*find_owner(run);
    input_pointer_source=FALSE; pointer_buttons=0;
    pointer_suppressed=pointer_legacy_drag=FALSE;
    window=cuis_window_service(CUIS_WINDOW_CREATE,0,0,100,100,0);
    CHECK(window>=0 && cuis_window_service(CUIS_WINDOW_POLICY,window,1,0,0,0)==0);
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW,window,0,0,0,0)==0);
    CHECK(cuis_pointer_packet((WORD)(10-pointer_x),(WORD)(10-pointer_y),0,NULL,NULL));
    CHECK(cuis_input_config(CUIS_INPUT_BUTTON,session,1,1,1,0,0)==0);
    CHECK(cuis_pointer_packet(0,0,1,NULL,NULL) && cuis_pointer_packet(0,0,0,NULL,NULL));
    CHECK(client->history_count==2 && client->button_sequence==2 && client->edge_count==2);
    count=client->edge_count; head=client->edge_head;
    button_history_begin(client,&cursor);
    for (i=0;i<2;i++) {
        CHECK(button_history_next(client,&cursor,&edge)==BUTTON_HISTORY_EVENT);
        CHECK(edge.sequence==i+1 && edge.event.x==10 && edge.event.buttons==!i);
    }
    CHECK(button_history_next(client,&cursor,&edge)==BUTTON_HISTORY_EMPTY);
    CHECK(client->edge_head==head && client->edge_count==count);
    memset(&tentative,0,sizeof(tentative)); tentative.events=CUIS_READY_BUTTON;
    sample_input(client,&tentative,TRUE);
    CHECK((tentative.ready&CUIS_READY_BUTTON) && tentative.buttons==1 && tentative.history_through==1);
    CHECK(client->edge_head==head && client->edge_count==count);
    /* Later IRQ data must survive committing precisely the observed prefix. */
    CHECK(cuis_pointer_packet(0,0,1,NULL,NULL));
    commit_initial_history(client,&tentative);
    CHECK(client->edge_count==2 && client->edge_sequence[client->edge_head]==2);
    CHECK(client->button_edges[client->edge_head].buttons==0);
    commit_initial_history(client,&tentative);
    CHECK(client->edge_count==2); /* consumed once */
    CHECK(cuis_input_config(CUIS_INPUT_BUTTON,session,1,0,1,0,0)==0);
    memset(&tentative,0,sizeof(tentative)); tentative.events=CUIS_READY_BUTTON;
    sample_input(client,&tentative,TRUE);
    CHECK(tentative.ready&CUIS_READY_BUTTON);
    head=client->edge_head; count=client->edge_count;
    CHECK(cuis_pointer_packet(0,0,0,NULL,NULL));
    /* Abandon the local observation, never restore a previous head/count. */
    memset(&tentative,0,sizeof(tentative));
    CHECK(client->edge_head==head && client->edge_count==count+1);
    tentative.events=CUIS_READY_BUTTON; sample_input(client,&tentative,TRUE);
    CHECK(cuis_input_config(CUIS_INPUT_BUTTON,session,1,1,1,0,0)==0);
    count=client->edge_count; commit_initial_history(client,&tentative);
    CHECK(client->edge_count==count); /* legitimate filter change wins */
    /* Real failure after publication must not consume a rejected prefix. */
    CHECK(cuis_input_config(CUIS_INPUT_BUTTON,session,3,3,1,0,0)==0);
    head=client->edge_head; count=client->edge_count;
    input_operation_hook=history_reject_observation;
    CHECK(cuis_session_wait(session,0,CUIS_READY_BUTTON,0,0,0)==ERANGE && !input_operation_hook);
    CHECK(client->edge_head==head && client->edge_count==count && !client->wait_operation &&
          !client->input_wait && client->references==1 && !client->input_result[1]);
    CHECK(cuis_input_config(CUIS_INPUT_BUTTON,session,1,1,1,0,0)==0);
    /* Modal fresh-grab save/restore and resume merge preserve provenance. */
    memcpy(cached_sequences,client->edge_sequence,sizeof(cached_sequences));
    head=client->edge_head; count=client->edge_count; policy=client->button_policy;
    CHECK(cuis_mouse_lock(session,CUIS_MOUSE_FRESH,0)==0);
    CHECK(cuis_pointer_packet(0,0,1,NULL,NULL) && cuis_pointer_packet(0,0,0,NULL,NULL));
    CHECK(cuis_mouse_lock(session,0,0)==0);
    CHECK(client->edge_head==head && client->edge_count==count && client->button_policy>policy);
    CHECK(!memcmp(cached_sequences,client->edge_sequence,sizeof(cached_sequences)));
    client->button_resume_active=TRUE; client->button_resume_head=0; client->button_resume_count=1;
    client->button_resume_edges[0]=client->button_edges[client->edge_head];
    client->resume_sequence[0]=client->edge_sequence[client->edge_head];
    sequence=client->edge_sequence[client->edge_head];
    client->edge_head=(client->edge_head+1)%CUIS_POINTER_CAPACITY; client->edge_count--;
    abandon_button_resume(client);
    CHECK(client->edge_count==count && client->edge_sequence[0]==sequence && !client->button_resume_active);
    for (i=1;i<count;i++) CHECK(client->edge_sequence[i]>client->edge_sequence[i-1]);
    memset(&tentative,0,sizeof(tentative)); tentative.events=CUIS_READY_BUTTON;
    sample_input(client,&tentative,TRUE);
    for (i=0;i<CUIS_POINTER_CAPACITY+4;i++) CHECK(cuis_pointer_packet(0,0,!(i&1),NULL,NULL));
    CHECK(client->edge_count==CUIS_POINTER_CAPACITY && client->edge_sequence[client->edge_head]>tentative.history_through);
    head=client->edge_head; commit_initial_history(client,&tentative);
    CHECK(client->edge_head==head && client->edge_count==CUIS_POINTER_CAPACITY);
    /* Tracking now retains a bounded eligible FIFO until commit, as well as
     * the durable journal. A lagging cursor fails explicitly after overflow. */
    client->edge_count=0; button_history_policy(client);
    memset(&client->active_input,0,sizeof(client->active_input));
    client->active_input.events=CUIS_READY_BUTTON;
    client->active_input.tracking=TRUE; client->active_input.count=1;
    client->active_input.start=input_ticks; client->active_input.duration=1000;
    client->active_input.history_read=BUTTON_HISTORY_EMPTY;
    button_history_begin(client,&client->active_input.history_cursor);
    client->input_wait=&client->active_input;
    button_history_begin(client,&cursor);
    sequence=client->button_sequence; allocations_before=allocations; fail_allocation=TRUE;
    for (i=0;i<CUIS_BUTTON_HISTORY_CAPACITY+4;i++)
        CHECK(cuis_pointer_packet(0,0,!(i&1),NULL,NULL));
    fail_allocation=FALSE;
    CHECK(allocations==allocations_before && client->button_sequence==sequence+CUIS_BUTTON_HISTORY_CAPACITY+4);
    CHECK(client->edge_count==CUIS_POINTER_CAPACITY && client->history_count==CUIS_BUTTON_HISTORY_CAPACITY);
    CHECK(client->active_input.history_read==BUTTON_HISTORY_GAP);
    client->input_wait=NULL;
    saved=cursor; memset(&edge,0xa5,sizeof(edge));
    CHECK(button_history_next(client,&cursor,&edge)==BUTTON_HISTORY_GAP && cursor.next==saved.next);
    CHECK(edge.sequence==0xa5a5a5a5a5a5a5a5ULL); /* failed observation writes nothing */
    button_history_begin(client,&cursor);
    for (i=0;i<CUIS_BUTTON_HISTORY_CAPACITY;i++) {
        CHECK(button_history_next(client,&cursor,&edge)==BUTTON_HISTORY_EVENT);
        CHECK(edge.sequence==sequence+5+i && edge.event.buttons==!(i&1));
    }
    CHECK(button_history_next(client,&cursor,&edge)==BUTTON_HISTORY_EMPTY);
    policy=client->button_policy; button_history_policy(client);
    CHECK(client->button_policy==policy+1 && button_history_next(client,&cursor,&edge)==BUTTON_HISTORY_POLICY);
    /* Session storage reuse never authorizes a cursor from its old identity. */
    storage=client;
    CHECK(cuis_service(CUIS_SESSION_END,session)==0 && allocations==frees);
    reuse_allocation=storage; replacement=cuis_service(CUIS_SESSION_START,0);
    CHECK(replacement>session); client=*find_owner(run);
    CHECK((void *)client==storage && button_history_next(client,&cursor,&edge)==BUTTON_HISTORY_RETIRED);
    client->button_sequence=~0ULL-1;
    button_history_begin(client,&cursor); memset(&event,0,sizeof(event));
    CHECK(button_history_record(client,&event)==~0ULL);
    CHECK(button_history_next(client,&cursor,&edge)==BUTTON_HISTORY_EVENT && edge.sequence==~0ULL);
    CHECK(button_history_next(client,&cursor,&edge)==BUTTON_HISTORY_EXHAUSTED);
    button_history_begin(client,&cursor);
    CHECK(button_history_next(client,&cursor,&edge)==BUTTON_HISTORY_EVENT && edge.sequence==~0ULL);
    CHECK(button_history_next(client,&cursor,&edge)==BUTTON_HISTORY_EXHAUSTED);
    CHECK(!button_history_record(client,&event) && client->history_exhausted && client->button_sequence==~0ULL);
    sequence=client->wait_sequence;
    CHECK(cuis_session_wait(replacement,0,CUIS_READY_BUTTON,0,0,0)==ERANGE && client->wait_sequence==sequence);
    CHECK(!client->wait_operation && !client->input_wait && client->references==1);
    client->button_policy=~0ULL; button_history_policy(client);
    CHECK(client->button_policy==~0ULL && client->history_exhausted);
    memset(&tentative,0,sizeof(tentative)); tentative.history_observed=TRUE;
    tentative.history_policy=~0ULL; tentative.history_through=~0ULL;
    client->edge_count=1; client->edge_head=0; client->edge_sequence[0]=~0ULL;
    commit_initial_history(client,&tentative);
    CHECK(client->edge_count==1 && !tentative.history_observed);
    CHECK(cuis_service(CUIS_SESSION_END,replacement)==0 && allocations==frees);
    next_handle=next_window_handle=0; allocations=frees=0;
    __builtin_printf("CUIS routed history: durable FIFO, explicit gaps/epochs, tentative prefix commit and no rewind: PASS (host)\n");
    return 0;
}
