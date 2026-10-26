/* Kernel notices stay generic; only libaes builds AES message bytes. */
#define NP(s) cuis_notice_service(CUIS_NOTICE_PEEK,s,0x1000,0x1004,0,0)
#define PUB(s,w,k) cuis_notice_service(CUIS_NOTICE_PUBLISH,s,w,k,0x10000,16)
static int notice_checks(void)
{
    LONG a, b, sa, sb, result;
    ULONG before, yields;
    CUIS_WINDOW *wa, *wb;
    next_handle = next_window_handle = 0;
    run = &first; sa = cuis_service(CUIS_SESSION_START, 0);
    a = cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 100, 100, 0);
    CHECK(a >= 0 && cuis_window_service(CUIS_WINDOW_POLICY, a, 2, 0, 0, 0) == EINVAL);
    CHECK(cuis_window_service(CUIS_WINDOW_POLICY, a, 1, 0, 0, 0) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW, a, 0, 0, 0, 0) == E_OK);
    wa = (*find_owner(run))->windows;
    CHECK(focus_window == wa && wa->notices == CUIS_NOTICE_REDRAW);
    run = &second; sb = cuis_service(CUIS_SESSION_START, 0);
    b = cuis_window_service(CUIS_WINDOW_CREATE, 50, 50, 100, 100, 0);
    CHECK(b >= 0 && cuis_window_service(CUIS_WINDOW_POLICY, b, 1, 0, 0, 0) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW, b, 0, 0, 0, 0) == E_OK);
    wb = (*find_owner(run))->windows;
    CHECK(focus_window == wb && window_front == wb);
    CHECK(cuis_stack_service(CUIS_STACK_QUERY, 0, 0x1000, 0x1004, 0x1008, 0) == E_OK);
    CHECK(copied[0] == wb->identifier && copied[1] == 2 && copied[2] == wa->identifier);
    CHECK(cuis_stack_service(CUIS_WINDOW_INFO, wa->identifier, 0x1000, 0x1004, 0x1008, 0x100c) == E_OK);
    CHECK(copied[0] == 1 && copied[1] == 1 && copied[2] == wb->identifier && copied[3] == 0);
    run = &first;
    yields = delay_yields;
    CHECK(cuis_session_wait(sa, 16, CUIS_READY_NOTICE | CUIS_READY_TIMER, 0) ==
          (CUIS_READY_NOTICE | CUIS_READY_TIMER) && delay_yields == yields);
    CHECK(NP(sa) == CUIS_NOTICE_REDRAW && copied[0] == a && copied[1] == wa->identifier);
    fail_copy = TRUE;
    CHECK(NP(sa) == EIMBA && PUB(sa, a, CUIS_NOTICE_REDRAW) == EIMBA && wa->notices);
    fail_copy = FALSE;
    CHECK(PUB(sa, b, CUIS_NOTICE_REDRAW) == EACCDN);
    before = allocations;
    fail_allocation = TRUE;
    CHECK(PUB(sa, a, CUIS_NOTICE_REDRAW) == ENSMEM && wa->notices);
    fail_allocation = FALSE;
    CHECK(allocations == before);
    stream_input[0] = 77;
    CHECK(PUB(sa, a, CUIS_NOTICE_REDRAW) == E_OK && !wa->notices);
    CHECK(cuis_stream_service(CUIS_STREAM_READ, sa, 0x20000, 16, 0, 0) == 16 && stream_output[0] == 77);
    CHECK(cuis_notice_service(CUIS_NOTICE_PEEK, sa, 0, 0, 0, 0) == 0);
    damage_windows(); notice_withdraw_on_copy = TRUE;
    CHECK(PUB(sa, a, CUIS_NOTICE_REDRAW) == EACCDN && !input_bad_idle);
    CHECK(!(*find_owner(run))->stream_bytes && !wa->notices);
    /* Clicking a visible part of the background requests topping, not focus. */
    input_pointer_source = FALSE; wa->pointer_head = wa->pointer_count = 0;
    pointer_initialized = TRUE; pointer_seen = TRUE; pointer_width = pointer_height = 200;
    pointer_x = pointer_y = 10; pointer_buttons = 0;
    pointer_capture = pointer_hover = NULL; pointer_suppressed = pointer_legacy_drag = FALSE;
    CHECK(cuis_pointer_packet(0, 0, 1, NULL, NULL));
    CHECK(focus_window == wb && window_front == wb && wa->notices == CUIS_NOTICE_ACTIVATE && !wa->pointer_count);
    CHECK(cuis_pointer_packet(0, 0, 0, NULL, NULL) && !pointer_activation_drag && !pointer_capture);
    CHECK(cuis_keyboard_event(0x1234) && wb->key_count == 1 && !wa->key_count);
    damage_windows();
    CHECK(wa->notices == (CUIS_NOTICE_ACTIVATE | CUIS_NOTICE_REDRAW) && NP(sa) == CUIS_NOTICE_ACTIVATE);
    (*find_owner(run))->stream_bytes = CUIS_STREAM_BYTES;
    CHECK(PUB(sa, a, CUIS_NOTICE_ACTIVATE) == EAGAIN && wa->notices == (CUIS_NOTICE_ACTIVATE | CUIS_NOTICE_REDRAW));
    (*find_owner(run))->stream_bytes = 0;
    CHECK(PUB(sa, a, CUIS_NOTICE_ACTIVATE) == E_OK && wa->notices == CUIS_NOTICE_REDRAW);
    CHECK(PUB(sa, a, CUIS_NOTICE_REDRAW) == E_OK && !wa->notices);
    CHECK(cuis_window_service(CUIS_WINDOW_RAISE, a, 0, 0, 0, 0) == E_OK && focus_window == wa && window_front == wa);
    CHECK(!wb->key_count && cuis_keyboard_event(0x5678) && wa->key_count == 1);
    CHECK(cuis_window_service(CUIS_WINDOW_LOWER, a, 0, 0, 0, 0) == E_OK && focus_window == wb && window_front == wb);
    run = &second;
    CHECK(cuis_display_lock(sb, 1, 1) == E_OK);
    run = &first;
    CHECK(cuis_window_service(CUIS_WINDOW_RAISE, a, 0, 0, 0, 0) == EAGAIN);
    run = &second;
    CHECK(cuis_display_lock(sb, 0, 0) == E_OK);
    cuis_process_exit(&second);
    CHECK(focus_window == wa && window_front == wa);
    run = &first;
    CHECK(cuis_window_service(CUIS_WINDOW_HIDE, a, 0, 0, 0, 0) == E_OK && !wa->notices);
    CHECK(cuis_stack_service(CUIS_STACK_QUERY, 0, 0x1000, 0x1004, 0x1008, 0) == E_OK && !copied[0]);
    result = cuis_service(CUIS_SESSION_END, sa);
    CHECK(result == E_OK && allocations == frees && !pointer_activation_drag);
    return 0;
}
#undef NP
#undef PUB
