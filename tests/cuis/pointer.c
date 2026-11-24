#define PW(fn,h) cuis_window_service(fn,h,0,0,0,0)
static BOOL point_at(LONG x, LONG y, ULONG buttons)
{
    return cuis_pointer_packet((WORD)(x-pointer_x),(WORD)(y-pointer_y),buttons,NULL,NULL);
}
/* Explicit fixture setup; production clients consume session input history. */
static void clear_pointer_fixture(LONG handle)
{
    CUIS_WINDOW *window=input_window(handle);
    window->pointer_head=window->pointer_count=0;
    window->pointer_overflow=FALSE;
}
static int pointer_checks(void)
{
    LONG a, b, x, y, session;
    ULONG before;
    SBYTE packet[3]={(SBYTE)0xf8,0,0};
    run=&first;
    CHECK(!window_total && !pointer_capture && !pointer_hover);
    session=cuis_service(CUIS_SESSION_START,0);
    a=cuis_window_service(CUIS_WINDOW_CREATE,0,0,100,100,0);
    CHECK(a >= 0 && PW(CUIS_WINDOW_SHOW,a) == E_OK);
    CHECK(point_at(10,10,0) && pointer_hover == input_window(a));
    CHECK(cuis_state_service(session,0x1000,0x1004,0x1008,0x100c) == E_OK);
    CHECK(copied[0] == 10 && copied[1] == 10 && copied[2] == 0);
    CHECK(cuis_state_service(session,0,0x1004,0x1008,0x100c) == EIMBA);
    CHECK(cuis_event_service(a,0,0) == EINVAL && cuis_event_service(a,32,0) == EINVAL);
    CHECK(point_at(12,13,1) && PW(CUIS_FOCUS_QUERY,a) == 1 && pointer_capture == input_window(a));
    run=&second;
    CHECK(cuis_service(CUIS_SESSION_START,0) >= 0);
    b=cuis_window_service(CUIS_WINDOW_CREATE,20,20,20,20,0);
    CHECK(b > a && PW(CUIS_WINDOW_SHOW,b) == E_OK);
    CHECK(cuis_state_service(session,0x1000,0x1004,0x1008,0x100c) == EACCDN);
    CHECK(cuis_event_service(a,CUIS_EVENT_POINTER,0) == EACCDN);
    CHECK(point_at(25,25,1));
    run=&first; CHECK(pointer_capture == input_window(a));
    CHECK(point_at(25,25,0) && !pointer_capture);
    run=&second; CHECK(pointer_hover == input_window(b));
    CHECK(point_at(25,25,1) && PW(CUIS_FOCUS_QUERY,b) == 1);
    CHECK(point_at(25,25,0));
    CHECK(point_at(40,25,0)); /* Half-open edge belongs to underlying a. */
    run=&first; CHECK(pointer_hover == input_window(a));
    CHECK(point_at(40,25,1) && PW(CUIS_FOCUS_QUERY,a) == 1);
    CHECK(point_at(40,25,0));
    CHECK(point_at(25,25,1));
    run=&second;
    CHECK(pointer_capture == input_window(b));
    CHECK(PW(CUIS_WINDOW_HIDE,b) == E_OK && !pointer_capture);
    CHECK(point_at(30,30,1) && pointer_suppressed);
    CHECK(point_at(30,30,0) && !pointer_suppressed);
    run=&first;
    CHECK(!point_at(200,200,1) && !cuis_focus_active());
    CHECK(!point_at(20,20,1)); /* A legacy drag cannot be stolen on entry. */
    CHECK(!point_at(20,20,0));
    before=legacy_pointer_calls;
    point_at(20,20,0); cuis_mouse_packet(packet);
    CHECK(legacy_pointer_calls == before);
    CHECK(!point_at(200,200,0)); cuis_mouse_packet(packet);
    CHECK(legacy_pointer_calls == before+1);
    input_pointer_source=TRUE;
    input_pointer_x=input_pointer_y=5;
    input_pointer_max_x=input_pointer_max_y=9;
    CHECK(cuis_pointer_packet(100,100,0,&x,&y) && x == 9 && y == 9);
    input_pointer_x=input_pointer_y=8;
    CHECK(cuis_state_service(session,0x1000,0x1004,0x1008,0x100c) == E_OK);
    CHECK(copied[0] == 8 && copied[1] == 8);
    input_pointer_source=FALSE;
    point_at(20,20,1); cuis_process_exit(&first);
    CHECK(!pointer_capture && pointer_suppressed);
    point_at(200,200,0); cuis_process_exit(&second);
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
            CHECK(cuis_window_service(CUIS_WINDOW_IDENTIFY, h, 0, 0, 0, 0) == (LONG)(i * CUIS_WINDOWS_PER_SESSION + j + 1));
            if (!i && !j) oldest = h;
        }
    }
    CHECK(window_total == CUIS_WINDOW_LIMIT && cuis_window_count() == CUIS_WINDOW_LIMIT);
    CHECK(cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 1, 1, 0) == ENSMEM);
    /* Even an inconsistent global count must not publish identifier zero. */
    {
        ULONG released = frees, handles = next_window_handle;
        window_total--;
        CHECK(cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 1, 1, 0) == ENSMEM);
        CHECK(frees == released + 1 && next_window_handle == handles);
        CHECK(window_total == CUIS_WINDOW_LIMIT - 1 && (*find_owner(run))->window_count == 0);
        window_total++;
    }
    run = &first;
    CHECK(PW(CUIS_WINDOW_DESTROY, oldest) == E_OK);
    run = &fifth;
    {
        LONG replacement = cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 1, 1, 0);
        CHECK(replacement >= 0 && cuis_window_service(CUIS_WINDOW_IDENTIFY, replacement, 0, 0, 0, 0) == 1);
    }
    for (i = 0; i < 5; i++) cuis_process_exit(owners[i]);
    CHECK(!window_total && !cuis_window_count() && allocations == frees);
    return 0;
}
#undef PW
