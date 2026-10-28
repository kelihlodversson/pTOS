/* Generic top-strip ownership and input remain independent of GEM trees. */
static int surface_checks(void)
{
    LONG a, b, c, window;
    CUIS_RECT rect;
    CUIS_CLIENT *previous, *next;
    next_handle = next_window_handle = 0;
    run = &first; a = cuis_service(CUIS_SESSION_START,0);
    run = &second; b = cuis_service(CUIS_SESSION_START,0);
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,a,20) == EACCDN);
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,b,display_height) == EINVAL);
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,b,20) == E_OK);
    CHECK((cuis_surface_service(CUIS_SURFACE_STATUS,b,1) & (CUIS_SURFACE_OWNED|CUIS_SURFACE_DIRTY)) == 3);
    CHECK(cuis_surface_service(CUIS_SURFACE_STATUS,b,0) == CUIS_SURFACE_OWNED);
    CHECK(!focus_window && cuis_keyboard_event(0x1e0061));
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,b,20) == E_OK);
    CHECK(cuis_session_wait(b,0,CUIS_READY_KEY,0) == CUIS_READY_KEY);
    CHECK((*find_owner(run))->input_result[0] == 0x1e0061);
    CHECK(cuis_surface_service(CUIS_SURFACE_STATUS,b,3) == EINVAL);
    CHECK(cuis_surface_service(CUIS_CLOCK,b,1) == EINVAL);
    input_ticks = 0xffffffffUL;
    CHECK(cuis_surface_service(CUIS_CLOCK,b,0) == 0x7fffffffL);
    input_ticks = 0;
    run = &first;
    CHECK(cuis_display_lock(a,1,1) == E_OK);
    CHECK(cuis_vdi_clip(0,&rect) && rect.y == 20 && rect.height == display_height-20);
    run = &second;
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,b,21) == EAGAIN);
    run = &first;
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,a,19) == E_OK);
    CHECK(cuis_vdi_clip(0,&rect) && rect.y == 0 && rect.height == display_height);
    CHECK(cuis_display_lock(a,0,0) == E_OK);
    CHECK(cuis_session_wait(a,16,CUIS_READY_SURFACE,0) == CUIS_READY_SURFACE);
    CHECK(cuis_surface_service(CUIS_SURFACE_STATUS,a,1) & CUIS_SURFACE_DIRTY);
    window = cuis_window_service(CUIS_WINDOW_CREATE,0,0,100,100,0);
    CHECK(window >= 0 && cuis_window_service(CUIS_WINDOW_SHOW,window,0,0,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_UPDATE_BEGIN,window,0,0,0,0) == E_OK);
    CHECK(cuis_clip_service(window,0,0x1000,0x1004,0x1008,0x100c) == 1 && copied[1] == 19 && copied[3] == 81);
    CHECK(cuis_window_service(CUIS_UPDATE_END,window,0,0,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_FOCUS_REQUEST,window,0,0,0,0) == E_OK);
    run = &second;
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,b,20) == E_OK);
    run = &first;
    CHECK(cuis_window_service(CUIS_FOCUS_REQUEST,window,0,0,0,0) == E_OK);
    /* Re-requesting an already focused window retains the explicit current
     * surface; an actual focus transition hands it back. */
    CHECK(cuis_window_service(CUIS_FOCUS_RELEASE,window,0,0,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_FOCUS_REQUEST,window,0,0,0,0) == E_OK);
    CHECK(cuis_surface_service(CUIS_SURFACE_STATUS,a,0) & CUIS_SURFACE_OWNED);
    refresh_pointer_bounds();
    pointer_suppressed = pointer_legacy_drag = FALSE;
    CHECK(cuis_pointer_packet((WORD)(5-pointer_x),(WORD)(5-pointer_y),0,NULL,NULL));
    CHECK(cuis_surface_service(CUIS_SURFACE_STATUS,a,1) & CUIS_SURFACE_POINTER);
    CHECK(cuis_pointer_packet(0,0,1,NULL,NULL) && surface_capture == *find_owner(&first));
    CHECK(cuis_mouse_lock(a,1,1) == E_OK);
    CHECK(cuis_pointer_packet(0,0,0,NULL,NULL));
    CHECK(cuis_mouse_lock(a,0,0) == E_OK);
    cuis_process_exit(&first);
    run = &second;
    CHECK(cuis_surface_service(CUIS_SURFACE_STATUS,b,0) & CUIS_SURFACE_OWNED);
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,b,0) == E_OK && !surface_owner);
    cuis_process_exit(&second);
    CHECK(!surface_owner && !surface_capture && allocations == frees);
    run = &first; a = cuis_service(CUIS_SESSION_START,0);
    run = &second; b = cuis_service(CUIS_SESSION_START,0);
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,b,20) == E_OK);
    run = &first;
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,a,19) == E_OK);
    run = &third; c = cuis_service(CUIS_SESSION_START,0);
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,c,18) == E_OK);
    previous = surface_owner; next = surface_front->surface_next;
    run = &first;
    CHECK(cuis_surface_service(CUIS_SURFACE_BEGIN,a,25) == EACCDN);
    CHECK(cuis_display_lock(a,1,1) == E_OK);
    CHECK(cuis_surface_service(CUIS_SURFACE_BEGIN,a,25) == E_OK);
    CHECK(surface_owner == previous && surface_front == previous && surface_front->surface_next == next);
    CHECK((*find_owner(run))->surface_height == 19 && cuis_vdi_clip(0,&rect) && rect.y == 0);
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,a,26) == EACCDN);
    run = &second;
    CHECK(cuis_surface_service(CUIS_SURFACE_END,b,0) == EACCDN);
    run = &first;
    CHECK(cuis_surface_service(CUIS_SURFACE_END,a,0) == E_OK);
    CHECK(surface_owner == previous && surface_front == previous && previous->surface_next == next);
    CHECK(cuis_vdi_clip(0,&rect) && rect.y == 18);
    CHECK(cuis_surface_service(CUIS_SURFACE_BEGIN,a,25) == E_OK);
    CHECK(cuis_display_lock(a,0,0) == E_OK && !surface_painter);
    CHECK(surface_owner == previous && previous->surface_next == next);
    CHECK(cuis_display_lock(a,1,1) == E_OK);
    CHECK(cuis_surface_service(CUIS_SURFACE_BEGIN,a,25) == E_OK);
    run = &third;
    cuis_process_exit(&third);
    run = &first;
    CHECK(cuis_surface_service(CUIS_SURFACE_END,a,0) == E_OK);
    CHECK((*find_owner(run))->surface_height == 19 && !surface_painter);
    CHECK(cuis_display_lock(a,0,0) == E_OK);
    run = &first;
    CHECK(cuis_surface_service(CUIS_SURFACE_STATUS,a,0) & CUIS_SURFACE_OWNED);
    cuis_process_exit(&first);
    run = &second;
    CHECK(cuis_surface_service(CUIS_SURFACE_STATUS,b,0) & CUIS_SURFACE_OWNED);
    cuis_process_exit(&second);
    CHECK(!surface_front && !surface_owner && allocations == frees);
    return 0;
}

