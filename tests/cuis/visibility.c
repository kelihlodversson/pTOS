/* Global stacking spans owners; redraw polling must remain owner-local. */
#define WINDOW_CALL(fn, handle) cuis_window_service(fn, handle, ~0ULL, ~0ULL, ~0ULL, ~0ULL)
static int visibility_checks(void)
{
    LONG a, b, c, session;
    CUIS_WINDOW *wa, *wb, *wc;
    ULONG fn;

    next_handle = next_window_handle = 0;
    run = &first;
    session = cuis_service(CUIS_SESSION_START, 0);
    CHECK(session >= 0);
    a = cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 100, 100, 0);
    wa = (*find_owner(run))->windows;
    b = cuis_window_service(CUIS_WINDOW_CREATE, 10, 10, 50, 50, 0);
    wb = (*find_owner(run))->windows;
    CHECK(a >= 0 && b > a && !window_front);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, a) == 0);
    CHECK(WINDOW_CALL(CUIS_WINDOW_RAISE, a) == EINVAL);
    CHECK(WINDOW_CALL(CUIS_WINDOW_LOWER, a) == EINVAL);
    CHECK(WINDOW_CALL(CUIS_WINDOW_SHOW, a) == E_OK);
    CHECK(window_front == wa && !wa->znext && wa->visible);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, a) == 1);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, a) == 0);
    CHECK(WINDOW_CALL(CUIS_WINDOW_SHOW, a) == E_OK);
    CHECK(WINDOW_CALL(CUIS_WINDOW_RAISE, a) == E_OK);
    CHECK(WINDOW_CALL(CUIS_WINDOW_LOWER, a) == E_OK);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, a) == 0);
    CHECK(WINDOW_CALL(CUIS_WINDOW_SHOW, b) == E_OK);
    CHECK(window_front == wb && wb->znext == wa);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, a) == 1);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, b) == 1);
    run = &second;
    CHECK(cuis_service(CUIS_SESSION_START, 0) >= 0);
    for (fn = CUIS_WINDOW_SHOW; fn <= CUIS_WINDOW_REDRAW; fn++) {
        CHECK(WINDOW_CALL(fn, a) == EACCDN);
        CHECK(WINDOW_CALL(fn, 0x100000000ULL) == EINVAL);
        CHECK(WINDOW_CALL(fn, ~0ULL) == EINVAL);
    }
    c = cuis_window_service(CUIS_WINDOW_CREATE, 20, 20, 30, 30, 0);
    wc = (*find_owner(run))->windows;
    CHECK(c > b && WINDOW_CALL(CUIS_WINDOW_SHOW, c) == E_OK);
    CHECK(window_front == wc && wc->znext == wb && wb->znext == wa && !wa->znext);
    CHECK(cuis_visible_window_count() == 3);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, a) == EACCDN);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, c) == 1);
    run = &first;
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, a) == 1);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, b) == 1);
    CHECK(WINDOW_CALL(CUIS_WINDOW_RAISE, a) == E_OK);
    CHECK(window_front == wa && wa->znext == wc && wc->znext == wb && !wb->znext);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, a) == 1);
    CHECK(WINDOW_CALL(CUIS_WINDOW_LOWER, a) == E_OK);
    CHECK(window_front == wc && wc->znext == wb && wb->znext == wa && !wa->znext);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, a) == 1);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, b) == 1);
    run = &second;
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, c) == 1);
    run = &first;
    CHECK(cuis_window_service(CUIS_WINDOW_CONFIGURE, b, 10, 10, 50, 50) == E_OK);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, a) == 0);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, b) == 0);
    run = &second;
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, c) == 0);
    run = &first;
    CHECK(cuis_window_service(CUIS_WINDOW_CONFIGURE, b, 0, 0, 0, 1) == EINVAL);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, b) == 0);
    CHECK(cuis_window_service(CUIS_WINDOW_CONFIGURE, b, -20LL, 30, 40, 50) == E_OK);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, a) == 1);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, b) == 1);
    CHECK(cuis_window_service(CUIS_WINDOW_CONFIGURE, b, -20LL, 30, 40, 50) == E_OK);
    run = &second;
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, c) == 1);
    run = &first;
    CHECK(WINDOW_CALL(CUIS_WINDOW_HIDE, b) == E_OK);
    CHECK(!wb->visible && !wb->redraw && !wb->znext && wc->znext == wa);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, b) == 0);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, a) == 1);
    CHECK(WINDOW_CALL(CUIS_WINDOW_HIDE, b) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_CONFIGURE, b, 0, 0, 1, 1) == E_OK);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, a) == 0);
    CHECK(WINDOW_CALL(CUIS_WINDOW_SHOW, b) == E_OK);
    CHECK(window_front == wb && wb->znext == wc && wc->znext == wa);
    /* Closing one owner removes non-adjacent entries and damages survivors. */
    run = &second;
    CHECK(window_front == wb && wb->znext == wc && wc->znext == wa);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, c) == 1);
    run = &first;
    CHECK(cuis_service(CUIS_SESSION_END, session) == E_OK);
    CHECK(window_front == wc && !wc->znext && cuis_visible_window_count() == 1);
    run = &second;
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, c) == 1);
    CHECK(WINDOW_CALL(CUIS_WINDOW_REDRAW, c) == 0);
    CHECK(WINDOW_CALL(CUIS_WINDOW_HIDE, c) == E_OK && !window_front);
    CHECK(WINDOW_CALL(CUIS_WINDOW_SHOW, c) == E_OK && window_front == wc);
    CHECK(WINDOW_CALL(CUIS_WINDOW_DESTROY, c) == E_OK && !window_front);
    CHECK(WINDOW_CALL(CUIS_WINDOW_SHOW, c) == EACCDN);
    cuis_process_exit(&second);
    CHECK(!cuis_window_count() && !cuis_visible_window_count());
    CHECK(allocations == frees);
    return 0;
}
#undef WINDOW_CALL
