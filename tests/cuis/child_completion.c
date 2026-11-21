/* Observe and retire the same canonical right used for production launch. */
static ULONG child_idle_at;
static void complete_modeled_child(UWORD status)
{
    MODELED_CHILD *child=&modeled_children[2];
    child->completion.state=PROCESS_COMPLETED;
    child->completion.kind=PROCESS_COMPLETION_NORMAL;
    child->completion.status=status;
    child->completion.order_low=++modeled_order;
}
static void child_idle_hook(void)
{
    if (child_idle_at && input_sleeps==child_idle_at) {
        child_idle_at=0;
        complete_modeled_child(0x0400);
    }
}
/* Observed and console launches use the same PROCESS preparation. */
static LONG child_validate_error, child_start_error;
static ULONG child_start_calls, child_abort_calls;
static BOOL child_stay_live, retire_during_child_prepare;
static ULONG console_abort_stage;
static intptr_t console_launch_escape[5];
static void abort_console_launch(void)
{
    console_abort_stage = 0;
    cuis_process_exit(run);
    /* Model process termination and retirement of its cleanup scope. */
    kproc_release((KPROC *)run); /* Unwound caller-owned PROCESS lease. */
    cleanup_depth = 0;
    __builtin_longjmp(console_launch_escape,1);
}
static char launched_directory[1024];
LONG proc_exec_prepare_user_args(KPROC *owner, const PROCESS_EXEC_REQUEST *request)
{
    ULONG length;
    if (owner != (KPROC *)run || !input_irqs || !cleanup_depth ||
        modeled_launch_open || request->policy != PROCESS_EXEC_ASYNC) __builtin_abort();
    if (request->path != 0x11000 || request->tail != 0x12000 ||
        request->environment) return EIMBA;
    if (console_abort_stage == 1) abort_console_launch();
    if (child_validate_error) return child_validate_error;
    if (request->directory) {
        for (length=0;length<sizeof(launched_directory);length++) {
            if (!kproc_copy_from_user(launched_directory+length,request->directory+length,1)) return EIMBA;
            if (!launched_directory[length]) break;
        }
        if (!length) return EPTHNF;
        if (length==sizeof(launched_directory)) return ERANGE;
    }
    child_start_calls++;
    if (child_start_error) return child_start_error;
    if (console_abort_stage == 2) abort_console_launch();
    if (!modeled_children[2].references) reset_modeled_child(&modeled_children[2],2);
    modeled_children[2].references++;
    modeled_launch_open=modeled_launch_pending=TRUE;
    if (retire_during_child_prepare) cuis_process_exit(run);
    return MODELED_LAUNCH_RIGHT;
}
LONG proc_exec_handle_commit(KPROC *owner,ULONG handle)
{
#ifdef RECEIPT_RUNTIME_FIXTURE
    if (receipt_fixture_active) return receipt_fixture_commit(owner,handle);
#endif
    if (owner!=(KPROC *)run || handle!=MODELED_LAUNCH_RIGHT || input_irqs || !modeled_launch_open ||
        !modeled_launch_pending || (console_launcher && console_owner!=&third)) __builtin_abort();
    modeled_launch_pending=FALSE;
    if (!child_stay_live) complete_modeled_child(0x2468);
    else modeled_children[2].completion.state=PROCESS_RUNNING;
    return 2;
}
LONG proc_exec_handle_close(KPROC *owner,ULONG handle)
{
#ifdef RECEIPT_RUNTIME_FIXTURE
    if (receipt_fixture_active) return receipt_fixture_close(owner,handle);
#endif
    if (owner!=(KPROC *)run || handle!=MODELED_LAUNCH_RIGHT || !input_irqs ||
        !modeled_launch_open || !modeled_children[2].references) __builtin_abort();
    if (modeled_launch_pending) child_abort_calls++;
    modeled_children[2].references--;
    modeled_launch_open=modeled_launch_pending=FALSE;
    return E_OK;
}
#ifdef RECEIPT_RUNTIME_FIXTURE
LONG proc_exec_handle_prepare(KPROC *owner,char *path,char *tail,char *environment,char *directory,ULONG policy)
{
    return receipt_fixture_prepare(owner,path,tail,environment,directory,policy);
}
#endif
static LONG launch_child(LONG session, UQUAD flags)
{
    return cuis_launch_attach(session,flags,0x11000,0x12000,0,0);
}
static int child_launch_checks(void)
{
    LONG session, other;
    ULONG aborted=child_abort_calls, before=child_start_calls;
    CHECK(!clients && allocations==frees);
    stream_ring_count=0;
    run=&first; session=cuis_service(CUIS_SESSION_START,0);
    run=&second; other=cuis_service(CUIS_SESSION_START,0); run=&first;
    CHECK(launch_child(other,CUIS_LAUNCH_OBSERVE)==EACCDN && child_start_calls==before);
    child_validate_error=EIMBA;
    CHECK(launch_child(session,CUIS_LAUNCH_OBSERVE)==EIMBA && child_start_calls==before);
    child_validate_error=0;
    child_start_error=ENSMEM;
    CHECK(launch_child(session,CUIS_LAUNCH_OBSERVE)==ENSMEM && child_start_calls==before+1);
    CHECK(!modeled_launch_open && !owned_client(session)->receipt_table);
    child_start_error=0;
    fail_allocation=TRUE;
    CHECK(launch_child(session,CUIS_LAUNCH_OBSERVE)==ENSMEM && child_abort_calls==aborted+1);
    fail_allocation=FALSE;
    retire_during_child_prepare=TRUE;
    CHECK(launch_child(session,CUIS_LAUNCH_OBSERVE | CUIS_LAUNCH_CONSOLE)==EACCDN &&
          child_abort_calls==aborted+2 && !console_owner && !console_launcher);
    retire_during_child_prepare=FALSE;
    CHECK(!modeled_launch_open && !modeled_children[2].references);
    session=cuis_service(CUIS_SESSION_START,0);
    CHECK(launch_child(session,CUIS_LAUNCH_OBSERVE)==2 &&
          modeled_launch_open && owned_client(session)->receipt_table);
    CHECK(cuis_session_wait(session,0,CUIS_READY_CHILD,0,0,0)==CUIS_READY_CHILD);
    CHECK(cuis_service(CUIS_SESSION_END,session)==E_OK &&
          !modeled_launch_open && !modeled_children[2].references);
    run=&second; cuis_process_exit(run); run=&first;
    CHECK(!clients && allocations==frees);
    return 0;
}
static int console_launch_checks(void)
{
    LONG session, other;
    CHECK(!clients && !console_owner && !text_console_active && allocations==frees);
    run=&first; session=cuis_service(CUIS_SESSION_START,0);
    run=&second; other=cuis_service(CUIS_SESSION_START,0); run=&first;
    CHECK(launch_child(session,4)==EINVAL && launch_child(session,0)==EINVAL);
    child_start_error=ENSMEM;
    CHECK(launch_child(session,CUIS_LAUNCH_CONSOLE | CUIS_LAUNCH_OBSERVE)==ENSMEM);
    CHECK(!console_owner && !console_launcher && !text_console_active);
    child_start_error=0; child_stay_live=TRUE;
    CHECK(cuis_display_lock(session,1,0)==E_OK);
    CHECK(launch_child(session,CUIS_LAUNCH_CONSOLE | CUIS_LAUNCH_OBSERVE)==2);
    CHECK(console_owner==&third && text_console_active && !console_launcher);
    CHECK(cuis_display_lock(session,0,0)==E_OK);
    run=&second;
    CHECK(launch_child(other,CUIS_LAUNCH_CONSOLE | CUIS_LAUNCH_OBSERVE)==EAGAIN);
    CHECK(cuis_display_lock(other,1,0)==EAGAIN);
    run=&first;
    cuis_process_exit(run);
    CHECK(console_owner==&third && !modeled_launch_open && text_console_active);
    cuis_process_exit(&third);
    CHECK(!console_owner && !text_console_active);
    run=&second;
    CHECK(cuis_display_lock(other,1,0)==E_OK && cuis_display_lock(other,0,0)==E_OK);
    CHECK(launch_child(other,CUIS_LAUNCH_CONSOLE)==2);
    CHECK(console_owner==&third && !modeled_launch_open && !owned_client(other)->receipt_table);
    cuis_process_exit(&third);
    cuis_process_exit(run);
    run=&first; child_stay_live=FALSE;
    CHECK(!clients && !console_owner && !text_console_active && allocations==frees);
    return 0;
}
static int console_abandoned_launch_checks(void)
{
    LONG session;
    UQUAD flags;
    volatile WORD stage;
    CHECK(!clients && !console_owner && allocations==frees);
    child_stay_live=TRUE;
    for (stage=1;stage<=4;stage++) {
        run=&first;
        session=cuis_service(CUIS_SESSION_START,0);
        flags=CUIS_LAUNCH_CONSOLE | (stage<=2 ? CUIS_LAUNCH_OBSERVE:0);
        console_abort_stage=stage<=2 ? stage:stage-2;
        if (!__builtin_setjmp(console_launch_escape)) {
            (void)launch_child(session,flags);
            CHECK(FALSE);
        }
        CHECK(!console_abort_stage && !console_launcher);
        CHECK(!console_owner && !text_console_active && !*find_owner(&first));
        run=&second;
        session=cuis_service(CUIS_SESSION_START,0);
        CHECK(launch_child(session,flags)==2);
        CHECK(console_owner==&third && text_console_active);
        cuis_process_exit(&third);
        cuis_process_exit(&second);
        CHECK(!clients && !console_owner && !text_console_active && allocations==frees);
    }
    child_stay_live=FALSE; run=&first;
    return 0;
}

