/* Generic content/border routing never interprets GEM gadgets or messages. */
#define CREAD(h) cuis_control_service(h,0x1000,0x1004,0x1008,0x100c)
static int control_checks(void)
{
    LONG a, b, wa, wb, wc, cover;
    CUIS_CLIENT *client;
    ULONG i;
    CUIS_INPUT_WAIT wait = { 0 };
    run = &first; a = cuis_service(CUIS_SESSION_START,0);
    wa = cuis_window_service(CUIS_WINDOW_CREATE,20,30,100,80,0);
    CHECK(a >= 0 && wa >= 0);
    CHECK(cuis_window_service(CUIS_WINDOW_CONTENT,wa,1,22,1,1) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_CONTENT,wa,32768,0,0,0) == EINVAL);
    CHECK(cuis_window_service(CUIS_WINDOW_POLICY,wa,1,0,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW,wa,0,0,0,0) == E_OK);
    client = *find_owner(run);
    pointer_suppressed = pointer_legacy_drag = FALSE;
    CHECK(point_at(40,70,0) && point_at(40,70,1) && point_at(40,70,0));
    CHECK(client->edge_count == 2);
    CHECK(point_at(25,35,1) && point_at(25,35,0));
    CHECK(client->edge_count == 2); /* Queued work clicks survive a border click. */
    CHECK(cuis_session_wait(a,0,CUIS_READY_CONTROL,0) == CUIS_READY_CONTROL);
    fail_copy = TRUE; CHECK(CREAD(wa) == EIMBA); fail_copy = FALSE;
    CHECK(CREAD(wa) == 1 && copied[0] == 25 && copied[1] == 35 && copied[2] == 1 && copied[3] == 1);
    CHECK(CREAD(wa) == 1 && copied[2] == 0 && copied[3] == 1);
    CHECK(CREAD(wa) == 0);
    CHECK(cuis_input_config(CUIS_INPUT_BUTTON,a,1,1,1,0,0) == E_OK);
    client->edge_count = client->recent_count = 0;
    wait.events = CUIS_READY_BUTTON; client->input_wait = &wait;
    CHECK(point_at(25,35,1) && point_at(60,75,1) && point_at(60,75,0));
    CHECK(!wait.ready && !client->edge_count); client->input_wait = NULL;
    CHECK(CREAD(wa) == 1 && copied[2] == 1);
    CHECK(CREAD(wa) == 1 && copied[0] == 60 && copied[2] == 1);
    CHECK(CREAD(wa) == 1 && copied[2] == 0 && CREAD(wa) == 0);
    /* Rectangle exit/entry remains live over borders, independently of
     * suppression of button quick checks and ordinary button history. */
    CHECK(point_at(60,75,0));
    CHECK(cuis_input_config(CUIS_INPUT_RECT,a,2,21,52,98,57) == E_OK && cuis_input_config(CUIS_INPUT_RECT,a,1,20,30,100,22) == E_OK);
    memset(&wait,0,sizeof(wait)); wait.events = CUIS_READY_RECT1|CUIS_READY_RECT2|CUIS_READY_BUTTON;
    client->input_wait = &wait;
    CHECK(point_at(25,35,0));
    CHECK((wait.ready & (CUIS_READY_RECT1|CUIS_READY_RECT2)) == (CUIS_READY_RECT1|CUIS_READY_RECT2));
    CHECK(!(wait.ready & CUIS_READY_BUTTON)); client->input_wait = NULL;
    /* Surface clipping changes can occlude a gadget with no focus change. */
    CHECK(point_at(25,35,1));
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,a,40) == E_OK);
    CHECK(focus_window == client->windows && window_front == client->windows && !pointer_capture);
    CHECK(CREAD(wa) == ERANGE && CREAD(wa) == 0 && point_at(25,35,0));
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,a,0) == E_OK);
    CHECK(point_at(25,35,1) && point_at(25,35,0));
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,a,10) == E_OK);
    CHECK(CREAD(wa) == ERANGE && CREAD(wa) == 0);
    CHECK(point_at(25,35,1));
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,a,40) == E_OK);
    CHECK(CREAD(wa) == ERANGE && CREAD(wa) == 0 && point_at(25,35,0));
    CHECK(cuis_surface_service(CUIS_SURFACE_REGISTER,a,0) == E_OK);
    /* An unmanaged occluder may disappear before the owner reads its
     * captured release. Restored focus/front cannot revive that gesture. */
    cover = cuis_window_service(CUIS_WINDOW_CREATE,20,30,100,22,0);
    CHECK(cover >= 0 && point_at(25,35,1));
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW,cover,0,0,0,0) == E_OK);
    CHECK(focus_window == client->windows->next && !pointer_capture && pointer_suppressed);
    CHECK(point_at(25,35,0));
    CHECK(cuis_window_service(CUIS_WINDOW_HIDE,cover,0,0,0,0) == E_OK);
    CHECK(CREAD(wa) == ERANGE && CREAD(wa) == 0);
    /* Lower an already visible unmanaged cover, then raise it transiently. */
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW,cover,0,0,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_LOWER,cover,0,0,0,0) == E_OK);
    CHECK(point_at(25,35,1) && point_at(25,35,0));
    CHECK(cuis_window_service(CUIS_WINDOW_RAISE,cover,0,0,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_HIDE,cover,0,0,0,0) == E_OK);
    CHECK(CREAD(wa) == ERANGE && CREAD(wa) == 0);
    CHECK(cuis_window_service(CUIS_WINDOW_DESTROY,cover,0,0,0,0) == E_OK);
    /* A topology change cancels a captured border drag and its release. */
    CHECK(point_at(25,35,1));
    run = &second; b = cuis_service(CUIS_SESSION_START,0);
    wb = cuis_window_service(CUIS_WINDOW_CREATE,0,0,10,10,0);
    CHECK(b >= 0 && wb >= 0 && cuis_window_service(CUIS_WINDOW_POLICY,wb,1,0,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_SHOW,wb,0,0,0,0) == E_OK);
    CHECK(!pointer_capture && pointer_suppressed);
    CHECK(point_at(25,35,0));
    run = &first;
    CHECK(CREAD(wb) == EACCDN && CREAD(wa) == ERANGE && CREAD(wa) == 0);
    CHECK(!client->edge_count);
    /* First click on an inactive window requests activation only. */
    CHECK(point_at(25,35,1) && point_at(25,35,0));
    CHECK(CREAD(wa) == 0 && (client->windows->notices & CUIS_NOTICE_ACTIVATE));
    CHECK(cuis_window_service(CUIS_WINDOW_RAISE,wa,0,0,0,0) == E_OK);
    for (i = 0; i < CUIS_POINTER_CAPACITY; i++) {
        CHECK(point_at(25,35,1) && point_at(25,35,0));
    }
    CHECK(CREAD(wa) == ERANGE); /* Lost edges cannot complete an old gesture. */
    CHECK(cuis_window_service(CUIS_WINDOW_CONFIGURE,wa,21,30,100,80) == E_OK);
    CHECK(CREAD(wa) == ERANGE && CREAD(wa) == 0);
    wc = cuis_window_service(CUIS_WINDOW_CREATE,0,0,10,10,0);
    CHECK(wc >= 0 && cuis_window_service(CUIS_WINDOW_SHOW,wc,0,0,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_RAISE,wa,0,0,0,0) == E_OK);
    CHECK(point_at(25,35,1));
    CHECK(cuis_event_service(wc,CUIS_EVENT_CONTROL,0) == 0);
    CHECK(cuis_event_service(wa,CUIS_EVENT_CONTROL,0) == 1 && CREAD(wa) == 1);
    CHECK(cuis_event_service(wa,CUIS_EVENT_CONTROL,0) == 0);
    CHECK(point_at(25,35,0));
    /* A peer may grab the released mouse, then wait for our display lock.
     * Cancellation must remain readable while that peer owns the mouse. */
    CHECK(cuis_display_lock(a,1,0) == E_OK);
    run = &second; CHECK(cuis_mouse_lock(b,1,0) == E_OK);
    run = &first;
    CHECK(cuis_event_service(wa,CUIS_EVENT_CONTROL,0) == 1);
    CHECK(cuis_session_wait(a,0,CUIS_READY_CONTROL,0) == CUIS_READY_CONTROL);
    CHECK(CREAD(wa) == ERANGE && cuis_event_service(wa,CUIS_EVENT_CONTROL,0) == 0);
    run = &second; CHECK(cuis_mouse_lock(b,0,0) == E_OK);
    run = &first; CHECK(cuis_display_lock(a,0,0) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_DESTROY,wc,0,0,0,0) == E_OK);
    CHECK(point_at(25,35,1));
    CHECK(cuis_window_service(CUIS_WINDOW_HIDE,wa,0,0,0,0) == E_OK);
    CHECK(CREAD(wa) == ERANGE && point_at(25,35,0));
    cuis_process_exit(&first); cuis_process_exit(&second);
    CHECK(allocations == frees);
    return 0;
}
