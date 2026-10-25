/* Focus ownership is distinct from stacking and never exposes foreign IDs. */
#define F(fn, h) cuis_window_service(fn, h, ~0ULL, ~0ULL, ~0ULL, ~0ULL)
static int focus_checks(void)
{
    LONG a, b, c, session;
    ULONG fn;
    run = &first;
    CHECK(!cuis_focus_active());
    CHECK(F(CUIS_FOCUS_REQUEST, 0) == EACCDN);
    session = cuis_service(CUIS_SESSION_START, 0);
    a = cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 10, 10, 0);
    CHECK(a >= 0 && session >= 0);
    CHECK(F(CUIS_FOCUS_QUERY, a) == 0);
    CHECK(F(CUIS_FOCUS_REQUEST, a) == EINVAL);
    CHECK(F(CUIS_WINDOW_SHOW, a) == E_OK);
    CHECK(F(CUIS_WINDOW_REDRAW, a) == 1);
    CHECK(F(CUIS_FOCUS_REQUEST, a) == E_OK && F(CUIS_FOCUS_QUERY, a) == 1);
    CHECK(F(CUIS_WINDOW_REDRAW, a) == 1);
    CHECK(F(CUIS_FOCUS_REQUEST, a) == E_OK && F(CUIS_WINDOW_REDRAW, a) == 0);
    for (fn = CUIS_FOCUS_REQUEST; fn <= CUIS_FOCUS_RELEASE; fn++) {
        CHECK(F(fn, 0x100000000ULL | (ULONG)a) == EINVAL);
        CHECK(F(fn, ~0ULL) == EINVAL);
    }
    CHECK(F(CUIS_UPDATE_BEGIN, a) == E_OK);
    CHECK(F(CUIS_FOCUS_QUERY, a) == 1);
    CHECK(F(CUIS_FOCUS_RELEASE, a) == EAGAIN);
    CHECK(F(CUIS_UPDATE_END, a) == E_OK);
    run = &second;
    CHECK(cuis_service(CUIS_SESSION_START, 0) >= 0);
    for (fn = CUIS_FOCUS_REQUEST; fn <= CUIS_FOCUS_RELEASE; fn++)
        CHECK(F(fn, a) == EACCDN);
    b = cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 10, 10, 0);
    CHECK(b > a && F(CUIS_WINDOW_SHOW, b) == E_OK);
    CHECK(F(CUIS_FOCUS_RELEASE, b) == E_OK);
    run = &first;
    CHECK(F(CUIS_FOCUS_QUERY, a) == 1); /* Showing a window does not steal focus. */
    CHECK(F(CUIS_FOCUS_REQUEST, a) == EACCDN);
    run = &second;
    CHECK(F(CUIS_FOCUS_REQUEST, b) == E_OK && F(CUIS_FOCUS_QUERY, b) == 1);
    CHECK(F(CUIS_WINDOW_LOWER, b) == E_OK && F(CUIS_FOCUS_QUERY, b) == 1);
    CHECK(F(CUIS_FOCUS_REQUEST, b) == EACCDN);
    CHECK(F(CUIS_WINDOW_RAISE, b) == E_OK && F(CUIS_FOCUS_QUERY, b) == 1);
    CHECK(F(CUIS_WINDOW_REDRAW, b) == 1);
    CHECK(F(CUIS_FOCUS_RELEASE, b) == E_OK && F(CUIS_FOCUS_QUERY, b) == 0);
    CHECK(F(CUIS_WINDOW_REDRAW, b) == 1);
    CHECK(F(CUIS_FOCUS_RELEASE, b) == E_OK && F(CUIS_WINDOW_REDRAW, b) == 0);
    CHECK(F(CUIS_FOCUS_REQUEST, b) == E_OK);
    CHECK(F(CUIS_WINDOW_HIDE, b) == E_OK && F(CUIS_FOCUS_QUERY, b) == 0);
    run = &first;
    CHECK(F(CUIS_FOCUS_QUERY, a) == 1); /* Focused hide falls back across owners. */
    c = cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 10, 10, 0);
    CHECK(c > b && F(CUIS_WINDOW_SHOW, c) == E_OK && F(CUIS_FOCUS_REQUEST, c) == E_OK);
    CHECK(F(CUIS_WINDOW_DESTROY, c) == E_OK && F(CUIS_FOCUS_QUERY, a) == 1);
    CHECK(F(CUIS_FOCUS_QUERY, c) == EACCDN);
    run = &second;
    CHECK(F(CUIS_WINDOW_SHOW, b) == E_OK && F(CUIS_FOCUS_REQUEST, b) == E_OK);
    cuis_process_exit(&second);
    run = &first;
    CHECK(F(CUIS_FOCUS_QUERY, a) == 1);
    CHECK(cuis_service(CUIS_SESSION_END, session) == E_OK);
    CHECK(!cuis_focus_active() && !window_front && allocations == frees);
    CHECK(cuis_window_service(CUIS_WINDOW_CLIP, 0, 0, 0, 0, 0) == EINVFN);
    return 0;
}
#undef F
