/* Production completion ownership, ordering, capacity and atomic publication. */
static struct { PD *parent; WORD pid; LONG status; BOOL reaped, started; } test_children[3];
static ULONG child_reaps;
static LONG child_session, child_token;
static int child_copy_mode;
static ULONG child_idle_at;
static BOOL modeled_pid_reserved(WORD pid)
{
    return modeled_children[pid].references != 0;
}
static void fault_modeled_child(PD *parent, WORD pid, WORD signal)
{
    MODELED_CHILD *child=&modeled_children[pid];
    if (child->parent==parent && child->completion.state<PROCESS_COMPLETED && !child->signal)
        child->signal=signal;
}
static void complete_modeled_child(PD *parent, WORD pid, UWORD status)
{
    MODELED_CHILD *child=&modeled_children[pid];
    if (child->parent!=parent || child->completion.state>=PROCESS_COMPLETED) return;
    child->completion.state=PROCESS_COMPLETED;
    child->completion.kind=child->signal ? PROCESS_COMPLETION_SIGNAL : PROCESS_COMPLETION_NORMAL;
    child->completion.signal=child->signal;
    child->completion.status=child->signal ? 0:status;
    child->completion.order_low=++modeled_order;
}
static LONG bind_modeled_child(LONG session, LONG token, WORD pid)
{
    MODELED_CHILD *child=&modeled_children[pid];
    PROCESS_OBSERVER *observer=(PROCESS_OBSERVER *)child;
    LONG result;
    if (!child->references) reset_modeled_child(child,pid);
    child->references++;
    result=bind_child(owned_client(session),(ULONG)token,&observer);
    process_observer_release(&observer);
    return result;
}
static void child_idle_hook(void)
{
    if (child_idle_at && input_sleeps == child_idle_at) {
        child_idle_at = 0;
        test_children[0].status = (10L << 16) | 0x0400;
        complete_modeled_child(&first,10,0x0400);
    }
}
/* A launched child's basepage, as spawn binds it; session.c's kproc_pid()
 * maps these page numbers back to the PID. */
