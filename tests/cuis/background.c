/* Background ownership, clipping, capture and per-client reset use real CUIS. */
#define BGREG(h,i) cuis_background_region(h,i,0x1000,0x1004,0x1008,0x100c)
static int background_checks(void)
{
    LONG a, b, window, result, area;
    ULONG i, before;
    CUIS_CLIENT *ca, *cb;
    next_handle = next_window_handle = 0;
    run = &first;
    CHECK(cuis_display_info(0) == EACCDN && cuis_windows_reset(0) == EACCDN);
    a = cuis_service(CUIS_SESSION_START,0); ca = *find_owner(run);
    CHECK(a >= 0 && cuis_display_info(0) == display_width && cuis_display_info(1) == display_height);
    CHECK(cuis_display_info(4) == EINVAL && cuis_display_info(1ULL<<32) == EINVAL);
    CHECK(cuis_background(1ULL<<32,1) == EINVAL && cuis_background(a,5) == EINVAL);
    CHECK(cuis_background(a,1) == EACCDN && cuis_background(a,2) == 0);
    CHECK(cuis_display_lock(a,1,0) == E_OK && cuis_background(a,1) == E_OK);
    CHECK(background_front == ca && cuis_background(a,2) == 3 && cuis_display_info(3) == 1);
    CHECK(cuis_background(a,3) == E_OK && cuis_background(a,2) == 1);
    CHECK(BGREG(a,0) == 1 && copied[0] == 0 && copied[1] == 0);
    CHECK(copied[2] == display_width && copied[3] == display_height && BGREG(a,1) == 0);
    CHECK(cuis_setting(0,0,0,0,0) == EFILNF);
    CHECK(cuis_setting(0,0xffff,37,0x1122,3) == E_OK);
    CHECK(cuis_setting(0,0,0,0,0) == 0xffff && cuis_setting(37,0,0,0,0) == 0x1122);
    CHECK(cuis_setting(0,0,64,1,3) == EINVAL && cuis_setting(0,0,0,0,0) == 0xffff);
    CHECK(cuis_setting(64,0,0,0,0) == EINVAL && cuis_setting(0,65536,0,0,1) == EINVAL);
    CHECK(cuis_display_lock(a,0,0) == E_OK);
    run = &second; b = cuis_service(CUIS_SESSION_START,0); cb = *find_owner(run);
    CHECK(b >= 0 && cuis_background(a,2) == EACCDN);
    CHECK(cuis_setting(0,1,0,0,1) == EACCDN && cuis_setting(37,0,0,0,0) == 0x1122);
    CHECK(cuis_display_lock(b,1,0) == E_OK);
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,b,20) == E_OK);
    CHECK(cuis_background(b,1) == E_OK && background_front == cb && cb->background_next == ca);
    CHECK(cuis_background(b,3) == E_OK);
    window = cuis_window_service(CUIS_WINDOW_CREATE,10,30,100,80,0);
    CHECK(window >= 0 && cuis_window_service(CUIS_WINDOW_OUTSET,window,2,2,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW,window,0,0,0,0) == E_OK && cb->background_dirty);
    area = 0;
    for (i = 0; ; i++) {
        result = BGREG(b,i); CHECK(result >= 0);
        if (!result) break;
        CHECK(copied[1] >= 20 && (copied[0] >= 112 || copied[0]+copied[2] <= 10 ||
              copied[1] >= 112 || copied[1]+copied[3] <= 30));
        area += copied[2]*copied[3];
    }
    CHECK(area == (LONG)display_width*(display_height-20)-102*82);
    before = allocations-frees;
    fail_allocation = TRUE;
    CHECK(BGREG(b,0) == ENSMEM && allocations-frees == before);
    fail_allocation = FALSE;
    fail_copy = TRUE;
    CHECK(BGREG(b,0) == EIMBA);
    fail_copy = FALSE;
    CHECK(BGREG(b,0) == 1 && cb->background->display_regions_valid);
    CHECK(cuis_display_lock(b,0,0) == E_OK);
    /* A background press owns a drag even after entering a foreign window. */
    pointer_initialized = pointer_seen = TRUE; pointer_suppressed = pointer_legacy_drag = FALSE;
    pointer_activation_drag = pointer_control_drag = FALSE; pointer_capture = NULL;
    pointer_x = 200; pointer_y = 200; pointer_buttons = 0;
    CHECK(cuis_pointer_packet(0,0,1,NULL,NULL) && background_capture == cb);
    CHECK(cb->edge_count && !ca->edge_count);
    CHECK(cuis_display_lock(b,1,0) == E_OK && cuis_background(b,1) == E_OK);
    CHECK(background_capture == cb && !pointer_suppressed);
    CHECK(cuis_display_lock(b,0,0) == E_OK);
    run = &first; CHECK(cuis_display_lock(a,1,0) == E_OK);
    CHECK(cuis_background(a,1) == EAGAIN);
    CHECK(cuis_display_lock(a,0,0) == E_OK);
    CHECK(cuis_pointer_packet(-180,-160,1,NULL,NULL) && background_capture == cb);
    CHECK(cuis_pointer_packet(0,0,0,NULL,NULL) && !background_capture);
    run = &second;
    CHECK(cuis_display_lock(b,1,0) == E_OK && cuis_background(b,0) == E_OK);
    CHECK(background_front == ca && ca->background_dirty && cuis_background(b,2) == 0);
    CHECK(cuis_display_lock(b,0,0) == E_OK);
    run = &first;
    CHECK(cuis_keyboard_event(0x1234) && ca->key_count == 1);
    CHECK(cuis_mouse_lock(a,1,0) == E_OK && cuis_display_lock(a,1,0) == E_OK);
    CHECK(cuis_display_lock(a,1,0) == E_OK);
    CHECK(cuis_surface_service(CUIS_SURFACE_BEGIN,a,25) == E_OK && surface_painter == ca);
    ca->pointer_hidden = 2;
    before = cuis_window_count();
    CHECK(cuis_windows_reset(a) == E_OK && !mouse_owner && !display_owner && !ca->pointer_hidden);
    CHECK(!surface_painter && !surface_paint_height);
    CHECK(cuis_window_count() == before && cuis_service(CUIS_SESSION_QUERY,a) == E_OK);
    CHECK(background_front == ca); /* wind_new retains desktop registration. */
    CHECK(cuis_display_lock(a,1,0) == E_OK);
    CHECK(cuis_surface_service(CUIS_SURFACE_BEGIN,a,25) == E_OK);
    CHECK(cuis_surface_service(CUIS_SURFACE_END,a,0) == E_OK && cuis_display_lock(a,0,0) == E_OK);
    run = &second;
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,b,20) == E_OK);
    CHECK(cuis_display_lock(b,1,0) == E_OK);
    CHECK(cuis_surface_service(CUIS_SURFACE_BEGIN,b,22) == E_OK);
    CHECK(cuis_surface_service(CUIS_SURFACE_END,b,0) == E_OK && cuis_display_lock(b,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_UPDATE_BEGIN,window,0,0,0,0) == E_OK);
    CHECK(cuis_windows_reset(b) == E_OK && !update_owner && !cb->windows && !cb->window_count);
    CHECK(cuis_display_lock(b,1,0) == E_OK);
    region_exit_on_output = TRUE;
    CHECK(BGREG(b,0) == EIMBA && !*find_owner(run));
    run = &first;
    CHECK(BGREG(a,0) == 1);
    cuis_process_exit(&first);
    CHECK(!background_front && !background_capture && !cuis_window_count() && allocations == frees);
    ui_settings_set = 0;
    return 0;
}
#undef BGREG

