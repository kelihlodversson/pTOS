/* Trusted keyboard delivery through active session waits and result snapshots. */
#define KEY_POLL(s) cuis_session_wait(s,0,CUIS_READY_KEY|CUIS_READY_TIMER,0)
#define KEY_RESULT(s) cuis_input_config(CUIS_INPUT_RESULT,s,0x1000,0x1004,0x1008,0x100c,0x1010)
static int input_checks(void)
{
    LONG a, b, session;
    ULONG i;
    run = &first;
    CHECK(!cuis_keyboard_event(123));
    session = cuis_service(CUIS_SESSION_START,0);
    a = cuis_window_service(CUIS_WINDOW_CREATE,0,0,10,10,0);
    CHECK(a >= 0 && cuis_window_service(CUIS_WINDOW_POLICY,a,1,0,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW,a,0,0,0,0) == E_OK);
    CHECK(cuis_event_service(a,CUIS_EVENT_KEY,0) == 0);
    CHECK(cuis_event_service(a,CUIS_EVENT_KEY,(UQUAD)-2) == EINVAL);
    CHECK(cuis_event_service(0x100000000ULL | (ULONG)a,CUIS_EVENT_KEY,0) == EINVAL);
    CHECK(cuis_keyboard_event(0));
    CHECK(KEY_POLL(session) == (CUIS_READY_KEY|CUIS_READY_TIMER));
    CHECK(KEY_RESULT(session) == E_OK && copied[0] == 0);
    input_irqs = FALSE;
    CHECK(cuis_keyboard_event(0xffffffffUL) && !input_irqs);
    CHECK(KEY_POLL(session) == (CUIS_READY_KEY|CUIS_READY_TIMER) && !input_irqs);
    input_irqs = TRUE;
    CHECK(KEY_RESULT(session) == E_OK && (ULONG)copied[0] == 0xffffffffUL);
    for (i = 0; i < CUIS_KEY_CAPACITY+3; i++) CHECK(cuis_keyboard_event(i+1));
    for (i = 0; i < CUIS_KEY_CAPACITY; i++) {
        CHECK(KEY_POLL(session) == (CUIS_READY_KEY|CUIS_READY_TIMER));
        if (!i) {
            CHECK(cuis_input_config(CUIS_INPUT_RESULT,session,0,0x1004,0x1008,0x100c,0x1010) == EIMBA);
            fail_copy = TRUE; CHECK(KEY_RESULT(session) == EIMBA); fail_copy = FALSE;
            /* Rejected delivery does not overwrite the committed snapshot. */
        }
        CHECK(KEY_RESULT(session) == E_OK && (ULONG)copied[0] == i+1);
        CHECK(KEY_RESULT(session) == E_OK && (ULONG)copied[0] == i+1);
    }
    CHECK(KEY_POLL(session) == CUIS_READY_TIMER);
    input_sleeps=0;
    CHECK(cuis_event_service(a,CUIS_EVENT_KEY,6) == 0 && input_sleeps == 3);
    input_ticks=0xfffffffeUL; input_sleeps=0;
    CHECK(cuis_event_service(a,CUIS_EVENT_KEY,10) == 0 && input_ticks == 1);
    input_sleeps=0; input_inject_after=1;
    CHECK(cuis_event_service(a,CUIS_EVENT_KEY,100) == 1 && input_sleeps == 1);
    CHECK(KEY_POLL(session) == (CUIS_READY_KEY|CUIS_READY_TIMER));
    CHECK(KEY_RESULT(session) == E_OK && (ULONG)copied[0] == 0xff123456UL);
    input_inject_after=0; input_drop_focus=TRUE;
    CHECK(cuis_event_service(a,CUIS_EVENT_KEY,10) == EACCDN);
    CHECK(!cuis_keyboard_event(88));
    CHECK(cuis_window_service(CUIS_WINDOW_RAISE,a,0,0,0,0) == E_OK);
    CHECK(cuis_keyboard_event(123));
    run=&second;
    b=cuis_service(CUIS_SESSION_START,0);
    CHECK(KEY_RESULT(session) == EACCDN);
    CHECK(cuis_event_service(a,CUIS_EVENT_KEY,0) == EACCDN);
    a=cuis_window_service(CUIS_WINDOW_CREATE,0,0,10,10,0);
    CHECK(a >= 0 && cuis_window_service(CUIS_WINDOW_POLICY,a,1,0,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW,a,0,0,0,0) == E_OK);
    run=&first; CHECK(cuis_keyboard_event(456)); /* Route by focus, not interrupted PROCESS. */
    run=&second;
    CHECK(KEY_POLL(b) == (CUIS_READY_KEY|CUIS_READY_TIMER));
    CHECK(KEY_RESULT(b) == E_OK && copied[0] == 456);
    cuis_process_exit(&second); run=&first;
    CHECK(KEY_POLL(session) == CUIS_READY_TIMER); /* Focus loss discarded old keys. */
    cuis_process_exit(&first);
    CHECK(allocations == frees && !input_bad_idle);
    return 0;
}
#undef KEY_RESULT
#undef KEY_POLL