#define CHILD_IMAGE(pid) ((ULONG)(pid) * 4096UL)
#define CHILD_BIND(s,t,pid) bind_modeled_child(s,t,pid)
LONG ksched_wait(WORD pid, WORD options, UQUAD usage)
{
    int i;
    if (options != 1 || usage) return EINVAL;
    for (i = 0; i < 3; i++)
        if (test_children[i].parent == run && test_children[i].pid == pid && !test_children[i].reaped) {
            if (!test_children[i].status) return 0;
            test_children[i].reaped = TRUE; child_reaps++;
            return test_children[i].status;
        }
    return EFILNF;
}
#define CHILD_W(s,a,t,p) cuis_child_service(CUIS_CHILD_WATCH,s,a,t,p)
#define CHILD_R(s) cuis_child_service(CUIS_CHILD_RESULT,s,0,0,0)
#define CHILD_P(s,v,p,n) cuis_child_service(CUIS_CHILD_PUBLISH,s,v,p,n)
static void child_copy_hook(void)
{
    int mode = child_copy_mode;
    child_copy_mode = 0;
    if (mode == 1) {
        CHILD_W(child_session,CUIS_CHILD_CANCEL,child_token,0);
        test_children[0].started = FALSE;
        child_token = CHILD_W(child_session,CUIS_CHILD_RESERVE,0,0);
        CHILD_BIND(child_session,child_token,10);
        test_children[0].started = TRUE;
        complete_modeled_child(&first,10,0x0400);
    } else if (mode == 2) cuis_process_exit(run);
}
static int child_completion_checks(void)
{
    LONG tokens[CUIS_CHILDREN_PER_SESSION], other, status, second_status, before, window;
    CUIS_CLIENT *client;
    UWORD i;
    CHECK(!clients && allocations == frees);
    stream_ring_count = 0; next_child_token = 0;
    run = &first; child_session = cuis_service(CUIS_SESSION_START,0);
    run = &second; other = cuis_service(CUIS_SESSION_START,0);
    run = &first;
    CHECK(CHILD_R(other) == EACCDN && CHILD_R(0x100000000ULL) == EINVAL);
    CHECK(CHILD_W(child_session,3,0,0) == EINVAL);
    CHECK(CHILD_W(child_session,CUIS_CHILD_RESERVE,1,0) == EINVAL);
    CHECK(CHILD_W(child_session,CUIS_CHILD_RESERVE,0,1) == EINVAL);
    before = allocations;
    for (i = 0; i < CUIS_CHILDREN_PER_SESSION; i++) {
        tokens[i] = CHILD_W(child_session,CUIS_CHILD_RESERVE,0,0);
        CHECK(tokens[i] > 0);
    }
    CHECK(allocations == (ULONG)before && CHILD_W(child_session,CUIS_CHILD_RESERVE,0,0) == EAGAIN);
    for (i = 0; i < CUIS_CHILDREN_PER_SESSION; i++) CHECK(CHILD_W(child_session,CUIS_CHILD_CANCEL,tokens[i],0) == E_OK);
    CHECK(CHILD_W(child_session,CUIS_CHILD_CANCEL,tokens[0],0) == EACCDN);
    child_token = CHILD_W(child_session,CUIS_CHILD_RESERVE,0,0);
    test_children[0].parent = &first; test_children[0].pid = 10;
    test_children[1].parent = &first; test_children[1].pid = 11;
    test_children[2].parent = &second; test_children[2].pid = 12;
    /* Action 1 bound an unlaunched staged image; it went with the x32
     * staged Pexec modes (#1018). */
    CHECK(CHILD_W(child_session,1,child_token,CHILD_IMAGE(10)) == EINVAL);
    CHECK(CHILD_BIND(child_session,child_token,10) == E_OK && modeled_pid_reserved(10));
    tokens[0] = CHILD_W(child_session,CUIS_CHILD_RESERVE,0,0);
    CHECK(CHILD_BIND(child_session,tokens[0],10) == EACCDN);
    CHECK(CHILD_BIND(child_session,tokens[0],11) == E_OK);
    CHECK(CHILD_R(child_session) == 0 && !child_reaps);
    /* Exit ordering, not reservation order, determines the notification head. */
    test_children[0].started = test_children[1].started = TRUE;
    second_status = test_children[1].status = (11L << 16) | 0x1234;
    complete_modeled_child(&first,11,0x1234);
    status = test_children[0].status = (10L << 16) | 0x0400;
    fault_modeled_child(&second,10,7); /* Another parent's watch cannot be modified. */
    fault_modeled_child(&first,10,4);
    fault_modeled_child(&first,10,11); /* A generic fallback must not replace SIGILL. */
    complete_modeled_child(&first,10,0xffff);
    complete_modeled_child(&first,10,0xffff);
    CHECK(CHILD_R(child_session) == second_status);
    CHECK(cuis_session_wait(child_session,16,CUIS_READY_CHILD|CUIS_READY_TIMER,0,0,0) == (CUIS_READY_CHILD|CUIS_READY_TIMER));
    CHECK(CHILD_P(child_session,status,0x10000,16) == EACCDN);
    CHECK(CHILD_P(child_session,second_status,0x10000,33) == EINVAL);
    fail_copy = TRUE;
    CHECK(CHILD_P(child_session,second_status,0x10000,16) == EIMBA); fail_copy = FALSE;
    fail_allocation = TRUE;
    CHECK(CHILD_P(child_session,second_status,0x10000,16) == ENSMEM); fail_allocation = FALSE;
    CHECK(CHILD_R(child_session) == second_status && !child_reaps && modeled_pid_reserved(11));
    /* An explicit MiNT wait cannot erase an already retained notification. */
    CHECK(ksched_wait(11,1,0) == second_status && child_reaps == 1);
    memset(stream_input,0x31,16);
    CHECK(CHILD_P(child_session,second_status,0x10000,16) == E_OK && !modeled_pid_reserved(11));
    client = *find_owner(run);
    client->stream_bytes = CUIS_STREAM_BYTES-1;
    CHECK(CHILD_P(child_session,status,0x10000,16) == EAGAIN && child_reaps == 1 && CHILD_R(child_session) == status);
    client->stream_bytes = 16;
    CHECK(CHILD_P(child_session,status,0x10000,16) == E_OK && child_reaps == 1);
    CHECK(!test_children[0].reaped && ksched_wait(10,1,0) == status && child_reaps == 2);
    CHECK(ksched_wait(10,1,0) == EFILNF && child_reaps == 2);
    CHECK(CHILD_R(child_session) == 0 && !modeled_pid_reserved(10));
    CHECK(cuis_stream_service(CUIS_STREAM_READ,child_session,0x20000,32,0,0) == 32);
    CHECK(stream_output[0] == 0x31 && stream_output[31] == 0x31);
    /* A public completion must wake MU_MESAG during an unfinished double click,
     * without waiting for the click interval/another mouse packet. */
    test_children[0].reaped = test_children[0].started = FALSE; test_children[0].status = 0;
    child_token = CHILD_W(child_session,CUIS_CHILD_RESERVE,0,0);
    CHECK(CHILD_BIND(child_session,child_token,10) == E_OK);
    pointer_buttons = 0; input_pointer_source = pointer_suppressed = pointer_legacy_drag = FALSE;
    window = cuis_window_service(CUIS_WINDOW_CREATE,0,0,300,300,0);
    CHECK(window >= 0 && cuis_window_service(CUIS_WINDOW_SHOW,window,0,0,0,0) == E_OK);
    CHECK(cuis_input_config(CUIS_INPUT_BUTTON,child_session,1,1,2,0,0) == E_OK);
    input_sleeps = 0; input_packet_mode = 2; child_idle_at = 2;
    CHECK(cuis_session_wait(child_session,0,CUIS_READY_BUTTON|CUIS_READY_CHILD,0,0,0) == CUIS_READY_CHILD);
    CHECK(input_sleeps == 2 && !input_packet_mode && !client->input_wait);
    CHECK(client->button_resume_active);
    CHECK(CHILD_P(child_session,status,0x10000,16) == E_OK);
    CHECK(cuis_stream_service(CUIS_STREAM_READ,child_session,0x20000,16,0,0) == 16);
    /* The second click arrives after message delivery, before the next wait. */
    CHECK(cuis_input_config(CUIS_INPUT_BUTTON,child_session,1,1,2,0,0) == E_OK);
    cuis_pointer_packet((WORD)(10-pointer_x),(WORD)(10-pointer_y),1,NULL,NULL);
    cuis_pointer_packet(0,0,0,NULL,NULL);
    CHECK(cuis_session_wait(child_session,0,CUIS_READY_BUTTON,0,0,0) == CUIS_READY_BUTTON);
    CHECK(cuis_input_config(CUIS_INPUT_RESULT,child_session,0x1000,0x1004,0x1008,0x100c,0x1010) == E_OK && copied[1] == 2);
    CHECK(!client->button_resume_active);
    test_children[0].reaped = test_children[0].started = FALSE;
    child_token = CHILD_W(child_session,CUIS_CHILD_RESERVE,0,0);
    CHECK(CHILD_BIND(child_session,child_token,10) == E_OK);
    test_children[0].started = TRUE; complete_modeled_child(&first,10,0x0400);
    CHECK(CHILD_R(child_session) == status);
    /* Replacing the same PID/status while copying must invalidate the old token. */
    child_copy_mode = 1;
    CHECK(CHILD_P(child_session,status,0x10000,16) == EACCDN && CHILD_R(child_session) == status);
    CHECK(CHILD_P(child_session,status,0x10000,16) == E_OK && child_reaps == 2);
    test_children[0].reaped = test_children[0].started = FALSE;
    child_token = CHILD_W(child_session,CUIS_CHILD_RESERVE,0,0);
    CHECK(CHILD_BIND(child_session,child_token,10) == E_OK);
    test_children[0].started = TRUE; complete_modeled_child(&first,10,0x0400);
    child_copy_mode = 2;
    CHECK(CHILD_P(child_session,status,0x10000,16) == EACCDN && !modeled_pid_reserved(10));
    CHECK(CHILD_R(child_session) == EACCDN);
    child_session = cuis_service(CUIS_SESSION_START,0);
    CHECK(CHILD_R(child_session) == 0);
    next_child_token = 0x7fffffffUL;
    CHECK(CHILD_W(child_session,CUIS_CHILD_RESERVE,0,0) == EAGAIN);
    cuis_process_exit(&first); run = &second; cuis_process_exit(run); run = &first;
    CHECK(!clients && allocations == frees && !input_bad_idle);
    return 0;
}
#undef CHILD_W
#undef CHILD_R
#undef CHILD_P

