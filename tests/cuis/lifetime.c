/* Production retained event waits; hooks run only at scheduler handoff. */
static CUIS_CLIENT *lifetime_client;
static CUIS_WINDOW *lifetime_window;
static LONG lifetime_session, lifetime_replacement;
static int lifetime_action, lifetime_bad;
static void lifetime_hook(void)
{
    ULONG before=frees;
    if (lifetime_client->references != 2 ||
        (lifetime_window && lifetime_window->references != 2)) lifetime_bad=1;
    if (lifetime_action==1) {
        if (cuis_window_service(CUIS_WINDOW_DESTROY,lifetime_window->handle,0,0,0,0)) lifetime_bad=1;
        if (!lifetime_window->retired || lifetime_window->references!=1 || frees!=before) lifetime_bad=1;
        /* New storage/handle must not revive the retained closed identity. */
        if (cuis_window_service(CUIS_WINDOW_CREATE,1,1,32,32,0)<0) lifetime_bad=1;
    } else if (lifetime_action==2) {
        if (cuis_service(CUIS_SESSION_END,lifetime_session)) lifetime_bad=1;
        if (!lifetime_client->retired || lifetime_client->owner ||
            lifetime_client->references!=1 || frees!=before) lifetime_bad=1;
        lifetime_replacement=cuis_service(CUIS_SESSION_START,0);
        if (lifetime_replacement<=lifetime_session) lifetime_bad=1;
        /* A message for the replacement must not satisfy the old wait. */
        (*find_owner(run))->message_count=1;
    } else {
        lifetime_client->message_count=1;
        if (lifetime_action==3) cancel_pending=TRUE;
    }
}
static int lifetime_checks(void)
{
    LONG window, result;
    ULONG before;
    void *retired_storage, *closed_storage;
    CUIS_INPUT_WAIT other_input;
    int action, cycle;
    run=&first;
    for (cycle=0;cycle<8;cycle++) {
        lifetime_session=cuis_service(CUIS_SESSION_START,0);
        CHECK(lifetime_session>=0);
        lifetime_client=*find_owner(run); lifetime_window=NULL;
        /* Pending cancellation neither consumes messages nor publishes input. */
        lifetime_client->message_count=1; before=frees; cancel_pending=TRUE;
        CHECK(cuis_message_service(CUIS_MESSAGE_WAIT,lifetime_session,0,0,0,0,0)==EACCDN);
        CHECK(lifetime_client->references==1 && lifetime_client->message_count==1 &&
              !lifetime_client->input_wait && frees==before && input_irqs);
        cancel_pending=FALSE; lifetime_client->message_count=0;
        for (action=3;action<=4;action++) {
            lifetime_action=action; lifetime_wait_hook=lifetime_hook;
            result=cuis_message_service(CUIS_MESSAGE_WAIT,lifetime_session,(UQUAD)-1,0,0,0,0);
            CHECK(result==(action==3 ? EACCDN : 1) && !lifetime_wait_hook);
            CHECK(lifetime_client->message_count==1 && lifetime_client->references==1 && input_irqs);
            cancel_pending=FALSE; lifetime_client->message_count=0;
        }
        /* Both READY and CANCELLED leave all unrelated input history intact. */
        memset(&other_input,0,sizeof(other_input));
        lifetime_client->input_wait=&other_input;
        lifetime_client->edge_count=2; lifetime_client->recent_count=3;
        lifetime_client->button_resume_active=TRUE;
        lifetime_action=3; lifetime_wait_hook=lifetime_hook;
        CHECK(cuis_message_service(CUIS_MESSAGE_WAIT,lifetime_session,(UQUAD)-1,0,0,0,0)==EACCDN);
        CHECK(lifetime_client->edge_count==2 && lifetime_client->recent_count==3 &&
              lifetime_client->button_resume_active && lifetime_client->input_wait==&other_input);
        lifetime_client->input_wait=NULL;
        cancel_pending=FALSE; lifetime_client->message_count=0;
        window=cuis_window_service(CUIS_WINDOW_CREATE,1,1,32,32,0);
        CHECK(window>=0 && cuis_window_service(CUIS_WINDOW_SHOW,window,0,0,0,0)==0);
        lifetime_window=input_window(window);
        lifetime_window->references=0xffffffffUL; before=frees;
        CHECK(cuis_event_service(window,CUIS_EVENT_MESSAGE,0)==EACCDN);
        CHECK(lifetime_client->references==1 && frees==before);
        lifetime_window->references=1;
        lifetime_client->references=0xffffffffUL;
        CHECK(cuis_event_service(window,CUIS_EVENT_MESSAGE,0)==EACCDN);
        CHECK(lifetime_window->references==1 && frees==before);
        lifetime_client->references=1;
        closed_storage=lifetime_window;
        lifetime_action=1; lifetime_wait_hook=lifetime_hook; before=frees;
        CHECK(cuis_event_service(window,CUIS_EVENT_MESSAGE,(UQUAD)-1)==EACCDN);
        CHECK(frees==before+1 && !input_window(window) && lifetime_client->references==1);
        reuse_allocation=closed_storage;
        result=cuis_window_service(CUIS_WINDOW_CREATE,1,1,32,32,0);
        CHECK(result>window && (void *)input_window(result)==closed_storage);
        CHECK(cuis_event_service(window,CUIS_EVENT_MESSAGE,0)==EACCDN);
        /* Retire the session while a message wait retains its storage. */
        while (lifetime_client->windows)
            CHECK(cuis_window_service(CUIS_WINDOW_DESTROY,lifetime_client->windows->handle,0,0,0,0)==0);
        retired_storage=lifetime_client;
        lifetime_window=NULL; lifetime_action=2; lifetime_wait_hook=lifetime_hook; before=frees;
        CHECK(cuis_message_service(CUIS_MESSAGE_WAIT,lifetime_session,(UQUAD)-1,0,0,0,0)==EACCDN);
        CHECK(frees==before+1 && (*find_owner(run))->handle==(ULONG)lifetime_replacement &&
              (*find_owner(run))->message_count==1 && input_irqs);
        CHECK(cuis_service(CUIS_SESSION_QUERY,lifetime_session)==EACCDN);
        CHECK(cuis_service(CUIS_SESSION_END,lifetime_replacement)==0);
        reuse_allocation=retired_storage;
        result=cuis_service(CUIS_SESSION_START,0);
        CHECK(result>lifetime_replacement && (void *)*find_owner(run)==retired_storage);
        CHECK(cuis_service(CUIS_SESSION_QUERY,lifetime_session)==EACCDN);
        CHECK(cuis_service(CUIS_SESSION_QUERY,lifetime_replacement)==EACCDN);
        CHECK(cuis_service(CUIS_SESSION_END,result)==0);
        CHECK(!lifetime_bad && allocations==frees && !input_bad_idle);
    }
    __builtin_printf("CUIS retained event identities, retirement, ABA, cancellation and rollback: PASS (host)\n");
    next_handle=next_window_handle=0; allocations=frees=0;
    return 0;
}
