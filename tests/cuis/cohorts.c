/* Opaque all-or-nothing requests and exit/consumption acknowledgements. */
#define CJ(fn,s,t,a,b,c,d) cuis_cohort_service(fn,s,t,a,b,c,d)
static ULONG cohort_idle_at;
static LONG cohort_click_owner, cohort_click_token;
static void cohort_idle_hook(void)
{
    if (cohort_idle_at && input_sleeps == cohort_idle_at) {
        PD *saved = run;
        cohort_idle_at = 0; run = &first;
        CJ(CUIS_COHORT_COMMIT,cohort_click_owner,cohort_click_token,0x10000,16,0,0);
        run = saved;
    }
}
static int cohort_checks(void)
{
    LONG a, b, c, token;
    stream_ring_count = 0;
    run = &first; a = cuis_service(CUIS_SESSION_START,0);
    run = &second; b = cuis_service(CUIS_SESSION_START,0);
    run = &third; c = cuis_service(CUIS_SESSION_START,0);
    run = &first;
    CHECK(cuis_display_lock(a,1,0) == E_OK);
    CHECK(CJ(CUIS_COHORT_BEGIN,a,0,0,0,0,0) == EAGAIN);
    CHECK(cuis_display_lock(a,0,0) == E_OK);
    fail_allocation = TRUE;
    CHECK(CJ(CUIS_COHORT_BEGIN,a,0,0,0,0,0) == ENSMEM && !cohort.owner);
    fail_allocation = FALSE;
    token = CJ(CUIS_COHORT_BEGIN,a,0,0,0,0,0);
    CHECK(token > 0 && cohort.remaining == 2);
    CHECK(CJ(CUIS_COHORT_QUERY,a,CUIS_COHORT_STATE,0,0,0,0) == 1);
    run = &fourth; CHECK(cuis_service(CUIS_SESSION_START,0) == EAGAIN);
    run = &second;
    CHECK(CJ(CUIS_COHORT_CANCEL,b,token,0,0,0,0) == EACCDN);
    CHECK(CJ(CUIS_COHORT_QUERY,b,CUIS_COHORT_STATE,0,0,0,0) == EACCDN);
    CHECK(CJ(CUIS_COHORT_QUERY,b,CUIS_COHORT_WORK,0,0,0,0) == 0);
    run = &first;
    memset(stream_input,0x35,16);
    CHECK(CJ(CUIS_COHORT_STAGE,a,token,b,0x10000,16,0) == E_OK);
    CHECK(CJ(CUIS_COHORT_COMMIT,a,token,0x10000,16,0,0) == EAGAIN);
    CHECK(CJ(CUIS_COHORT_STAGE,a,token,c,0x10000,16,1) == E_OK);
    CHECK(CJ(CUIS_COHORT_COMMIT,a,token,0x10000,16,0,0) == E_OK);
    run = &second;
    CHECK(CJ(CUIS_COHORT_QUERY,b,CUIS_COHORT_WORK,0,0,0,0) == CUIS_COHORT_REQUEST);
    CHECK(CJ(CUIS_COHORT_READ,b,token,0x20000,15,0,0) == ERANGE);
    CHECK(CJ(CUIS_COHORT_READ,b,token,0x20000,16,0,0) == 16 && stream_output[0] == 0x35);
    CHECK(CJ(CUIS_COHORT_PUBLISH,b,token,0,0,0,0) == E_OK);
    CHECK(cuis_session_wait(b,16,CUIS_READY_COHORT|CUIS_READY_STREAM,0,0,0) == CUIS_READY_STREAM);
    CHECK(cuis_stream_service(CUIS_STREAM_PENDING,b,0,0,0,0) == 16);
    CHECK(cuis_stream_service(CUIS_STREAM_PEEK,b,0x20000,16,0,0) == 16);
    CHECK(cuis_stream_service(CUIS_STREAM_PENDING,b,0,0,0,0) == 16);
    CHECK(CJ(CUIS_COHORT_QUERY,b,CUIS_COHORT_READ_OFFSET,0,0,0,0) == 0);
    CHECK(CJ(CUIS_COHORT_QUERY,b,CUIS_COHORT_READ_BYTES,0,0,0,0) == 16);
    CHECK(CJ(CUIS_COHORT_ACK,b,token,0,0,0,0) == EAGAIN);
    CHECK(cuis_stream_service(CUIS_STREAM_READ,b,0x20000,8,0,0) == 8);
    CHECK(CJ(CUIS_COHORT_QUERY,b,CUIS_COHORT_READ_OFFSET,0,0,0,0) == 0);
    CHECK(CJ(CUIS_COHORT_QUERY,b,CUIS_COHORT_READ_BYTES,0,0,0,0) == 8);
    CHECK(CJ(CUIS_COHORT_ACK,b,token,0,0,0,0) == EAGAIN);
    CHECK(cuis_stream_service(CUIS_STREAM_READ,b,0x20000,8,0,0) == 8);
    CHECK(CJ(CUIS_COHORT_QUERY,b,CUIS_COHORT_READ_OFFSET,0,0,0,0) == 8);
    CHECK(CJ(CUIS_COHORT_QUERY,b,CUIS_COHORT_WORK,0,0,0,0) == CUIS_COHORT_ACCEPT);
    CHECK(CJ(CUIS_COHORT_ACK,b,token,0,0,0,0) == E_OK && cohort.remaining == 1);
    CHECK(CJ(CUIS_COHORT_ACK,b,token,0,0,0,0) == EAGAIN && cohort.remaining == 1);
    run = &third; CHECK(cuis_service(CUIS_SESSION_END,c) == E_OK);
    run = &first;
    CHECK(CJ(CUIS_COHORT_QUERY,a,CUIS_COHORT_WORK,0,0,0,0) == CUIS_COHORT_RESULT);
    CHECK(CJ(CUIS_COHORT_PUBLISH,a,token,0,0,0,0) == E_OK);
    CHECK(cuis_stream_service(CUIS_STREAM_READ,a,0x20000,16,0,0) == 16);
    CHECK(CJ(CUIS_COHORT_QUERY,a,CUIS_COHORT_STATE,0,0,0,0) == CUIS_COHORT_FINISHED);
    run = &fourth; CHECK(cuis_service(CUIS_SESSION_START,0) == EAGAIN);
    run = &first; CHECK(CJ(CUIS_COHORT_CANCEL,a,token,0,0,0,0) == E_OK);
    run = &fourth; CHECK(cuis_service(CUIS_SESSION_START,0) >= 0); cuis_process_exit(&fourth);
    run = &first;
    token = CJ(CUIS_COHORT_BEGIN,a,0,0,0,0,0);
    CHECK(token > 0 && CJ(CUIS_COHORT_STAGE,a,token,b,0x10000,16,1) == E_OK);
    CHECK(CJ(CUIS_COHORT_COMMIT,a,token,0x10000,16,0,0) == E_OK);
    run = &second;
    memset(stream_input,0x71,16);
    CHECK(CJ(CUIS_COHORT_REJECT,b,token,0x10000,16,16,0) == EACCDN);
    CHECK(CJ(CUIS_COHORT_PUBLISH,b,token,0,0,0,0) == E_OK);
    CHECK(CJ(CUIS_COHORT_REJECT,b,token,0x10000,16,16,0) == EACCDN);
    CHECK(cuis_stream_service(CUIS_STREAM_READ,b,0x20000,8,0,0) == 8);
    CHECK(CJ(CUIS_COHORT_REJECT,b,token,0x10000,16,16,0) == EACCDN);
    CHECK(cuis_stream_service(CUIS_STREAM_READ,b,0x20000,8,0,0) == 8);
    CHECK(CJ(CUIS_COHORT_REJECT,b,token,0x10000,16,16,0) == E_OK);
    run = &first;
    CHECK(CJ(CUIS_COHORT_READ,a,token,0x20000,16,0,0) == 16 && stream_output[0] == 0x71);
    CHECK(CJ(CUIS_COHORT_PUBLISH,a,token,0,0,0,0) == E_OK && !cohort.owner);
    CHECK(cuis_stream_service(CUIS_STREAM_READ,a,0x20000,16,0,0) == 16);
    token = CJ(CUIS_COHORT_BEGIN,a,0,0,0,0,0);
    fail_copy = TRUE;
    CHECK(CJ(CUIS_COHORT_STAGE,a,token,b,0x10000,16,0) == EIMBA);
    fail_copy = FALSE;
    CHECK(CJ(CUIS_COHORT_STAGE,a,token,b,0x10000,16,0) == E_OK);
    CHECK(CJ(CUIS_COHORT_COMMIT,a,token,0x10000,16,0,0) == E_OK);
    run = &second;
    region_exit_on_output = TRUE;
    CHECK(CJ(CUIS_COHORT_READ,b,token,0x20000,16,0,0) == EIMBA);
    CHECK(!*find_owner(&second) && cohort.remaining == 0);
    run = &first;
    CHECK(CJ(CUIS_COHORT_QUERY,a,CUIS_COHORT_WORK,0,0,0,0) == CUIS_COHORT_RESULT);
    cuis_process_exit(&first);
    CHECK(!cohort.owner && !cuis_session_count() && allocations == frees && input_irqs);
    return 0;
}
static int cohort_click_checks(void)
{
    LONG member, window, ready;
    unsigned pass;
    stream_ring_count = 0;
    run = &first; cohort_click_owner = cuis_service(CUIS_SESSION_START,0);
    run = &second; member = cuis_service(CUIS_SESSION_START,0);
    window = cuis_window_service(CUIS_WINDOW_CREATE,0,0,300,300,0);
    CHECK(window >= 0 && cuis_window_service(CUIS_WINDOW_SHOW,window,0,0,0,0) == E_OK);
    cuis_input_config(CUIS_INPUT_INTERVAL,member,450,1,0,0,0);
    for (pass = 0; pass < 2; pass++) {
        pointer_buttons = 0; input_pointer_source = pointer_suppressed = pointer_legacy_drag = FALSE;
        run = &first;
        cohort_click_token = CJ(CUIS_COHORT_BEGIN,cohort_click_owner,0,0,0,0,0);
        CHECK(cohort_click_token > 0);
        CHECK(CJ(CUIS_COHORT_STAGE,cohort_click_owner,cohort_click_token,member,0x10000,16,1) == E_OK);
        run = &second;
        CHECK(cuis_input_config(CUIS_INPUT_BUTTON,member,1,1,2,0,0) == E_OK);
        input_sleeps = 0; input_packet_mode = pass ? 2 : 1; cohort_idle_at = 2;
        ready = cuis_session_wait(member,16,CUIS_READY_BUTTON|CUIS_READY_COHORT|
            (pass ? CUIS_READY_TIMER : 0),pass ? 10 : 0,0,0);
        CHECK((ULONG)ready == (CUIS_READY_COHORT | (pass ? CUIS_READY_TIMER : CUIS_READY_BUTTON)));
        CHECK(input_sleeps >= (pass ? 3 : 4));
        CHECK(cuis_input_config(CUIS_INPUT_RESULT,member,0x1000,0x1004,0x1008,0x100c,0x1010) == E_OK);
        CHECK(copied[1] == (pass ? 0 : 2));
        run = &first;
        CHECK(CJ(CUIS_COHORT_CANCEL,cohort_click_owner,cohort_click_token,0,0,0,0) == E_OK);
    }
    cuis_process_exit(&first); cuis_process_exit(&second);
    CHECK(!cuis_session_count() && allocations == frees && input_irqs);
    return 0;
}
static int cohort_origin_checks(void)
{
    LONG owner, member, token;
    unsigned pass;
    stream_ring_count = 0;
    run = &first; owner = cuis_service(CUIS_SESSION_START,0);
    run = &second; member = cuis_service(CUIS_SESSION_START,0);
    for (pass = 0; pass < 2; pass++) {
        run = &first;
        CHECK(cuis_stream_service(CUIS_STREAM_WRITE,member,0x10000,16,0,0) == 16);
        token = CJ(CUIS_COHORT_BEGIN,owner,0,0,0,0,0);
        CHECK(token > 0 && CJ(CUIS_COHORT_STAGE,owner,token,member,0x10000,32,1) == E_OK);
        CHECK(CJ(CUIS_COHORT_COMMIT,owner,token,0x10000,16,0,0) == E_OK);
        run = &second;
        CHECK(CJ(CUIS_COHORT_PUBLISH,member,token,0,0,0,0) == E_OK);
        /* Identical bytes from appl_write, including prefix/overlap reads,
         * cannot impersonate delivery from the staged opaque request. */
        CHECK(cuis_stream_service(CUIS_STREAM_READ,member,0x20000,pass ? 24 : 16,0,0) > 0);
        CHECK(CJ(CUIS_COHORT_QUERY,member,CUIS_COHORT_READ_OFFSET,0,0,0,0) == EFILNF);
        CHECK(cuis_stream_service(CUIS_STREAM_READ,member,0x20000,pass ? 8 : 16,0,0) > 0);
        CHECK(CJ(CUIS_COHORT_QUERY,member,CUIS_COHORT_READ_OFFSET,0,0,0,0) == (pass ? 8 : 0));
        CHECK(CJ(CUIS_COHORT_QUERY,member,CUIS_COHORT_READ_BYTES,0,0,0,0) == (pass ? 8 : 16));
        CHECK(cuis_stream_service(CUIS_STREAM_READ,member,0x20000,16,0,0) == 16);
        CHECK(CJ(CUIS_COHORT_QUERY,member,CUIS_COHORT_READ_OFFSET,0,0,0,0) == 16);
        run = &first;
        CHECK(CJ(CUIS_COHORT_CANCEL,owner,token,0,0,0,0) == E_OK);
        run = &second;
        CHECK(CJ(CUIS_COHORT_QUERY,member,CUIS_COHORT_READ_OFFSET,0,0,0,0) == EFILNF);
        CHECK(CJ(CUIS_COHORT_QUERY,member,CUIS_COHORT_READ_BYTES,0,0,0,0) == EFILNF);
    }
    cuis_process_exit(&first); cuis_process_exit(&second);
    CHECK(!cuis_session_count() && allocations == frees && input_irqs);
    return 0;
}
/* A published refusal outlives the cohort, but never inherits a new token. */
static int cohort_result_checks(void)
{
    LONG owner, member, old_token, new_token;
    stream_ring_count = 0;
    run = &first; owner = cuis_service(CUIS_SESSION_START,0);
    run = &second; member = cuis_service(CUIS_SESSION_START,0);
    run = &first;
    old_token = CJ(CUIS_COHORT_BEGIN,owner,0,0,0,0,0);
    CHECK(old_token > 0 && CJ(CUIS_COHORT_STAGE,owner,old_token,member,0x10000,16,1) == E_OK);
    CHECK(CJ(CUIS_COHORT_COMMIT,owner,old_token,0x10000,16,0,0) == E_OK);
    run = &second;
    CHECK(cuis_stream_service(CUIS_STREAM_WRITE,owner,0x10000,16,0,0) == 16);
    CHECK(CJ(CUIS_COHORT_REJECT,member,old_token,0x10000,16,0,0) == E_OK);
    run = &first;
    CHECK(CJ(CUIS_COHORT_PUBLISH,owner,old_token,0,0,0,0) == E_OK && !cohort.owner);
    new_token = CJ(CUIS_COHORT_BEGIN,owner,0,0,0,0,0);
    CHECK(new_token > old_token);
    CHECK(CJ(CUIS_COHORT_STAGE,owner,new_token,member,0x10000,16,1) == E_OK);
    CHECK(CJ(CUIS_COHORT_COMMIT,owner,new_token,0x10000,16,0,0) == E_OK);
    /* An identical ordinary-write prefix has no result provenance. */
    CHECK(cuis_stream_service(CUIS_STREAM_READ,owner,0x20000,16,0,0) == 16);
    CHECK(CJ(CUIS_COHORT_QUERY,owner,CUIS_COHORT_RESULT_ID,0,0,0,0) == EFILNF);
    CHECK(cuis_stream_service(CUIS_STREAM_READ,owner,0x20000,16,0,0) == 16);
    CHECK(CJ(CUIS_COHORT_QUERY,owner,CUIS_COHORT_RESULT_ID,0,0,0,0) == old_token);
    CHECK(CJ(CUIS_COHORT_QUERY,owner,CUIS_COHORT_RESULT_OFFSET,0,0,0,0) == 0);
    CHECK(CJ(CUIS_COHORT_QUERY,owner,CUIS_COHORT_RESULT_BYTES,0,0,0,0) == 16);
    CHECK(CJ(CUIS_COHORT_QUERY,owner,CUIS_COHORT_ID,0,0,0,0) == new_token);
    run = &second;
    CHECK(CJ(CUIS_COHORT_QUERY,member,CUIS_COHORT_RESULT_ID,0,0,0,0) == EFILNF);
    CHECK(CJ(CUIS_COHORT_REJECT,member,new_token,0x10000,16,0,0) == E_OK);
    run = &first;
    CHECK(CJ(CUIS_COHORT_PUBLISH,owner,new_token,0,0,0,0) == E_OK && !cohort.owner);
    CHECK(cuis_stream_service(CUIS_STREAM_READ,owner,0x20000,8,0,0) == 8);
    CHECK(CJ(CUIS_COHORT_QUERY,owner,CUIS_COHORT_RESULT_ID,0,0,0,0) == new_token);
    CHECK(CJ(CUIS_COHORT_QUERY,owner,CUIS_COHORT_RESULT_BYTES,0,0,0,0) == 8);
    CHECK(cuis_stream_service(CUIS_STREAM_READ,owner,0x20000,8,0,0) == 8);
    CHECK(CJ(CUIS_COHORT_QUERY,owner,CUIS_COHORT_RESULT_OFFSET,0,0,0,0) == 8);
    run = &second;
    CHECK(cuis_stream_service(CUIS_STREAM_WRITE,owner,0x10000,16,0,0) == 16);
    run = &first;
    CHECK(cuis_stream_service(CUIS_STREAM_READ,owner,0x20000,16,0,0) == 16);
    CHECK(CJ(CUIS_COHORT_QUERY,owner,CUIS_COHORT_RESULT_ID,0,0,0,0) == EFILNF);
    /* Re-encoding a short opaque rejection must obey the normalized size's
     * backpressure, and a failed copy must leave the original result intact. */
    run = &second;
    CHECK(cuis_stream_service(CUIS_STREAM_WRITE,owner,0x10000,32767,0,0) == 32767);
    CHECK(cuis_stream_service(CUIS_STREAM_WRITE,owner,0x10000,32761,0,0) == 32761);
    run = &first;
    new_token = CJ(CUIS_COHORT_BEGIN,owner,0,0,0,0,0);
    CHECK(new_token > 0 && CJ(CUIS_COHORT_STAGE,owner,new_token,member,0x10000,16,1) == E_OK);
    CHECK(CJ(CUIS_COHORT_COMMIT,owner,new_token,0x10000,16,0,0) == E_OK);
    run = &second;
    CHECK(CJ(CUIS_COHORT_REJECT,member,new_token,0x10000,1,0,0) == E_OK);
    run = &first;
    CHECK(CJ(CUIS_COHORT_PUBLISH,owner,new_token,0x10000,129,0,0) == EINVAL);
    fail_copy = TRUE;
    CHECK(CJ(CUIS_COHORT_PUBLISH,owner,new_token,0x10000,16,0,0) == EIMBA && cohort.bytes == 1);
    fail_copy = FALSE;
    CHECK(CJ(CUIS_COHORT_PUBLISH,owner,new_token,0x10000,16,0,0) == EAGAIN && cohort.bytes == 16);
    CHECK(CJ(CUIS_COHORT_QUERY,owner,CUIS_COHORT_WORK,0,0,0,0) == 0);
    CHECK(cuis_stream_service(CUIS_STREAM_READ,owner,0x20000,16,0,0) == 16);
    CHECK(CJ(CUIS_COHORT_PUBLISH,owner,new_token,0,0,0,0) == E_OK && !cohort.owner);
    cuis_process_exit(&first); cuis_process_exit(&second);
    CHECK(!cuis_session_count() && allocations == frees && input_irqs);
    return 0;
}
#undef CJ