/* Observed load-and-go reuses GEMDOS validation and attaches inside spawn. */
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
#define MODELED_LAUNCH_RIGHT 70UL
static BOOL modeled_launch_open, modeled_launch_pending;
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
    if (!child_stay_live) {
        test_children[0].status = (2L << 16) | 0x2468;
        complete_modeled_child(run,2,0x2468);
    }
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
static LONG launch_child(UQUAD session, UQUAD token, UQUAD path,
                         UQUAD tail, UQUAD environment, UQUAD console)
{
    if (console > 1) return EINVAL;
    return cuis_launch_attach(session,token | (console ? CUIS_LAUNCH_CONSOLE : 0),
                             path,tail,environment,0);
}
static int child_launch_checks(void)
{
    LONG session, token, other;
    CHECK(!clients && allocations == frees);
    next_child_token = 0; run = &first;
    session = cuis_service(CUIS_SESSION_START,0);
    run = &second; other = cuis_service(CUIS_SESSION_START,0); run = &first;
    token = cuis_child_service(CUIS_CHILD_WATCH,session,CUIS_CHILD_RESERVE,0,0);
    CHECK(launch_child(other,token,0x11000,0x12000,0,0) == EACCDN && !child_start_calls);
    child_validate_error = EIMBA;
    CHECK(launch_child(session,token,0x11000,0x12000,0,0) == EIMBA && !child_start_calls);
    CHECK(cuis_child_service(CUIS_CHILD_WATCH,session,CUIS_CHILD_CANCEL,token,0) == EACCDN);
    child_validate_error = 0;
    token = cuis_child_service(CUIS_CHILD_WATCH,session,CUIS_CHILD_RESERVE,0,0);
    child_start_error = ENSMEM;
    CHECK(launch_child(session,token,0x11000,0x12000,0,0) == ENSMEM && child_start_calls == 1);
    CHECK(cuis_child_service(CUIS_CHILD_WATCH,session,CUIS_CHILD_CANCEL,token,0) == EACCDN);
    child_start_error = 0;
    token = cuis_child_service(CUIS_CHILD_WATCH,session,CUIS_CHILD_RESERVE,0,0);
    CHECK(launch_child(session,token,0x11000,0x12000,0,0) == 2 && child_start_calls == 2);
    CHECK(cuis_child_service(CUIS_CHILD_RESULT,session,0,0,0) == ((2L << 16) | 0x2468));
    CHECK(modeled_pid_reserved(2));
    CHECK(launch_child(session,token,0x11000,0x12000,0,0) == EACCDN && child_start_calls == 2);
    token = cuis_child_service(CUIS_CHILD_WATCH,session,CUIS_CHILD_RESERVE,0,0);
    CHECK(token > 0);
    retire_during_child_prepare = TRUE;
    CHECK(launch_child(session,token,0x11000,0x12000,0,1) == EACCDN &&
          child_abort_calls == 1 && !console_owner && !console_launcher);
    retire_during_child_prepare = FALSE;
    cuis_process_exit(&first); run = &second; cuis_process_exit(run); run = &first;
    CHECK(!clients && allocations == frees && !modeled_pid_reserved(2));
    return 0;
}

