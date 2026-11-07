/* Production resumed waits: reservation, live observation, commit and abort. */
static CUIS_CLIENT *resume_client;
static LONG resume_handle, resume_replacement;
static ULONG resume_start, resume_duration;
static int resume_action, resume_bad;
static void resume_wait_hook(void)
{
    UQUAD token=resume_client->wait_operation;
    ULONG count=resume_client->button_resume_count;
    CUIS_INPUT_WAIT *input=&resume_client->active_input;
    if (!token || resume_client->resume_operation!=token || !resume_client->button_resume_active ||
        input->start!=resume_start || input->duration!=resume_duration || input->count!=1 ||
        resume_client->references!=2) resume_bad=1;
    if (cuis_session_wait(resume_handle,0,CUIS_READY_TIMER,0)!=EACCDN ||
        resume_client->resume_operation!=token || resume_client->button_resume_count!=count)
        resume_bad=1;
    if (resume_action==2) {
        ULONG before=frees;
        if (cuis_service(CUIS_SESSION_END,resume_handle) || !resume_client->retired ||
            resume_client->input_wait || resume_client->references!=1 || frees!=before+1) resume_bad=1;
        resume_replacement=cuis_service(CUIS_SESSION_START,0);
        if (resume_replacement<=resume_handle || (*find_owner(run))->resume_operation ||
            (*find_owner(run))->button_resume_active) resume_bad=1;
        return;
    }
    if (!cuis_pointer_packet(0,0,resume_action==1?2:1,NULL,NULL) ||
        !cuis_pointer_packet(0,0,0,NULL,NULL)) resume_bad=1;
    if (!resume_client->button_resume_active || resume_client->button_resume_count!=count+2 ||
        resume_client->button_resume.start!=resume_start ||
        resume_client->active_input.live_through!=resume_client->button_resume.live_through)
        resume_bad=1;
    if (resume_action==1) {
        if (cuis_input_config(CUIS_INPUT_BUTTON,resume_handle,2,2,1,0,0)) resume_bad=1;
        if (resume_client->button_resume_active || resume_client->edge_count!=2) resume_bad=1;
    } else {
        if (resume_client->button_resume.count!=2 || input->count!=2 ||
            resume_client->button_resume.duration!=resume_duration+3 || input->duration!=resume_duration+3)
            resume_bad=1;
        if (resume_action==3) {
            input_ticks=resume_start+input->duration;
            /* This press arrives after the original deadline. It must remain
             * normal history, not become part of the completed resume prefix. */
            if (!cuis_pointer_packet(0,0,1,NULL,NULL)) resume_bad=1;
        } else if (resume_action==4) cancel_pending=TRUE;
        else update_owner=resume_client; /* fail after observation, before input commit */
    }
}
static int resume_transaction_checks(void)
{
    LONG window, token;
    ULONG interval=input_interval, before, visits;
    ULONG ticks_before=input_ticks, sleeps_before=input_sleeps, yields_before=delay_yields;
    int action, cycle;
    run=&first;
    for (cycle=0;cycle<3;cycle++) for (action=0;action<5;action++) {
        resume_handle=cuis_service(CUIS_SESSION_START,0);
        CHECK(resume_handle>=0); resume_client=*find_owner(run);
        pointer_buttons=0; pointer_suppressed=pointer_legacy_drag=FALSE; input_pointer_source=FALSE;
        window=cuis_window_service(CUIS_WINDOW_CREATE,0,0,300,300,0);
        CHECK(window>=0 && cuis_window_service(CUIS_WINDOW_SHOW,window,0,0,0,0)==0);
        test_children[0].parent=run; test_children[0].pid=10;
        test_children[0].reaped=test_children[0].started=FALSE; test_children[0].status=0;
        token=cuis_child_service(CUIS_CHILD_WATCH,resume_handle,CUIS_CHILD_RESERVE,0,0);
        CHECK(token>0 && cuis_child_service(CUIS_CHILD_WATCH,resume_handle,CUIS_CHILD_BIND,token,CHILD_IMAGE(10))==0);
        CHECK(cuis_input_config(CUIS_INPUT_INTERVAL,resume_handle,1000,1,0,0,0)==1000);
        CHECK(cuis_input_config(CUIS_INPUT_BUTTON,resume_handle,1,1,2,0,0)==0);
        resume_client->recent_count=0; input_sleeps=0; input_packet_mode=2; child_idle_at=2;
        CHECK(cuis_session_wait(resume_handle,0,CUIS_READY_BUTTON|CUIS_READY_CHILD,0)==CUIS_READY_CHILD);
        CHECK(resume_client->button_resume_active && resume_client->button_resume.count==1);
        resume_start=resume_client->button_resume.start; resume_duration=resume_client->button_resume.duration;
        resume_client->input_result[0]=123; resume_client->input_result[1]=456;
        cancellation_owner=action==4?resume_client:NULL;
        visits=cancellation_visits;
        resume_action=action; input_operation_hook=resume_wait_hook; before=frees;
        CHECK(cuis_session_wait(resume_handle,0,CUIS_READY_BUTTON,0)==(action==1?EAGAIN:action==3?(LONG)CUIS_READY_BUTTON:EACCDN));
        update_owner=NULL; cancel_pending=FALSE; cancellation_owner=NULL;
        CHECK(cancellation_visits==visits+(action==4) && !cancellation_bad);
        CHECK(!resume_bad && !input_operation_hook && input_irqs);
        if (action==2) {
            CHECK(frees>before && (*find_owner(run))->handle==(ULONG)resume_replacement);
            CHECK(cuis_service(CUIS_SESSION_QUERY,resume_handle)==EACCDN);
            CHECK(cuis_service(CUIS_SESSION_END,resume_replacement)==0);
        } else if (action==3) {
            CHECK(!resume_client->wait_operation && !resume_client->resume_operation &&
                  !resume_client->input_wait && resume_client->references==1 &&
                  resume_client->input_result[1]==2 && !resume_client->button_resume_active &&
                  !resume_client->button_resume_count && resume_client->edge_count==1);
            CHECK(cuis_input_config(CUIS_INPUT_BUTTON,resume_handle,1,1,1,0,0)==0);
            CHECK(cuis_session_wait(resume_handle,0,CUIS_READY_BUTTON,0)==CUIS_READY_BUTTON &&
                  resume_client->input_result[1]==1 && !resume_client->edge_count);
            CHECK(cuis_service(CUIS_SESSION_END,resume_handle)==0);
        } else {
            CHECK(!resume_client->wait_operation && !resume_client->resume_operation &&
                  !resume_client->input_wait && resume_client->references==1 &&
                  resume_client->input_result[0]==123 && resume_client->input_result[1]==456);
            if (!action || action==4) {
                CHECK(resume_client->button_resume_active && resume_client->button_resume_count==2 &&
                      resume_client->button_resume.start==resume_start &&
                      resume_client->button_resume.duration==resume_duration+3);
                CHECK(cuis_session_wait(resume_handle,0,CUIS_READY_BUTTON|CUIS_READY_TIMER,0)==CUIS_READY_TIMER);
                CHECK(resume_client->button_resume_active && !resume_client->button_resume_count &&
                      resume_client->button_resume.count==2 && resume_client->button_resume.start==resume_start &&
                      resume_client->button_resume.duration==resume_duration+3);
                CHECK(cuis_session_wait(resume_handle,0,CUIS_READY_BUTTON,0)==CUIS_READY_BUTTON);
                CHECK(resume_client->input_result[1]==2 && !resume_client->button_resume_active &&
                      !resume_client->edge_count && !resume_client->button_resume_count);
            } else {
                CHECK(!resume_client->button_resume_active && resume_client->edge_count==2);
                CHECK(cuis_session_wait(resume_handle,0,CUIS_READY_BUTTON,0)==CUIS_READY_BUTTON);
                CHECK((resume_client->input_result[4]&3)==2 && resume_client->edge_count==1);
                CHECK(cuis_input_config(CUIS_INPUT_BUTTON,resume_handle,2,0,1,0,0)==0);
                CHECK(cuis_session_wait(resume_handle,0,CUIS_READY_BUTTON,0)==CUIS_READY_BUTTON);
                CHECK(!(resume_client->input_result[4]&3) && !resume_client->edge_count);
            }
            CHECK(cuis_service(CUIS_SESSION_END,resume_handle)==0);
        }
        CHECK(allocations==frees);
    }
    input_interval=interval; input_ticks=ticks_before; input_sleeps=sleeps_before; delay_yields=yields_before;
    memset(test_children,0,sizeof(test_children));
    next_handle=next_window_handle=0; allocations=frees=0;
    __builtin_printf("CUIS tentative resume reservation, live prefix commit, abort/retry, deadline and policy ownership: PASS (host)\n");
    return 0;
}
