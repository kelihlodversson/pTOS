/* Production completion ownership, ordering, capacity and atomic publication. */
static struct { PD *parent; WORD pid; LONG status; BOOL reaped; } test_children[3];
static ULONG child_reaps;
static LONG child_session, child_token;
static int child_copy_mode;
static ULONG child_idle_at;
static void child_idle_hook(void)
{
    if (child_idle_at && input_sleeps == child_idle_at) {
        child_idle_at = 0;
        test_children[0].status = (10L << 16) | 0x0400;
        cuis_child_completed(&first,10,0x0400);
    }
}
LONG ksched_child_status(WORD pid)
{
    int i;
    for (i = 0; i < 3; i++)
        if (test_children[i].parent == run && test_children[i].pid == pid && !test_children[i].reaped)
            return test_children[i].status;
    return EFILNF;
}
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
        child_token = CHILD_W(child_session,CUIS_CHILD_RESERVE,0,0);
        CHILD_W(child_session,CUIS_CHILD_BIND,child_token,10);
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
    CHECK(CHILD_W(child_session,CUIS_CHILD_BIND,child_token,12) == EFILNF);
    CHECK(CHILD_W(child_session,CUIS_CHILD_BIND,child_token,32768) == EINVAL);
    CHECK(CHILD_W(child_session,CUIS_CHILD_BIND,child_token,10) == E_OK && cuis_child_pid_reserved(10));
    tokens[0] = CHILD_W(child_session,CUIS_CHILD_RESERVE,0,0);
    CHECK(CHILD_W(child_session,CUIS_CHILD_BIND,tokens[0],10) == EACCDN);
    CHECK(CHILD_W(child_session,CUIS_CHILD_BIND,tokens[0],11) == E_OK);
    CHECK(CHILD_R(child_session) == 0 && !child_reaps);
    /* Exit ordering, not reservation order, determines the notification head. */
    second_status = test_children[1].status = (11L << 16) | 0x1234;
    cuis_child_completed(&first,11,0x1234);
    status = test_children[0].status = (10L << 16) | 0x0400;
    cuis_child_completed(&first,10,0x0400);
    cuis_child_completed(&first,10,0x0400);
    CHECK(CHILD_R(child_session) == second_status);
    CHECK(cuis_session_wait(child_session,16,CUIS_READY_CHILD|CUIS_READY_TIMER,0) == (CUIS_READY_CHILD|CUIS_READY_TIMER));
    CHECK(CHILD_P(child_session,status,0x10000,16) == EACCDN);
    CHECK(CHILD_P(child_session,second_status,0x10000,33) == EINVAL);
    fail_copy = TRUE;
    CHECK(CHILD_P(child_session,second_status,0x10000,16) == EIMBA); fail_copy = FALSE;
    fail_allocation = TRUE;
    CHECK(CHILD_P(child_session,second_status,0x10000,16) == ENSMEM); fail_allocation = FALSE;
    CHECK(CHILD_R(child_session) == second_status && !child_reaps && cuis_child_pid_reserved(11));
    /* An explicit MiNT wait cannot erase an already retained notification. */
    CHECK(ksched_wait(11,1,0) == second_status && child_reaps == 1);
    memset(stream_input,0x31,16);
    CHECK(CHILD_P(child_session,second_status,0x10000,16) == E_OK && !cuis_child_pid_reserved(11));
    client = *find_owner(run);
    client->stream_bytes = CUIS_STREAM_BYTES-1;
    CHECK(CHILD_P(child_session,status,0x10000,16) == EAGAIN && child_reaps == 1 && CHILD_R(child_session) == status);
    client->stream_bytes = 16;
    CHECK(CHILD_P(child_session,status,0x10000,16) == E_OK && child_reaps == 2);
    CHECK(CHILD_R(child_session) == 0 && !cuis_child_pid_reserved(10));
    CHECK(cuis_stream_service(CUIS_STREAM_READ,child_session,0x20000,32,0,0) == 32);
    CHECK(stream_output[0] == 0x31 && stream_output[31] == 0x31);
    /* A public completion must wake MU_MESAG during an unfinished double click,
     * without waiting for the click interval/another mouse packet. */
    test_children[0].reaped = FALSE; test_children[0].status = 0;
    child_token = CHILD_W(child_session,CUIS_CHILD_RESERVE,0,0);
    CHECK(CHILD_W(child_session,CUIS_CHILD_BIND,child_token,10) == E_OK);
    pointer_buttons = 0; input_pointer_source = pointer_suppressed = pointer_legacy_drag = FALSE;
    window = cuis_window_service(CUIS_WINDOW_CREATE,0,0,300,300,0);
    CHECK(window >= 0 && cuis_window_service(CUIS_WINDOW_SHOW,window,0,0,0,0) == E_OK);
    CHECK(cuis_input_config(CUIS_INPUT_BUTTON,child_session,1,1,2,0,0) == E_OK);
    input_sleeps = 0; input_packet_mode = 2; child_idle_at = 2;
    CHECK(cuis_session_wait(child_session,0,CUIS_READY_BUTTON|CUIS_READY_CHILD,0) == CUIS_READY_CHILD);
    CHECK(input_sleeps == 2 && !input_packet_mode && !client->input_wait);
    CHECK(CHILD_W(child_session,CUIS_CHILD_CANCEL,child_token,0) == E_OK);
    /* Completion before bind is recovered from the owned native zombie. */
    test_children[0].reaped = FALSE;
    child_token = CHILD_W(child_session,CUIS_CHILD_RESERVE,0,0);
    CHECK(CHILD_W(child_session,CUIS_CHILD_BIND,child_token,10) == E_OK && CHILD_R(child_session) == status);
    /* Replacing the same PID/status while copying must invalidate the old token. */
    child_copy_mode = 1;
    CHECK(CHILD_P(child_session,status,0x10000,16) == EACCDN && CHILD_R(child_session) == status);
    CHECK(CHILD_P(child_session,status,0x10000,16) == E_OK && child_reaps == 3);
    test_children[0].reaped = FALSE;
    child_token = CHILD_W(child_session,CUIS_CHILD_RESERVE,0,0);
    CHECK(CHILD_W(child_session,CUIS_CHILD_BIND,child_token,10) == E_OK);
    child_copy_mode = 2;
    CHECK(CHILD_P(child_session,status,0x10000,16) == EACCDN && !cuis_child_pid_reserved(10));
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