static int button_resume_checks(void)
{
    LONG session, window;
    CUIS_CLIENT *client;
    int variant;
    CHECK(!clients && allocations == frees);
    run = &first;
    session = cuis_service(CUIS_SESSION_START,0);
    client = *find_owner(run);
    pointer_buttons = 0; input_pointer_source = pointer_suppressed = pointer_legacy_drag = FALSE;
    window = cuis_window_service(CUIS_WINDOW_CREATE,0,0,300,300,0);
    CHECK(window >= 0 && cuis_window_service(CUIS_WINDOW_SHOW,window,0,0,0,0) == E_OK);
    for (variant = 0; variant < 2; variant++) {
        CUIS_RECEIPT_RECORD record;
        child_stay_live=TRUE;
        CHECK(launch_child(session,CUIS_LAUNCH_OBSERVE)==2);
        child_stay_live=FALSE;
        client->recent_count = 0;
        CHECK(cuis_input_config(CUIS_INPUT_BUTTON,session,1,1,2,0,0) == E_OK);
        input_sleeps = 0; input_packet_mode = 2; child_idle_at = 2;
        CHECK(cuis_session_wait(session,0,CUIS_READY_BUTTON|CUIS_READY_CHILD,0,0,0) == CUIS_READY_CHILD);
        CHECK(client->button_resume_active && client->button_resume.count == 1);
        receipt_record(client,0,&record);
        record.packet=0x10000; record.bytes=16;
        CHECK(receipt_prepare(&record)==E_OK && receipt_commit(&record)==16);
        CHECK(receipt_ack(&record)==E_OK && proc_exec_handle_close((KPROC *)run,record.tag)==E_OK);
        CHECK(receipt_reclaim(&record)==E_OK);
        CHECK(cuis_stream_service(CUIS_STREAM_READ,session,0x20000,16,0,0) == 16);
        if (!variant) {
            cuis_pointer_packet(0,0,2,NULL,NULL); cuis_pointer_packet(0,0,0,NULL,NULL);
            CHECK(client->button_resume_count == 2 && !client->edge_count);
            CHECK(cuis_input_config(CUIS_INPUT_BUTTON,session,2,2,1,0,0) == E_OK);
            CHECK(!client->button_resume_active && client->edge_count == 2);
            CHECK(cuis_session_wait(session,0,CUIS_READY_BUTTON,0,0,0) == CUIS_READY_BUTTON);
            CHECK(cuis_input_config(CUIS_INPUT_RESULT,session,0x1000,0x1004,0x1008,0x100c,0x1010) == E_OK && (copied[4]&3) == 2);
            CHECK(cuis_input_config(CUIS_INPUT_BUTTON,session,2,0,1,0,0) == E_OK);
            CHECK(cuis_session_wait(session,0,CUIS_READY_BUTTON,0,0,0) == CUIS_READY_BUTTON);
            CHECK(cuis_input_config(CUIS_INPUT_RESULT,session,0x1000,0x1004,0x1008,0x100c,0x1010) == E_OK && !(copied[4]&3));
            CHECK(!client->edge_count);
        } else {
            CHECK(cuis_stream_service(CUIS_STREAM_WRITE,session,0x10000,16,0,0) == 16);
            CHECK(cuis_session_wait(session,16,CUIS_READY_BUTTON|CUIS_READY_STREAM,0,0,0) == CUIS_READY_STREAM);
            CHECK(client->button_resume_active && client->button_resume.count == 1);
            CHECK(cuis_stream_service(CUIS_STREAM_READ,session,0x20000,16,0,0) == 16);
            CHECK(cuis_session_wait(session,0,CUIS_READY_BUTTON|CUIS_READY_TIMER,0,0,0) == CUIS_READY_TIMER);
            CHECK(client->button_resume_active && client->button_resume.count == 1);
            CHECK(cuis_keyboard_event(0x001e0061));
            CHECK(cuis_session_wait(session,0,CUIS_READY_BUTTON|CUIS_READY_KEY,0,0,0) == CUIS_READY_KEY);
            CHECK(client->button_resume_active && client->button_resume.count == 1);
            cuis_pointer_packet(0,0,1,NULL,NULL); cuis_pointer_packet(0,0,0,NULL,NULL);
            CHECK(cuis_session_wait(session,0,CUIS_READY_BUTTON,0,0,0) == CUIS_READY_BUTTON);
            CHECK(cuis_input_config(CUIS_INPUT_RESULT,session,0x1000,0x1004,0x1008,0x100c,0x1010) == E_OK && copied[1] == 2);
            CHECK(!client->button_resume_active && !client->button_resume_count && !client->edge_count);
        }
    }
    cuis_process_exit(run);
    CHECK(!clients && allocations == frees);
    return 0;
}

