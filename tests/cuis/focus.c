/* Production managed activation, fallback and foreign-window isolation. */
#define F(fn,h) cuis_window_service(fn,h,0,0,0,0)
static int focus_checks(void)
{
    LONG a, b, c, session;
    run=&first;
    CHECK(!cuis_focus_active() && F(CUIS_FOCUS_QUERY,0) == EACCDN);
    session=cuis_service(CUIS_SESSION_START,0);
    a=cuis_window_service(CUIS_WINDOW_CREATE,0,0,10,10,0);
    CHECK(a >= 0 && session >= 0 && F(CUIS_FOCUS_QUERY,a) == 0);
    CHECK(F(CUIS_WINDOW_RAISE,a) == EINVAL);
    CHECK(cuis_window_service(CUIS_WINDOW_POLICY,a,1,0,0,0) == E_OK);
    CHECK(F(CUIS_WINDOW_SHOW,a) == E_OK && F(CUIS_FOCUS_QUERY,a) == 1);
    input_window(a)->redraw=FALSE;
    CHECK(F(CUIS_WINDOW_RAISE,a) == E_OK && !input_window(a)->redraw);
    CHECK(F(CUIS_FOCUS_QUERY,0x100000000ULL|(ULONG)a) == EINVAL);
    CHECK(F(CUIS_FOCUS_QUERY,~0ULL) == EINVAL);
    run=&second;
    CHECK(cuis_service(CUIS_SESSION_START,0) >= 0);
    CHECK(F(CUIS_FOCUS_QUERY,a) == EACCDN && F(CUIS_WINDOW_RAISE,a) == EACCDN);
    b=cuis_window_service(CUIS_WINDOW_CREATE,0,0,10,10,0);
    CHECK(b > a && F(CUIS_WINDOW_SHOW,b) == E_OK);
    CHECK(F(CUIS_FOCUS_QUERY,b) == 0); /* Unmanaged show does not claim focus. */
    run=&first; CHECK(F(CUIS_FOCUS_QUERY,a) == 1);
    run=&second;
    CHECK(cuis_window_service(CUIS_WINDOW_POLICY,b,1,0,0,0) == E_OK);
    CHECK(F(CUIS_WINDOW_RAISE,b) == E_OK && F(CUIS_FOCUS_QUERY,b) == 1);
    CHECK(F(CUIS_WINDOW_LOWER,b) == E_OK && F(CUIS_FOCUS_QUERY,b) == 0);
    run=&first; CHECK(F(CUIS_FOCUS_QUERY,a) == 1);
    run=&second;
    CHECK(F(CUIS_WINDOW_RAISE,b) == E_OK && F(CUIS_FOCUS_QUERY,b) == 1);
    CHECK(F(CUIS_WINDOW_HIDE,b) == E_OK && F(CUIS_FOCUS_QUERY,b) == 0);
    run=&first; CHECK(F(CUIS_FOCUS_QUERY,a) == 1);
    c=cuis_window_service(CUIS_WINDOW_CREATE,0,0,10,10,0);
    CHECK(c > b && cuis_window_service(CUIS_WINDOW_POLICY,c,1,0,0,0) == E_OK);
    CHECK(F(CUIS_WINDOW_SHOW,c) == E_OK && F(CUIS_FOCUS_QUERY,c) == 1);
    CHECK(F(CUIS_WINDOW_DESTROY,c) == E_OK && F(CUIS_FOCUS_QUERY,a) == 1);
    CHECK(F(CUIS_FOCUS_QUERY,c) == EACCDN);
    run=&second;
    CHECK(F(CUIS_WINDOW_SHOW,b) == E_OK && F(CUIS_FOCUS_QUERY,b) == 1);
    cuis_process_exit(&second);
    run=&first; CHECK(F(CUIS_FOCUS_QUERY,a) == 1);
    CHECK(cuis_service(CUIS_SESSION_END,session) == E_OK);
    CHECK(!cuis_focus_active() && !window_front && allocations == frees);
    return 0;
}
#undef F
