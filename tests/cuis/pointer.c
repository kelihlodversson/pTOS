#define PREAD(h) cuis_pointer_service(h, 0x1000, 0x1004, 0x1008, 0x100c)
#define PW(fn, h) cuis_window_service(fn, h, ~0ULL, ~0ULL, ~0ULL, ~0ULL)
static BOOL point_at(LONG x, LONG y, ULONG buttons)
{
    return cuis_pointer_packet((WORD)(x - pointer_x), (WORD)(y - pointer_y), buttons, NULL, NULL);
}
static BOOL pointer_drain(LONG handle)
{
    ULONG i;
    for (i = 0; i <= CUIS_POINTER_CAPACITY + 1; i++) {
        LONG rc = PREAD(handle);
        if (!rc) return TRUE;
        if (rc < 0 && rc != ERANGE) return FALSE;
    }
    return FALSE;
}
static int pointer_checks(void)
{
    LONG a, b, x, y;
    ULONG i, before;
    SBYTE packet[3] = { (SBYTE)0xf8, 0, 0 };
    run = &first;
    CHECK(!window_total && !pointer_capture && !pointer_hover);
    CHECK(cuis_service(CUIS_SESSION_START, 0) >= 0);
    a = cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 100, 100, 0);
    CHECK(a >= 0 && PW(CUIS_WINDOW_SHOW, a) == E_OK);
    CHECK(point_at(10, 10, 0));
    CHECK(PREAD(a) == 1 && copied[0] == 10 && copied[1] == 10 && copied[3] == CUIS_POINTER_INSIDE);
    CHECK(point_at(11, 12, 0) && point_at(12, 13, 0));
    CHECK(PREAD(a) == 1 && copied[0] == 12 && copied[1] == 13 && PREAD(a) == 0);
    CHECK(cuis_pointer_service(a, 0, 0, 0, 0) == 0);
    CHECK(cuis_event_service(a, CUIS_EVENT_POINTER, 0) == 0);
    CHECK(cuis_event_service(a, 0, 0) == EINVAL);
    CHECK(cuis_event_service(a, 8, 0) == EINVAL);
    CHECK(cuis_event_service(a, 0x100000002ULL, 0) == EINVAL);
    CHECK(cuis_event_service(a, CUIS_EVENT_KEY, 0) == EACCDN);
    CHECK(point_at(12, 13, 1)); /* Trusted click focuses, even a non-front hit. */
    CHECK(PW(CUIS_FOCUS_QUERY, a) == 1 && pointer_capture != NULL);
    CHECK(cuis_pointer_service(a, 0x1000, 0x1004, 0x1008, 0x10000100cULL) == EIMBA);
    fail_copy = TRUE;
    CHECK(PREAD(a) == EIMBA);
    fail_copy = FALSE;
    CHECK(PREAD(a) == 1 && copied[2] == 1 && copied[3] == (CUIS_POINTER_INSIDE | 1));
    run = &second;
    CHECK(cuis_service(CUIS_SESSION_START, 0) >= 0);
    b = cuis_window_service(CUIS_WINDOW_CREATE, 20, 20, 20, 20, 0);
    CHECK(b > a && PW(CUIS_WINDOW_SHOW, b) == E_OK);
    run = &first; CHECK(pointer_drain(a)); run = &second;
    CHECK(PREAD(a) == EACCDN);
    CHECK(cuis_event_service(a, CUIS_EVENT_POINTER, 0) == EACCDN);
    CHECK(point_at(25, 25, 1)); /* Capture stays with a across the foreign window. */
    CHECK(PREAD(b) == 0);
    run = &first;
    CHECK(PREAD(a) == 1 && copied[0] == 25 && !(copied[3] & CUIS_POINTER_INSIDE));
    CHECK(point_at(25, 25, 0));
    CHECK(PREAD(a) == 1 && copied[2] == 0 && (copied[3] & 3) == 1);
    CHECK(pointer_drain(a));
    run = &second;
    CHECK(PREAD(b) == 1 && copied[3] == CUIS_POINTER_INSIDE);
    CHECK(point_at(25, 25, 1) && PW(CUIS_FOCUS_QUERY, b) == 1);
    CHECK(point_at(25, 25, 0));
    CHECK(pointer_drain(b));
    CHECK(point_at(40, 25, 0)); /* Half-open right edge belongs to a. */
    CHECK(PREAD(b) == 1 && !(copied[3] & CUIS_POINTER_INSIDE));
    run = &first;
    CHECK(PREAD(a) == 1 && (copied[3] & CUIS_POINTER_INSIDE));
    CHECK(point_at(40, 25, 1) && PW(CUIS_FOCUS_QUERY, a) == 1);
    CHECK(point_at(40, 25, 0));
    CHECK(pointer_drain(a));
    /* Preserve button edges; report overflow once, then a fresh state. */
    for (i = 0; i < CUIS_POINTER_CAPACITY + 1; i++)
        CHECK(point_at(40, 25, (i & 1) ? 0 : 1));
    CHECK(cuis_event_service(a, CUIS_EVENT_POINTER, 0) == 1);
    CHECK(PREAD(a) == ERANGE);
    CHECK(PREAD(a) == 1 && copied[2] == 1 && !(copied[3] & CUIS_POINTER_CHANGED));
    CHECK(point_at(40, 25, 0)); CHECK(pointer_drain(a));
    CHECK(PW(CUIS_WINDOW_REDRAW, a) >= 0);
    CHECK(cuis_event_service(a, CUIS_EVENT_KEY | CUIS_EVENT_POINTER | CUIS_EVENT_REDRAW, 0) == 0);
    CHECK(cuis_keyboard_event(123));
    CHECK(cuis_event_service(a, CUIS_EVENT_KEY | CUIS_EVENT_POINTER, 0) == 1);
    CHECK(cuis_input_service(CUIS_KEY_READ, a, 0x1000) == 1);
    CHECK(PW(CUIS_UPDATE_BEGIN, a) == E_OK);
    CHECK(cuis_event_service(a, CUIS_EVENT_POINTER, 10) == EACCDN);
    CHECK(PW(CUIS_UPDATE_END, a) == E_OK);
    CHECK(point_at(25, 25, 1)); /* b captures. Hide cancels without retargeting held input. */
    run = &second;
    CHECK(PW(CUIS_WINDOW_HIDE, b) == E_OK && !pointer_capture);
    CHECK(point_at(30, 30, 1) && pointer_suppressed);
    CHECK(point_at(30, 30, 0) && !pointer_suppressed);
    CHECK(PREAD(b) == 0);
    run = &first;
    CHECK(pointer_drain(a));
    CHECK(!point_at(200, 200, 1) && !cuis_focus_active());
    CHECK(!point_at(20, 20, 1)); /* A legacy drag cannot be stolen on entry. */
    CHECK(!point_at(20, 20, 0));
    CHECK(pointer_drain(a));
    before = legacy_pointer_calls;
    point_at(20, 20, 0);
    cuis_mouse_packet(packet);
    CHECK(legacy_pointer_calls == before); /* CUIS hover bypasses legacy callbacks. */
    CHECK(!point_at(200, 200, 0));
    cuis_mouse_packet(packet);
    CHECK(legacy_pointer_calls == before + 1);
    input_pointer_source = TRUE;
    input_pointer_x = input_pointer_y = 5;
    input_pointer_max_x = input_pointer_max_y = 9;
    CHECK(cuis_pointer_packet(100, 100, 0, &x, &y) && x == 9 && y == 9);
    CHECK(pointer_drain(a));
    input_pointer_x = input_pointer_y = 8;
    CHECK(cuis_event_service(a, CUIS_EVENT_POINTER, 0) == 1);
    CHECK(PREAD(a) == 1 && copied[0] == 8 && copied[1] == 8);
    input_pointer_source = FALSE;
    point_at(20, 20, 1);
    cuis_process_exit(&first);
    CHECK(!pointer_capture && pointer_suppressed);
    point_at(200, 200, 0);
    cuis_process_exit(&second);
    CHECK(!window_total && !pointer_capture && !pointer_hover && allocations == frees);
    return 0;
}
static int window_limit_checks(void)
{
    PD *owners[5] = { &first, &second, &third, &fourth, &fifth };
    LONG oldest = -1;
    ULONG i, j;
    for (i = 0; i < 5; i++) {
        run = owners[i];
        CHECK(cuis_service(CUIS_SESSION_START, 0) >= 0);
        if (i == 4) break;
        for (j = 0; j < CUIS_WINDOWS_PER_SESSION; j++) {
            LONG h = cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 1, 1, 0);
            CHECK(h >= 0);
            if (!i && !j) oldest = h;
        }
    }
    CHECK(window_total == CUIS_WINDOW_LIMIT && cuis_window_count() == CUIS_WINDOW_LIMIT);
    CHECK(cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 1, 1, 0) == ENSMEM);
    run = &first;
    CHECK(PW(CUIS_WINDOW_DESTROY, oldest) == E_OK);
    run = &fifth;
    CHECK(cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 1, 1, 0) >= 0);
    for (i = 0; i < 5; i++) cuis_process_exit(owners[i]);
    CHECK(!window_total && !cuis_window_count() && allocations == frees);
    return 0;
}
#undef PREAD
#undef PW
