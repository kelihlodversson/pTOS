/* Application-level predicates use trusted packets, never user injection. */
#define IW(h,e,t) cuis_session_wait(h,0,e,t)
#define IR(h) cuis_input_config(CUIS_INPUT_RESULT,h,0x1000,0x1004,0x1008,0x100c,0x1010)
#define IB(h,n,m,s,i) cuis_input_config(CUIS_INPUT_BUTTON,h,m,s,n,i,0)
#define IX(h,n,l,x,y,w,z) cuis_input_config(CUIS_INPUT_RECT,h,n+2*l,(UQUAD)(QUAD)x,(UQUAD)(QUAD)y,(UQUAD)(QUAD)w,(UQUAD)(QUAD)z)
static int input_event_checks(void)
{
    LONG session, other, window;
    ULONG before, mode, edge;
    run = &first; session = cuis_service(CUIS_SESSION_START, 0);
    run = &second; other = cuis_service(CUIS_SESSION_START, 0); run = &first;
    input_pointer_source = FALSE; pointer_suppressed = pointer_legacy_drag = FALSE;
    pointer_buttons = 0; input_modifiers = 5;
    window = cuis_window_service(CUIS_WINDOW_CREATE,0,0,300,300,0);
    CHECK(window >= 0 && cuis_window_service(CUIS_WINDOW_POLICY,window,1,0,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW,window,0,0,0,0) == E_OK);
    CHECK(IR(session) == E_OK && !copied[0] && !copied[1] && !copied[2] && !copied[3] && !copied[4]);
    CHECK(cuis_keyboard_event(0x001e0061));
    before = delay_yields;
    CHECK(IW(session,CUIS_READY_KEY|CUIS_READY_TIMER,0) == (CUIS_READY_KEY|CUIS_READY_TIMER));
    CHECK(delay_yields == before && !focus_window->key_count && IR(session) == E_OK && copied[0] == 0x001e0061);
    fail_copy_address = 0x1008;
    CHECK(IR(session) == EIMBA); fail_copy_address = 0;
    CHECK(IR(session) == E_OK && copied[0] == 0x001e0061);
    CHECK(cuis_input_config(CUIS_INPUT_RESULT,other,0,0,0,0,0) == EACCDN);
    input_sleeps = 0; input_inject_after = 1;
    CHECK(IW(session,CUIS_READY_KEY,0) == CUIS_READY_KEY && input_sleeps == 1);
    input_inject_after = 0;
    CHECK(IR(session) == E_OK && (ULONG)copied[0] == 0xff123456UL);
    CHECK(IX(session,0,0,0,0,300,300) == E_OK && IX(session,1,1,400,400,10,10) == E_OK);
    CHECK(IW(session,CUIS_READY_RECT1|CUIS_READY_RECT2|CUIS_READY_TIMER,0) ==
          (CUIS_READY_RECT1|CUIS_READY_RECT2|CUIS_READY_TIMER));
    CHECK(IX(session,0,0,0,0,-1,10) == E_OK);
    CHECK(IW(session,CUIS_READY_RECT1|CUIS_READY_TIMER,0) == CUIS_READY_TIMER);
    CHECK(cuis_input_config(CUIS_INPUT_RECT,session,4,0,0,1,1) == EINVAL);
    CHECK(cuis_input_config(CUIS_INPUT_RECT,session,0,1ULL<<32,0,1,1) == EINVAL);
    CHECK(IB(session,0,1,1,0) == E_OK && IB(session,256,1,1,0) == EINVAL);
    CHECK(IB(session,1,1,0x8000,0) == E_OK);
    /* Bits outside the mask do not affect GEM's equality predicate. */
    CHECK(IW(session,CUIS_READY_BUTTON,0) == CUIS_READY_BUTTON && IR(session) == E_OK && copied[1] == 1);
    CHECK(IB(session,1,1,1,1) == E_OK);
    CHECK(IW(session,CUIS_READY_BUTTON,0) == CUIS_READY_BUTTON);
    /* No lost press/release pair between application calls. */
    CHECK(cuis_pointer_packet((WORD)(10-pointer_x),(WORD)(10-pointer_y),1,NULL,NULL));
    CHECK(cuis_pointer_packet(0,0,0,NULL,NULL));
    CHECK(IB(session,1,1,1,0) == E_OK);
    CHECK(IW(session,CUIS_READY_BUTTON,0) == CUIS_READY_BUTTON);
    CHECK(IR(session) == E_OK && copied[1] == 1 && (copied[4] & 3) == 1 && !pointer_buttons);
    CHECK((*find_owner(run))->edge_count == 1);
    CHECK(IB(session,1,1,0,0) == E_OK);
    CHECK(IW(session,CUIS_READY_BUTTON,0) == CUIS_READY_BUTTON && !(*find_owner(run))->edge_count);
    /* Multiple queued clicks are returned in FIFO order, without discarding
     * the release or a later press after the first matching edge. */
    for (mode = 0; mode < 2; mode++) {
        before = delay_yields;
        for (edge = 0; edge < 4; edge++)
            CHECK(cuis_pointer_packet((WORD)(30 + edge - pointer_x),0,!(edge & 1),NULL,NULL));
        for (edge = 0; edge < (mode ? 4 : 2); edge++) {
            CHECK(IB(session,1,1,mode ? !(edge & 1) : 1,0) == E_OK);
            CHECK(IW(session,CUIS_READY_BUTTON|CUIS_READY_TIMER,0) == (CUIS_READY_BUTTON|CUIS_READY_TIMER));
            CHECK(IR(session) == E_OK && copied[1] == 1 && copied[2] == (LONG)(30 + (mode ? edge : 2 * edge)));
            CHECK((copied[4] & 3) == (LONG)(mode ? !(edge & 1) : 1));
            CHECK((*find_owner(run))->edge_count == 3 - (mode ? edge : 2 * edge));
        }
        if (!mode) {
            CHECK(IB(session,1,1,0,0) == E_OK);
            CHECK(IW(session,CUIS_READY_BUTTON|CUIS_READY_TIMER,0) == (CUIS_READY_BUTTON|CUIS_READY_TIMER));
            CHECK(IR(session) == E_OK && copied[2] == 33 && !(copied[4] & 3));
        }
        CHECK(!(*find_owner(run))->edge_count && delay_yields == before);
    }
    CHECK(IB(session,2,1,1,0) == E_OK);
    input_sleeps = 0; input_packet_mode = 1;
    CHECK(IW(session,CUIS_READY_BUTTON,0) == CUIS_READY_BUTTON && !input_packet_mode);
    CHECK(IR(session) == E_OK && copied[1] == 2 && (copied[4] & 3) == 1);
    CHECK(!pointer_buttons && input_sleeps >= 91 && input_sleeps <= 95);
    input_sleeps = 0; input_packet_mode = 4;
    CHECK(IW(session,CUIS_READY_BUTTON,0) == CUIS_READY_BUTTON && pointer_buttons == 1);
    CHECK(IR(session) == E_OK && copied[1] == 2);
    before = input_sleeps;
    CHECK(IW(session,CUIS_READY_BUTTON,0) == CUIS_READY_BUTTON && input_sleeps == before);
    CHECK(IR(session) == E_OK && copied[1] == 2);
    CHECK(IB(session,1,1,1,0) == E_OK);
    CHECK(IW(session,CUIS_READY_BUTTON,0) == CUIS_READY_BUTTON && IR(session) == E_OK && copied[1] == 1);
    CHECK(IB(session,0,1,1,0) == E_OK);
    CHECK(IW(session,CUIS_READY_BUTTON,0) == CUIS_READY_BUTTON && IR(session) == E_OK && !copied[1]);
    CHECK(IB(session,2,1,1,0) == E_OK);
    CHECK(IW(session,CUIS_READY_BUTTON,0) == CUIS_READY_BUTTON && input_sleeps == before);
    CHECK(IR(session) == E_OK && copied[1] == 2);
    CHECK(cuis_pointer_packet(0,0,0,NULL,NULL));
    /* A single click still completes after the click interval, rather than
     * waiting forever for a requested double click. Tick wrap is harmless. */
    input_ticks = 0xfffffffeUL; input_sleeps = 0; input_packet_mode = 2;
    CHECK(IW(session,CUIS_READY_BUTTON,0) == CUIS_READY_BUTTON);
    CHECK(IR(session) == E_OK && copied[1] == 1 && !input_packet_mode);
    /* Delay the waiter while multiple packets arrive within one IRQ-enabled
     * idle. A ready result must preserve later input, including a press after
     * an expired multi-click window. Repeat expiry across tick wrap. */
    for (mode = 5; mode <= 7; mode++) {
        CHECK(IB(session,mode == 5 ? 1 : 2,1,1,0) == E_OK);
        input_ticks = mode == 7 ? 0xfffffffeUL : 1000;
        input_sleeps = 0; input_packet_mode = mode == 5 ? 5 : 6;
        CHECK(IW(session,CUIS_READY_BUTTON,0) == CUIS_READY_BUTTON && input_sleeps == 1);
        CHECK(IR(session) == E_OK && copied[1] == 1 && copied[2] == 10 && (copied[4] & 3) == 1);
        CHECK((*find_owner(run))->edge_count != 0);
        CHECK(IW(session,CUIS_READY_BUTTON,0) == CUIS_READY_BUTTON && input_sleeps == 1);
        CHECK(IR(session) == E_OK && copied[1] == 1 && copied[2] == 20 && (copied[4] & 3) == 1);
        CHECK((*find_owner(run))->edge_count == 1 && !pointer_buttons);
        CHECK(IB(session,1,1,0,0) == E_OK);
        CHECK(IW(session,CUIS_READY_BUTTON,0) == CUIS_READY_BUTTON && !(*find_owner(run))->edge_count);
    }
    CHECK(IB(session,2,1,1,0) == E_OK);
    /* Internal maintenance wakes may service idle clients, but must not
     * split a real double-click wait into fresh per-call accumulations. */
    input_sleeps = 0; input_packet_mode = 1;
    CHECK(IW(session,CUIS_READY_BUTTON|CUIS_READY_SERVICE,0) == (CUIS_READY_BUTTON|CUIS_READY_SERVICE));
    CHECK(!input_packet_mode && input_sleeps >= 91 && input_sleeps <= 95);
    CHECK(IR(session) == E_OK && copied[1] == 2);
    input_sleeps = 0; input_packet_mode = 2;
    CHECK(IW(session,CUIS_READY_BUTTON|CUIS_READY_TIMER|CUIS_READY_SERVICE,10) == (CUIS_READY_TIMER|CUIS_READY_SERVICE));
    CHECK(input_sleeps == 3 && IR(session) == E_OK && !copied[1]);
    /* Another source cancels click accumulation without delaying its timer. */
    input_sleeps = 0; input_packet_mode = 2;
    CHECK(IW(session,CUIS_READY_BUTTON|CUIS_READY_TIMER,10) == CUIS_READY_TIMER && input_sleeps == 3);
    CHECK(IR(session) == E_OK && !copied[1] && !(*find_owner(run))->input_wait);
    /* A crossing reversed before userspace runs still wakes the waiter. */
    CHECK(IX(session,0,0,100,100,20,20) == E_OK);
    input_sleeps = 0; input_packet_mode = 3;
    CHECK(IW(session,CUIS_READY_RECT1,0) == CUIS_READY_RECT1 && input_sleeps == 1 && !input_packet_mode);
    CHECK(cuis_input_config(CUIS_INPUT_INTERVAL,session,165,1,0,0,0) == 165);
    run = &second;
    CHECK(cuis_input_config(CUIS_INPUT_INTERVAL,other,0,0,0,0,0) == 165);
    CHECK(IW(other,CUIS_READY_RECT1|CUIS_READY_BUTTON|CUIS_READY_KEY|CUIS_READY_TIMER,0) == CUIS_READY_TIMER);
    run = &first;
    CHECK(cuis_input_config(CUIS_INPUT_INTERVAL,session,450,1,0,0,0) == 450);
    cuis_process_exit(&first); cuis_process_exit(&second);
    CHECK(allocations == frees && !input_bad_idle && input_irqs);
    return 0;
}
/* The drag owner retains its release even when focus changes mid-drag. */
static int input_capture_checks(void)
{
    LONG session, a, b;
    ULONG before;
    CUIS_WINDOW *wa, *wb;
    run = &first;
    session = cuis_service(CUIS_SESSION_START, 0);
    a = cuis_window_service(CUIS_WINDOW_CREATE,0,0,100,100,0);
    CHECK(a >= 0 && cuis_window_service(CUIS_WINDOW_POLICY,a,1,0,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW,a,0,0,0,0) == E_OK);
    wa = (*find_owner(run))->windows;
    CHECK(point_at(10,10,1) && pointer_capture == wa && !pointer_activation_drag);
    run = &second;
    CHECK(cuis_service(CUIS_SESSION_START,0) >= 0);
    b = cuis_window_service(CUIS_WINDOW_CREATE,200,200,100,100,0);
    CHECK(b >= 0 && cuis_window_service(CUIS_WINDOW_POLICY,b,1,0,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW,b,0,0,0,0) == E_OK);
    wb = (*find_owner(run))->windows;
    CHECK(focus_window == wb && pointer_capture == wa);
    run = &first;
    CHECK(IB(session,1,1,0,0) == E_OK);
    input_sleeps = 0; input_packet_mode = 8;
    CHECK(IW(session,CUIS_READY_BUTTON|CUIS_READY_TIMER,10) == CUIS_READY_BUTTON && input_sleeps == 1);
    CHECK(IR(session) == E_OK && copied[1] == 1 && copied[2] == 210 && copied[3] == 210 && !(copied[4] & 3));
    CHECK(!pointer_capture && pointer_hover == wb && focus_window == wb);
    CHECK(!(*find_owner(&first))->edge_count && !(*find_owner(&second))->edge_count);
    /* A captured release queued before registration remains A's input,
     * despite B owning focus by the time A performs its history scan. */
    CHECK(cuis_window_service(CUIS_WINDOW_RAISE,a,0,0,0,0) == E_OK);
    CHECK(point_at(10,10,1) && pointer_capture == wa && !pointer_activation_drag);
    run = &second;
    CHECK(cuis_window_service(CUIS_WINDOW_RAISE,b,0,0,0,0) == E_OK);
    CHECK(focus_window == wb && pointer_capture == wa);
    CHECK(point_at(210,210,0) && !pointer_capture);
    CHECK((*find_owner(&first))->edge_count == 1 && !(*find_owner(&second))->edge_count);
    run = &first;
    before = delay_yields; input_sleeps = 0;
    CHECK(IW(session,CUIS_READY_BUTTON|CUIS_READY_TIMER,0) == (CUIS_READY_BUTTON|CUIS_READY_TIMER));
    CHECK(!input_sleeps && delay_yields == before && focus_window == wb);
    CHECK(IR(session) == E_OK && copied[1] == 1 && copied[2] == 210 && copied[3] == 210 && !(copied[4] & 3));
    CHECK(!(*find_owner(&first))->edge_count);
    /* A background activation drag is still only a WM_TOPPED gesture:
     * its release must not satisfy the background owner's button wait. */
    CHECK(point_at(10,10,1) && pointer_capture == wa && pointer_activation_drag);
    CHECK(focus_window == wb && (wa->notices & CUIS_NOTICE_ACTIVATE));
    input_sleeps = 0; input_packet_mode = 8;
    CHECK(IW(session,CUIS_READY_BUTTON|CUIS_READY_TIMER,10) == CUIS_READY_TIMER && input_sleeps == 3);
    CHECK(IR(session) == E_OK && !copied[1]);
    CHECK(!pointer_capture && !pointer_activation_drag && focus_window == wb);
    CHECK(!(*find_owner(&first))->edge_count);
    cuis_process_exit(&first); cuis_process_exit(&second);
    CHECK(allocations == frees && !input_bad_idle && input_irqs);
    return 0;
}
#undef IW
#undef IR
#undef IB
#undef IX