static int console_launch_checks(void)
{
    LONG session, other, token, other_token, child_session, slots[CUIS_CHILDREN_PER_SESSION];
    unsigned index;
    CHECK(!clients && !console_owner && !text_console_active && allocations == frees);
    run = &first; session = cuis_service(CUIS_SESSION_START,0);
    run = &second; other = cuis_service(CUIS_SESSION_START,0); run = &first;
    token = cuis_child_service(CUIS_CHILD_WATCH,session,CUIS_CHILD_RESERVE,0,0);
    CHECK(launch_child(session,token,0x11000,0x12000,0,2) == EINVAL);
    child_start_error = ENSMEM;
    CHECK(launch_child(session,token,0x11000,0x12000,0,1) == ENSMEM);
    CHECK(!console_owner && !console_launcher && !text_console_active);
    child_start_error = 0; child_stay_live = TRUE;
    CHECK(cuis_display_lock(session,1,0) == E_OK);
    token = cuis_child_service(CUIS_CHILD_WATCH,session,CUIS_CHILD_RESERVE,0,0);
    CHECK(launch_child(session,token,0x11000,0x12000,0,1) == 2);
    CHECK(console_owner == &third && text_console_active && displayed_hidden);
    run = &third;
    child_session = cuis_service(CUIS_SESSION_START,0);
    CHECK(cuis_service(CUIS_SESSION_END,child_session) == E_OK);
    CHECK(console_owner == &third && text_console_active);
    run = &first;
    CHECK(!cuis_keyboard_event(0x1234));
    CHECK(cuis_display_lock(session,0,0) == E_OK);
    CHECK(cuis_vdi_policy() == EAGAIN && cuis_display_lock(session,1,0) == EAGAIN);
    run = &second;
    CHECK(cuis_vdi_policy() == EAGAIN && cuis_display_lock(other,1,0) == EAGAIN);
    other_token = cuis_child_service(CUIS_CHILD_WATCH,other,CUIS_CHILD_RESERVE,0,0);
    CHECK(launch_child(other,other_token,0x11000,0x12000,0,1) == EAGAIN);
    CHECK(cuis_child_service(CUIS_CHILD_WATCH,other,CUIS_CHILD_CANCEL,other_token,0) == EACCDN);
    /* The text process retains its lease when its launching GUI client exits. */
    cuis_process_exit(&first);
    CHECK(console_owner == &third && text_console_active);
    cuis_process_exit(&third);
    CHECK(!console_owner && !text_console_active && cuis_display_lock(other,1,0) == E_OK);
    CHECK(cuis_display_lock(other,0,0) == E_OK);
    cuis_process_exit(&third); /* fault/normal teardown may both pass here */
    cuis_process_exit(&second); run = &first; child_stay_live = FALSE;
    CHECK(!clients && allocations == frees);
    /* Parallel TOS children attach the console without an AES exit observer.
     * A full observation table cannot prevent that independent launch. */
    run = &first; session = cuis_service(CUIS_SESSION_START,0);
    for (index = 0; index < CUIS_CHILDREN_PER_SESSION; index++) {
        slots[index] = cuis_child_service(CUIS_CHILD_WATCH,session,CUIS_CHILD_RESERVE,0,0);
        CHECK(slots[index] > 0);
    }
    CHECK(launch_child(session,0,0x11000,0x12000,0,0) == EINVAL);
    child_start_error = ENSMEM;
    CHECK(launch_child(session,0,0x11000,0x12000,0,1) == ENSMEM);
    CHECK(!console_owner && !console_launcher && !text_console_active);
    child_start_error = 0; child_stay_live = TRUE;
    CHECK(launch_child(session,0,0x11000,0x12000,0,1) == 2);
    CHECK(console_owner == &third && text_console_active && !modeled_pid_reserved(2));
    cuis_process_exit(&third);
    CHECK(!console_owner && !text_console_active);
    CHECK(cuis_child_service(CUIS_CHILD_RESULT,session,0,0,0) == 0);
    CHECK(cuis_display_lock(session,1,0) == E_OK && cuis_display_lock(session,0,0) == E_OK);
    for (index = 0; index < CUIS_CHILDREN_PER_SESSION; index++)
        CHECK(cuis_child_service(CUIS_CHILD_WATCH,session,CUIS_CHILD_CANCEL,slots[index],0) == E_OK);
    cuis_process_exit(&first); child_stay_live = FALSE;
    CHECK(!clients && allocations == frees);
    return 0;
}

