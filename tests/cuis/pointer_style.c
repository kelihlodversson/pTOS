/* Copied pointer styles follow authoritative focus/grab/lifetime state. */
static int pointer_style_checks(void)
{
    LONG a,b,wa,wb;
    MFORM *form = (MFORM *)stream_input;
    CHECK(sizeof(MFORM) == 74 && __builtin_offsetof(MFORM,mf_bg) == 6 && __builtin_offsetof(MFORM,mf_fg) == 8);
    run = &first; a = cuis_service(CUIS_SESSION_START,0);
    CHECK(!(*find_owner(run))->pointer_style_set && !(*find_owner(run))->pointer_hidden);
    CHECK(a >= 0 && cuis_pointer_style(a,0,0) == EIMBA);
    CHECK(cuis_pointer_style(a,0x10000,32768) == EINVAL);
    CHECK(cuis_pointer_style(1ULL << 32,0x10000,0) == EINVAL);
    memset(form,0,sizeof(*form)); form->mf_fg = 1; form->mf_mask[0] = 0xffff;
    CHECK(cuis_pointer_style(a,0x10000,2) == E_OK && displayed_style && displayed_hidden && displayed_pointer.mf_fg == 1);
    form->mf_fg = 99; cuis_pointer_sync();
    CHECK(displayed_pointer.mf_fg == 1); /* The kernel retained a copy. */
    fail_copy = TRUE;
    CHECK(cuis_pointer_style(a,0x10000,0) == EIMBA && displayed_hidden);
    fail_copy = FALSE;
    wa = cuis_window_service(CUIS_WINDOW_CREATE,20,30,100,80,0);
    CHECK(wa >= 0 && cuis_window_service(CUIS_WINDOW_SHOW,wa,0,0,0,0) == E_OK);
    run = &second; b = cuis_service(CUIS_SESSION_START,0);
    CHECK(cuis_pointer_style(a,0x10000,0) == EACCDN);
    form->mf_fg = 2;
    CHECK(cuis_pointer_style(b,0x10000,0) == E_OK && displayed_pointer.mf_fg == 1 && displayed_hidden);
    wb = cuis_window_service(CUIS_WINDOW_CREATE,140,30,100,80,0);
    CHECK(wb >= 0 && cuis_window_service(CUIS_WINDOW_SHOW,wb,0,0,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_FOCUS_REQUEST,wb,0,0,0,0) == E_OK);
    CHECK(displayed_pointer.mf_fg == 2 && !displayed_hidden);
    run = &first;
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,a,40) == E_OK);
    pointer_suppressed = pointer_legacy_drag = FALSE;
    CHECK(point_at(200,5,0) && displayed_pointer.mf_fg == 1 && displayed_hidden);
    CHECK(point_at(200,60,0) && displayed_pointer.mf_fg == 2 && !displayed_hidden);
    CHECK(point_at(200,60,1) && point_at(200,5,1) && displayed_pointer.mf_fg == 2 && !displayed_hidden);
    CHECK(point_at(200,5,0) && displayed_pointer.mf_fg == 1 && displayed_hidden);
    CHECK(point_at(200,5,1) && point_at(200,60,1) && displayed_pointer.mf_fg == 1 && displayed_hidden);
    CHECK(point_at(200,60,0) && displayed_pointer.mf_fg == 2 && !displayed_hidden);
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,a,0) == E_OK);
    CHECK(cuis_mouse_lock(a,1,0) == E_OK && displayed_pointer.mf_fg == 1 && displayed_hidden);
    CHECK(cuis_mouse_lock(a,0,0) == E_OK && displayed_pointer.mf_fg == 2 && !displayed_hidden);
    CHECK(cuis_mouse_lock(a,CUIS_MOUSE_FRESH,0) == E_OK && displayed_pointer.mf_fg == 1);
    CHECK(cuis_mouse_lock(a,0,0) == E_OK && (*find_owner(run))->pointer_style_set);
    CHECK(displayed_pointer.mf_fg == 2 && !displayed_hidden);
    cuis_process_exit(&second);
    CHECK(displayed_pointer.mf_fg == 1 && displayed_hidden);
    cuis_process_exit(&first);
    CHECK(!displayed_style && !displayed_hidden && !pointer_fallback && allocations == frees);
    return 0;
}