/* A foreign border cannot steal a captured desktop drag's release. */
static int background_release_checks(void)
{
    LONG a, b, window;
    CUIS_CLIENT *desktop, *peer;
    run = &first; a = cuis_service(CUIS_SESSION_START,0); desktop = *find_owner(run);
    CHECK(a >= 0 && cuis_display_lock(a,1,0) == E_OK);
    CHECK(cuis_background(a,1) == E_OK && cuis_display_lock(a,0,0) == E_OK);
    run = &second; b = cuis_service(CUIS_SESSION_START,0); peer = *find_owner(run);
    window = cuis_window_service(CUIS_WINDOW_CREATE,200,200,100,100,0);
    CHECK(b >= 0 && window >= 0);
    CHECK(cuis_window_service(CUIS_WINDOW_CONTENT,window,20,20,1,1) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_POLICY,window,1,0,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW,window,0,0,0,0) == E_OK);
    pointer_suppressed = pointer_legacy_drag = FALSE;
    pointer_buttons = 0;
    CHECK(point_at(50,50,1) && background_capture == desktop);
    CHECK(point_at(210,210,1) && background_capture == desktop && !pointer_capture);
    CHECK(cuis_window_service(CUIS_FOCUS_REQUEST,window,0,0,0,0) == E_OK);
    CHECK(focus_window == peer->windows && !content_contains(focus_window));
    run = &first;
    CHECK(cuis_input_config(CUIS_INPUT_BUTTON,a,1,0,1,0,0) == E_OK);
    input_sleeps = 0; input_packet_mode = 8;
    CHECK(cuis_session_wait(a,0,CUIS_READY_BUTTON|CUIS_READY_TIMER,10) == CUIS_READY_BUTTON);
    CHECK(input_sleeps == 1 && !input_packet_mode && !background_capture && !pointer_buttons);
    CHECK(cuis_input_config(CUIS_INPUT_RESULT,a,0x1000,0x1004,0x1008,0x100c,0x1010) == E_OK);
    CHECK(copied[1] == 1 && copied[2] == 210 && copied[3] == 210 && !(copied[4] & 3));
    CHECK(!desktop->edge_count && !peer->edge_count);
    /* Ordinary uncaptured border hover still suppresses live button waits. */
    run = &second;
    CHECK(cuis_input_config(CUIS_INPUT_BUTTON,b,1,0,1,0,0) == E_OK);
    CHECK(cuis_session_wait(b,0,CUIS_READY_BUTTON|CUIS_READY_TIMER,0) == CUIS_READY_TIMER);
    cuis_process_exit(&first); cuis_process_exit(&second);
    CHECK(allocations == frees && input_irqs && !input_bad_idle);
    return 0;
}