/* Deferred focus must run after capture cleanup and after exit-owned locks. */
static int surface_cleanup_checks(void)
{
    LONG a, b, c, wa, wb;
    int mode;
    for (mode = 0; mode < 2; mode++) {
        run = &first; a = cuis_service(CUIS_SESSION_START,0);
        wa = cuis_window_service(CUIS_WINDOW_CREATE,0,30,100,100,0);
        CHECK(wa >= 0 && cuis_window_service(CUIS_WINDOW_POLICY,wa,1,0,0,0) == E_OK);
        CHECK(cuis_window_service(CUIS_WINDOW_SHOW,wa,0,0,0,0) == E_OK);
        run = &second; b = cuis_service(CUIS_SESSION_START,0);
        wb = cuis_window_service(CUIS_WINDOW_CREATE,200,30,100,100,0);
        CHECK(wb >= 0 && cuis_window_service(CUIS_WINDOW_POLICY,wb,1,0,0,0) == E_OK);
        CHECK(cuis_window_service(CUIS_WINDOW_SHOW,wb,0,0,0,0) == E_OK);
        CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,b,19) == E_OK);
        run = &third; c = cuis_service(CUIS_SESSION_START,0);
        CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,c,19) == E_OK);
        run = &first;
        CHECK(cuis_window_service(CUIS_WINDOW_RAISE,wa,0,0,0,0) == E_OK);
        if (!mode) {
            CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,a,19) == E_OK);
            pointer_suppressed = pointer_legacy_drag = FALSE;
            CHECK(point_at(5,5,0) && point_at(5,5,1) && surface_capture);
            CHECK(cuis_mouse_lock(a,1,1) == E_OK);
            run = &second;
            CHECK(cuis_window_service(CUIS_WINDOW_RAISE,wb,0,0,0,0) == E_OK);
            CHECK(surface_focus_pending && surface_owner == *find_owner(&first));
            run = &first;
            CHECK(cuis_mouse_lock(a,0,0) == E_OK);
            CHECK(surface_owner == *find_owner(&second) && !surface_focus_pending);
            CHECK(point_at(5,5,0));
        } else {
            CHECK(cuis_display_lock(a,1,1) == E_OK);
            CHECK(cuis_window_service(CUIS_WINDOW_HIDE,wa,0,0,0,0) == E_OK);
            CHECK(focus_window && focus_window->client == *find_owner(&second));
            CHECK(surface_focus_pending && surface_owner == *find_owner(&third));
        }
        cuis_process_exit(&first);
        CHECK(!display_owner && !update_owner && surface_owner == *find_owner(&second));
        cuis_process_exit(&second); cuis_process_exit(&third);
        CHECK(!surface_owner && !surface_front && allocations == frees);
    }
    /* Enter the strip while a single click is accumulating toward a double.
     * The internal wake must retain the first press and its timeout. */
    run = &first; a = cuis_service(CUIS_SESSION_START,0);
    wa = cuis_window_service(CUIS_WINDOW_CREATE,0,30,100,100,0);
    CHECK(wa >= 0 && cuis_window_service(CUIS_WINDOW_SHOW,wa,0,0,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_FOCUS_REQUEST,wa,0,0,0,0) == E_OK);
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,a,19) == E_OK);
    CHECK(cuis_surface_service(CUIS_SURFACE_STATUS,a,1) & CUIS_SURFACE_DIRTY);
    pointer_suppressed = pointer_legacy_drag = FALSE;
    CHECK(point_at(40,60,0));
    CHECK(cuis_input_config(CUIS_INPUT_BUTTON,a,1,1,2,0,0) == E_OK);
    input_ticks = 100; input_sleeps = 0; input_packet_mode = 9;
    CHECK(cuis_session_wait(a,0,CUIS_READY_BUTTON|CUIS_READY_SURFACE,0) ==
          (CUIS_READY_BUTTON|CUIS_READY_SURFACE));
    CHECK(input_sleeps >= 91 && input_sleeps <= 93);
    CHECK((*find_owner(run))->input_result[1] == 1);
    CHECK((*find_owner(run))->input_result[2] == 40 && (*find_owner(run))->input_result[3] == 60);
    CHECK((*find_owner(run))->input_result[4] & 1);
    cuis_process_exit(&first);
    CHECK(allocations == frees);
    return 0;
}