static int console_abandoned_launch_checks(void)
{
    LONG session, token;
    volatile WORD stage;
    CHECK(!clients && !console_owner && allocations == frees);
    child_stay_live = TRUE;
    for (stage = 1; stage <= 4; stage++) {
        run = &first;
        session = cuis_service(CUIS_SESSION_START,0);
        token = stage <= 2 ? cuis_child_service(CUIS_CHILD_WATCH,session,CUIS_CHILD_RESERVE,0,0) : 0;
        console_abort_stage = stage <= 2 ? stage : stage-2;
        if (!__builtin_setjmp(console_launch_escape)) {
            (void)launch_child(session,token,0x11000,0x12000,0,1);
            CHECK(FALSE);
        }
        CHECK(!console_abort_stage && !console_launcher);
        CHECK(!console_owner && !text_console_active && !*find_owner(&first));
        /* Another client must be able to launch after the abandoned call. */
        run = &second;
        session = cuis_service(CUIS_SESSION_START,0);
        token = cuis_child_service(CUIS_CHILD_WATCH,session,CUIS_CHILD_RESERVE,0,0);
        CHECK(launch_child(session,token,0x11000,0x12000,0,1) == 2);
        CHECK(console_owner == &third && text_console_active);
        cuis_process_exit(&third);
        cuis_process_exit(&second);
        CHECK(!clients && !console_owner && !text_console_active && allocations == frees);
    }
    child_stay_live = FALSE; run = &first;
    return 0;
}

