/* Point lookup is global metadata, independent of focus, grabs and shadows. */
#define AT(x,y) cuis_window_at((UQUAD)(QUAD)(x),(UQUAD)(QUAD)(y))
static int window_at_checks(void)
{
    LONG a, b, ia, ib, session;
    ULONG before;
    next_handle = next_window_handle = 0;
    run = &first;
    CHECK(AT(0,0) == EACCDN && input_irqs);
    session = cuis_service(CUIS_SESSION_START,0);
    CHECK(session >= 0 && AT(0,0) == 0);
    a = cuis_window_service(CUIS_WINDOW_CREATE,(UQUAD)(QUAD)-20,(UQUAD)(QUAD)-10,100,80,0);
    CHECK(a >= 0);
    ia = cuis_window_service(CUIS_WINDOW_IDENTIFY,a,0,0,0,0);
    CHECK(AT(0,0) == 0); /* Hidden metadata is never hit. */
    CHECK(cuis_window_service(CUIS_WINDOW_OUTSET,a,2,2,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW,a,0,0,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_FOCUS_REQUEST,a,0,0,0,0) == E_OK);
    CHECK(AT(-20,-10) == ia && AT(79,69) == ia);
    CHECK(AT(-21,0) == 0 && AT(0,-11) == 0 && AT(80,0) == 0 && AT(0,70) == 0);
    CHECK(AT(80,70) == 0); /* The visual shadow has no input bounds. */
    CHECK(AT(-2147483647L-1,2147483647L) == 0);
    CHECK(cuis_window_at(0x100000000ULL,0) == EINVAL);
    CHECK(cuis_window_at(0,0x100000000ULL) == EINVAL);
    CHECK(cuis_window_at(0xffffffffULL,0) == EINVAL);
    run = &second;
    CHECK(AT(0,0) == EACCDN);
    CHECK(cuis_service(CUIS_SESSION_START,0) >= 0 && AT(0,0) == ia);
    b = cuis_window_service(CUIS_WINDOW_CREATE,10,20,100,80,0);
    CHECK(b >= 0);
    ib = cuis_window_service(CUIS_WINDOW_IDENTIFY,b,0,0,0,0);
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW,b,0,0,0,0) == E_OK);
    CHECK(AT(10,20) == ib && AT(9,20) == ia && AT(79,69) == ib);
    before = allocations;
    run = &first;
    CHECK(cuis_display_lock(session,1,0) == E_OK);
    run = &second;
    CHECK(AT(10,20) == ib && AT(109,99) == ib && AT(110,99) == 0);
    CHECK(focus_window && focus_window->identifier == ia && window_front->identifier == ib);
    CHECK(allocations == before && input_irqs);
    run = &first;
    CHECK(cuis_display_lock(session,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_RAISE,a,0,0,0,0) == E_OK && AT(10,20) == ia);
    CHECK(cuis_window_service(CUIS_WINDOW_LOWER,a,0,0,0,0) == E_OK && AT(10,20) == ib);
    run = &second;
    CHECK(cuis_window_service(CUIS_WINDOW_HIDE,b,0,0,0,0) == E_OK && AT(10,20) == ia);
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW,b,0,0,0,0) == E_OK && AT(10,20) == ib);
    CHECK(cuis_window_service(CUIS_WINDOW_DESTROY,b,0,0,0,0) == E_OK && AT(10,20) == ia);
    cuis_process_exit(&first);
    CHECK(AT(10,20) == 0);
    /* Native coordinates retain their LONG range, beyond GEM's WORD API. */
    b = cuis_window_service(CUIS_WINDOW_CREATE,2147483646L,(UQUAD)(QUAD)(-2147483647L-1),1,1,0);
    CHECK(b >= 0 && cuis_window_service(CUIS_WINDOW_SHOW,b,0,0,0,0) == E_OK);
    CHECK(AT(2147483646L,-2147483647L-1) > 0);
    CHECK(AT(2147483647L,-2147483647L-1) == 0 && AT(2147483646L,-2147483647L) == 0);
    cuis_process_exit(&second);
    CHECK(AT(0,0) == EACCDN && input_irqs && allocations == frees);
    return 0;
}
#undef AT