static int launch_directory_checks(void)
{
    LONG session, other;
    CHECK(!clients && allocations == frees);
    run = &first; session = cuis_service(CUIS_SESSION_START,0);
    run = &second; other = cuis_service(CUIS_SESSION_START,0); run = &first;
    strcpy((char *)stream_input+0x3000,"C:\\APPS");
    CHECK(cuis_launch_attach(other,CUIS_LAUNCH_CONSOLE,0x11000,0x12000,0,0x13000) == EACCDN);
    CHECK(cuis_launch_attach(session,0,0x11000,0x12000,0,0x13000) == EINVAL);
    CHECK(cuis_launch_attach(session,CUIS_LAUNCH_CONSOLE,0x11000,0x12000,0,0x13000) == 2);
    CHECK(!strcmp(launched_directory,"C:\\APPS"));
    cuis_process_exit(&third);
    CHECK(cuis_launch_attach(session,CUIS_LAUNCH_CONSOLE,0x11000,0x12000,0,0x18000) == EIMBA);
    stream_input[0x3000] = 0;
    CHECK(cuis_launch_attach(session,CUIS_LAUNCH_CONSOLE,0x11000,0x12000,0,0x13000) == EPTHNF);
    memset(stream_input+0x3000,'X',sizeof(launched_directory));
    CHECK(cuis_launch_attach(session,CUIS_LAUNCH_CONSOLE,0x11000,0x12000,0,0x13000) == ERANGE);
    cuis_process_exit(&first); run = &second; cuis_process_exit(run); run = &first;
    CHECK(!clients && allocations == frees);
    return 0;
}