static int button_resume_checks(void)
{
    LONG session, token, window;
    CUIS_CLIENT *client;
    int variant;
    CHECK(!clients && allocations == frees);
    run = &first; next_child_token = 0;
    session = cuis_service(CUIS_SESSION_START,0);
    client = *find_owner(run);
    pointer_buttons = 0; input_pointer_source = pointer_suppressed = pointer_legacy_drag = FALSE;
    window = cuis_window_service(CUIS_WINDOW_CREATE,0,0,300,300,0);
    CHECK(window >= 0 && cuis_window_service(CUIS_WINDOW_SHOW,window,0,0,0,0) == E_OK);
    for (variant = 0; variant < 2; variant++) {
        test_children[0].parent = run; test_children[0].pid = 10;
        test_children[0].reaped = test_children[0].started = FALSE; test_children[0].status = 0;
        token = cuis_child_service(CUIS_CHILD_WATCH,session,CUIS_CHILD_RESERVE,0,0);
        CHECK(CHILD_BIND(session,token,10) == E_OK);
        client->recent_count = 0;
        CHECK(cuis_input_config(CUIS_INPUT_BUTTON,session,1,1,2,0,0) == E_OK);
        input_sleeps = 0; input_packet_mode = 2; child_idle_at = 2;
        CHECK(cuis_session_wait(session,0,CUIS_READY_BUTTON|CUIS_READY_CHILD,0,0,0) == CUIS_READY_CHILD);
        CHECK(client->button_resume_active && client->button_resume.count == 1);
        CHECK(cuis_child_service(CUIS_CHILD_PUBLISH,session,(10L << 16)|0x0400,0x10000,16) == E_OK);
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
