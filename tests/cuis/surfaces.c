/* Generic top-strip ownership and input remain independent of GEM trees. */
static int surface_checks(void)
{
    LONG a, b, window;
    CUIS_RECT rect;
    next_handle = next_window_handle = 0;
    run = &first; a = cuis_service(CUIS_SESSION_START,0);
    run = &second; b = cuis_service(CUIS_SESSION_START,0);
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,a,20) == EACCDN);
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,b,display_height) == EINVAL);
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,b,20) == E_OK);
    CHECK((cuis_surface_service(CUIS_SURFACE_STATUS,b,1) & (CUIS_SURFACE_OWNED|CUIS_SURFACE_DIRTY)) == 3);
    CHECK(cuis_surface_service(CUIS_SURFACE_STATUS,b,0) == CUIS_SURFACE_OWNED);
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
    window = cuis_window_service(CUIS_WINDOW_CREATE,20,40,100,100,0);
    CHECK(window >= 0 && cuis_window_service(CUIS_WINDOW_SHOW,window,0,0,0,0) == E_OK);
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
    return 0;
}
