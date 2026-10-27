/* Modal pointer ownership is session-wide and independent of draw/focus. */
#define ML(s,a,b) cuis_mouse_lock(s,a,b)
#define MW(s,e) cuis_session_wait(s,0,e|CUIS_READY_TIMER,0)
#define MR(s) cuis_input_config(CUIS_INPUT_RESULT,s,0x1000,0x1004,0x1008,0x100c,0x1010)
static int mouse_control_checks(void)
{
    LONG a, b, window, own;
    CUIS_CLIENT *ca, *cb;
    CUIS_WINDOW *wb;
    ULONG before;
    run = &first; a = cuis_service(CUIS_SESSION_START,0); ca = *find_owner(run);
    run = &second; b = cuis_service(CUIS_SESSION_START,0); cb = *find_owner(run);
    window = cuis_window_service(CUIS_WINDOW_CREATE,200,200,100,100,0);
    CHECK(window >= 0 && cuis_window_service(CUIS_WINDOW_POLICY,window,1,0,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW,window,0,0,0,0) == E_OK);
    wb = cb->windows;
    run = &first;
    CHECK(ML(1ULL<<32,1,0) == EINVAL && ML(a,2,0) == EINVAL && ML(a,1,2) == EINVAL && ML(a,0,1) == EINVAL);
    CHECK(ML(a,0,0) == EACCDN && ML(b,1,0) == EACCDN);
    CHECK(ML(a,1,1) == E_OK && ML(a,1,0) == E_OK && mouse_depth == 2 && focus_window == wb);
    CHECK(cuis_mouse_active() && !ca->windows);
    run = &second; CHECK(pointer_drain(window)); run = &first;
    CHECK(point_at(210,210,1) && focus_window == wb && !pointer_activation_drag);
    CHECK(ca->edge_count == 1 && !cb->edge_count && !wb->pointer_count);
    CHECK(cuis_keyboard_event(0x1234) && wb->key_count == 1);
    run = &second;
    CHECK(ML(b,1,0) == EAGAIN && ML(b,0,0) == EACCDN);
    CHECK(cuis_window_service(CUIS_UPDATE_BEGIN,window,0,0,0,0) == E_OK);
    input_sleeps = 0;
    CHECK(ML(b,1,1) == EAGAIN && !input_sleeps);
    CHECK(cuis_window_service(CUIS_UPDATE_END,window,0,0,0,0) == E_OK);
    CHECK(cuis_display_lock(b,1,0) == E_OK && cuis_vdi_policy() == 1);
    run = &first;
    CHECK(cuis_vdi_policy() == EAGAIN); /* Grabs do not grant drawing permission. */
    /* A owns mouse control, B owns BEG_UPDATE and may be waiting for A's
     * mouse. A's redraw must fail before sleeping; use a fake display release
     * so the old blocking implementation fails the test rather than hangs. */
    display_release_owner = &second; input_sleeps = 0;
    CHECK(cuis_display_lock(a,1,1) == EACCDN && !input_sleeps);
    CHECK(mouse_owner == ca && mouse_depth == 2 && display_owner == cb && display_depth == 1);
    CHECK(cuis_display_lock(a,1,0) == EAGAIN);
    display_release_owner = NULL;
    run = &second; CHECK(cuis_display_lock(b,0,0) == E_OK);
    run = &first;
    /* A windowless modal owner captures keyboard only when explicitly asked,
     * retaining the foreign focused window and its queued keys. */
    CHECK(cuis_keyboard_lock(a,1) == EACCDN);
    CHECK(cuis_display_lock(a,1,1) == E_OK);
    CHECK(cuis_keyboard_lock(1ULL<<32,1) == EINVAL && cuis_keyboard_lock(a,2) == EINVAL);
    CHECK(cuis_keyboard_lock(b,1) == EACCDN && cuis_keyboard_lock(a,0) == EACCDN);
    CHECK(cuis_keyboard_lock(a,1) == E_OK && cuis_keyboard_lock(a,1) == E_OK && keyboard_depth == 2);
    CHECK(cuis_keyboard_event(0x1e0061) && ca->key_count == 1 && wb->key_count == 1 && focus_window == wb);
    run = &second; CHECK(MW(b,CUIS_READY_KEY) == CUIS_READY_TIMER); run = &first;
    CHECK(MW(a,CUIS_READY_KEY) == (CUIS_READY_KEY|CUIS_READY_TIMER));
    CHECK(MR(a) == E_OK && copied[0] == 0x1e0061 && !ca->key_count);
    CHECK(cuis_keyboard_lock(a,0) == E_OK && keyboard_depth == 1);
    for (before = 0; before < CUIS_KEY_CAPACITY+1; before++) CHECK(cuis_keyboard_event(before));
    CHECK(ca->key_count == CUIS_KEY_CAPACITY);
    CHECK(cuis_keyboard_lock(a,0) == E_OK && !keyboard_owner && !ca->key_count && wb->key_count == 1);
    CHECK(cuis_keyboard_lock(a,1) == E_OK && cuis_keyboard_event(0x2e0063));
    CHECK(cuis_display_lock(a,0,0) == E_OK && !keyboard_owner && !ca->key_count && mouse_depth == 2);
    CHECK(cuis_keyboard_lock(a,0) == EACCDN);
    own = cuis_window_service(CUIS_WINDOW_CREATE,0,0,100,100,0);
    CHECK(own >= 0 && cuis_window_service(CUIS_WINDOW_POLICY,own,1,0,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW,own,0,0,0,0) == E_OK);
    input_sleeps = 0;
    CHECK(cuis_window_service(CUIS_UPDATE_BEGIN,own,0,0,0,0) == E_OK);
    CHECK(cuis_display_lock(a,1,1) == EAGAIN && !input_sleeps);
    CHECK(cuis_window_service(CUIS_UPDATE_END,own,0,0,0,0) == E_OK);
    CHECK(cuis_display_lock(a,1,1) == E_OK && cuis_display_lock(a,1,1) == E_OK && display_depth == 2);
    CHECK(cuis_display_lock(a,0,0) == E_OK && cuis_display_lock(a,0,0) == E_OK && mouse_depth == 2);

    CHECK(cuis_display_redraw(a,0,0,100,100) == EACCDN);
    CHECK(cuis_display_lock(a,1,0) == E_OK);
    ca->windows->redraw = wb->redraw = FALSE;
    CHECK(cuis_display_redraw(b,0,0,100,100) == EACCDN);
    CHECK(cuis_display_redraw(a,0,0,32768,1) == EINVAL);
    CHECK(cuis_display_redraw(a,(UQUAD)(QUAD)-10,(UQUAD)(QUAD)-10,20,20) == E_OK);
    CHECK(ca->windows->redraw && !wb->redraw);
    CHECK(cuis_display_lock(a,0,0) == E_OK);

    run = &second;
    CHECK(cuis_window_service(CUIS_WINDOW_RAISE,window,0,0,0,0) == E_OK && ca->edge_count == 1);
    run = &first;
    CHECK(cuis_window_service(CUIS_WINDOW_HIDE,own,0,0,0,0) == E_OK && !pointer_suppressed && ca->edge_count == 1);
    CHECK(point_at(210,210,0) && ca->edge_count == 2 && !cb->edge_count);
    CHECK(cuis_input_config(CUIS_INPUT_BUTTON,a,1,0,1,0,0) == E_OK);
    CHECK(cuis_input_config(CUIS_INPUT_RECT,a,0,200,200,100,100) == E_OK);
    CHECK(MW(a,CUIS_READY_BUTTON|CUIS_READY_RECT1) == (CUIS_READY_BUTTON|CUIS_READY_RECT1|CUIS_READY_TIMER));
    CHECK(MR(a) == E_OK && copied[1] == 1 && copied[2] == 210 && copied[3] == 210 && !(copied[4]&3));
    CHECK(!ca->edge_count && focus_window == wb);
    CHECK(point_at(900,700,1) && focus_window == wb && !cb->edge_count);
    CHECK(ML(a,0,0) == E_OK && mouse_depth == 1 && mouse_owner == ca);
    CHECK(ML(a,0,0) == E_OK && !mouse_owner && !keyboard_owner && pointer_suppressed);
    CHECK(point_at(210,210,0) && !pointer_suppressed && !cb->edge_count);
    CHECK(point_at(210,210,1) && cb->edge_count == 1 && focus_window == wb);
    run = &first; CHECK(ML(a,1,0) == EAGAIN); /* Never steal a foreign held drag. */
    input_packet_mode = 8; input_sleeps = 0;
    CHECK(ML(a,1,1) == E_OK && input_sleeps == 1 && !pointer_buttons);
    CHECK(ML(a,1,1) == E_OK && mouse_depth == 2);
    run = &second;
    mouse_release_owner = &first; mouse_release_steps = 2; input_sleeps = 0;
    CHECK(ML(b,1,1) == E_OK && input_sleeps == 2 && mouse_owner == cb);
    mouse_depth = 0xffffffffUL;
    CHECK(ML(b,1,0) == ERANGE && mouse_depth == 0xffffffffUL); mouse_depth = 2;
    mouse_release_owner = &second; mouse_release_exit = TRUE;
    run = &first; input_sleeps = 0;
    CHECK(ML(a,1,1) == E_OK && input_sleeps == 1 && !*find_owner(&second));
    CHECK(ML(a,0,0) == E_OK && !cuis_mouse_active());
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW,own,0,0,0,0) == E_OK);
    CHECK(point_at(10,10,1) && pointer_capture == ca->windows);
    CHECK(ML(a,1,0) == E_OK); /* Acquiring one's own drag must not deadlock. */
    CHECK(ML(a,1,0) == E_OK);
    CHECK(cuis_display_lock(a,1,0) == E_OK && cuis_keyboard_lock(a,1) == E_OK);
    CHECK(cuis_keyboard_event(0x1e0061) && keyboard_owner == ca);
    before = frees;
    CHECK(cuis_service(CUIS_SESSION_END,a) == E_OK && frees > before && !mouse_owner && !keyboard_owner && pointer_suppressed);
    CHECK(point_at(900,700,0) && !pointer_suppressed); /* Consumed orphan release. */
    CHECK(allocations == frees && !input_bad_idle && input_irqs);
    return 0;
}
#undef ML
#undef MW
#undef MR
