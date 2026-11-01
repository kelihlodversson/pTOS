/* Standard window helpers, colours and desktop contracts over failing transport. */
static int window_pointer_checks(short *global)
{
    static const char title[] = "A title longer than one WORD";
    static const char info[] = "An info line longer than one WORD";
    const char *texts[2] = { title,info };
    short fields[2] = { WF_NAME,WF_INFO }, id, field, call, i;
    short guarded[12], pair[2];
    void *pointer;
    ULONG expected;
    id = wind_create(NAME|INFO,0,0,100,80);
    CHECK(id > 0);
    for (field = 0; field < 2; field++) {
        CHECK(wind_set_ptr(id,fields[field],(void *)texts[field]) == 1);
        CHECK((UQUAD)(uintptr_t)texts[field] <= 0xffffffffUL);
        expected = (ULONG)(uintptr_t)texts[field];
        for (call = 0; call < 2; call++) {
            for (i = 0; i < 12; i++) guarded[i] = 0x5555;
            if (call) CHECK(mt_wind_get(id,fields[field],guarded+1,guarded+4,guarded+7,guarded+10,global) == 1);
            else CHECK(wind_get(id,fields[field],guarded+1,guarded+4,guarded+7,guarded+10) == 1);
            CHECK((UWORD)guarded[1] == (UWORD)(expected >> 16) && (UWORD)guarded[4] == (UWORD)expected);
            for (i = 0; i < 12; i++) if (i != 1 && i != 4) CHECK(guarded[i] == 0x5555);
        }
        CHECK(mt_wind_get(id,fields[field],pair,pair+1,NULL,NULL,global) == 1);
        CHECK((((ULONG)(UWORD)pair[0] << 16)|(UWORD)pair[1]) == expected);
        CHECK(wind_get_ptr(id,fields[field],&pointer) == 1 && pointer == texts[field]);
        CHECK(mt_wind_get_ptr(id,fields[field],&pointer,global) == 1 && pointer == texts[field]);
        guarded[1] = guarded[4] = 0x5555;
        CHECK(!wind_get(id,fields[field],guarded+1,NULL,NULL,NULL) && guarded[1] == 0x5555);
        CHECK(!mt_wind_get(id,fields[field],NULL,guarded+4,NULL,NULL,global) && guarded[4] == 0x5555);
        CHECK(!mt_wind_get(id,fields[field],guarded+1,guarded+4,NULL,NULL,NULL));
        CHECK(guarded[1] == 0x5555 && guarded[4] == 0x5555);
    }
    CHECK(wind_delete(id) == 1);
    return 0;
}

static int default_colour_checks(short *global)
{
    short gadget, top, bottom, flags, slot;
    CHECK(wind_set(0,WF_DCOLOR,W_NAME,0x11a1,0x1101,0) == 1);
    colour_peer_pending = TRUE; gadget = W_NAME;
    CHECK(wind_get(0,WF_DCOLOR,&gadget,&top,&bottom,&flags) == 1);
    CHECK(top == 0x11a1 && bottom == 0x1101 && !display_nesting && !colour_peer_pending);
    CHECK(wind_get(0,WF_DCOLOR,&gadget,&top,&bottom,&flags) == 1 && top == 0x1172 && bottom == 0x1174);
    CHECK(wind_set(0,WF_DCOLOR,W_NAME,0x11a1,0x1101,0) == 1);
    CHECK(wind_update(BEG_UPDATE) == 1);
    colour_peer_pending = TRUE;
    CHECK(mt_wind_get(0,WF_DCOLOR,&gadget,&top,&bottom,&flags,global) == 1);
    CHECK(top == 0x11a1 && bottom == 0x1101 && display_nesting == 1 && colour_peer_pending);
    CHECK(wind_update(END_UPDATE) == 1 && !colour_peer_pending);
    CHECK(wind_get(0,WF_DCOLOR,&gadget,&top,&bottom,&flags) == 1 && top == 0x1172 && bottom == 0x1174);
    top = bottom = flags = 123;
    display_error = EAGAIN;
    CHECK(!wind_get(0,WF_DCOLOR,&gadget,&top,&bottom,&flags) && !display_nesting);
    CHECK(gadget == W_NAME && top == 123 && bottom == 123 && flags == 123);
    display_error = 0;
    for (slot = 0; slot < 2; slot++) {
        colour_fail_slot = slot ? 19+W_NAME : W_NAME;
        CHECK(wind_update(BEG_UPDATE) == 1);
        CHECK(!mt_wind_get(0,WF_DCOLOR,&gadget,&top,&bottom,&flags,global));
        CHECK(display_nesting == 1 && gadget == W_NAME && top == 123 && bottom == 123 && flags == 123);
        CHECK(wind_update(END_UPDATE) == 1);
    }
    colour_fail_slot = -1;
    CHECK(wind_set(0,WF_DCOLOR,W_NAME,0x11a1,0x1101,0) == 1 && !display_nesting);
    return 0;
}

static int background_event_checks(short *global)
{
    short message[8], saved[8], key = 99, clicks = 99, x = 99, y = 99, buttons = 99, modifiers = 99;
    ULONG before;
    __builtin_memset(message,0x55,sizeof(message));
    __builtin_memcpy(saved,message,sizeof(saved));
    input_ready = 0; background_on_wait = 1; before = wait_calls;
    CHECK(evnt_multi(MU_MESAG|MU_TIMER,0,0,0,0,0,0,0,0,0,0,0,0,0,message,0,
        &x,&y,&buttons,&modifiers,&key,&clicks) == MU_TIMER);
    CHECK(wait_calls == before+1 && !(background_state & 2));
    CHECK(!__builtin_memcmp(message,saved,sizeof(saved)) && key == 0 && clicks == 0);
    input_ready = CUIS_READY_INPUT; background_on_wait = 1; before = wait_calls;
    CHECK(mt_evnt_multi(MU_MESAG|MU_TIMER|MU_KEYBD|MU_BUTTON|MU_M1|MU_M2,1,1,1,
        0,0,0,100,100,0,0,0,100,100,message,0,&x,&y,&buttons,&modifiers,&key,&clicks,global) ==
        (MU_TIMER|MU_KEYBD|MU_BUTTON|MU_M1|MU_M2));
    CHECK(wait_calls == before+1 && key == 0x1e61 && clicks == 2);
    CHECK(x == 17 && y == 23 && buttons == 2 && modifiers == 5);
    CHECK(!__builtin_memcmp(message,saved,sizeof(saved)));
    /* A notice-only wake re-arms against the original timer deadline. */
    input_ready = 0; background_on_wait = background_only_wakes = 1;
    before = wait_calls; menu_ticks = 100;
    CHECK(evnt_multi(MU_MESAG|MU_TIMER,0,0,0,0,0,0,0,0,0,0,0,0,0,message,100,
        &x,&y,&buttons,&modifiers,&key,&clicks) == MU_TIMER);
    CHECK(wait_calls == before+2 && waited_interval == 85 && !background_only_wakes && !background_on_wait);
    /* A real message still combines with the timer during desktop damage. */
    saved[0] = 123; saved[1] = gl_apid;
    CHECK(appl_write(gl_apid,sizeof(saved),saved) == 1);
    background_on_wait = 1;
    CHECK(evnt_multi(MU_MESAG|MU_TIMER,0,0,0,0,0,0,0,0,0,0,0,0,0,message,0,
        &x,&y,&buttons,&modifiers,&key,&clicks) == (MU_MESAG|MU_TIMER));
    CHECK(!pipe_count && !__builtin_memcmp(message,saved,sizeof(saved)));
    read_error = EIMBA; background_on_wait = 1; x = 123;
    CHECK(evnt_multi(MU_MESAG|MU_TIMER,0,0,0,0,0,0,0,0,0,0,0,0,0,message,0,
        &x,&y,&buttons,&modifiers,&key,&clicks) == 0 && x == 123);
    read_error = 0;
    return 0;
}

static int window_desktop_api_checks(short *global)
{
    short a, b, c, d, old, fresh, invalid[15] = {0};
    GRECT rect = {10,20,100,80}, result;
    OBJECT tree = { NIL,NIL,NIL,G_BOX,LASTOB,NORMAL,{0x1173},0,0,640,400 };
    void *pointer, *again;
    ULONG before;
    CHECK(window_pointer_checks(global) == 0);
    CHECK(default_colour_checks(global) == 0);
    CHECK(mt_wind_new(NULL) == 0 && mt_wind_set_ptr(0,WF_NEWDESK,&tree,invalid) == 0);
    CHECK(wind_get_grect(0,WF_CURRXYWH,&result) == 1 && result.g_w == 640 && result.g_h == 400);
    CHECK(mt_wind_get_grect(0,WF_WORKXYWH,&result,global) == 1 && result.g_y == 0);
    CHECK(wind_get(0,WF_KIND,&a,&b,&c,&d) == 1 && !(a|b|c|d));
    CHECK(wind_get(0,WF_OWNER,&a,&b,&c,&d) == 1 && !a && b == 1);
    CHECK(wind_get_grect(0,WF_FIRSTXYWH,&result) == 1 && result.g_w == 640);
    CHECK(wind_get_grect(0,WF_NEXTXYWH,&result) == 1 && !result.g_w && !result.g_h);
    CHECK(!wind_get_grect(0,WF_WORKXYWH,NULL));
    a = W_NAME;
    CHECK(wind_get(0,WF_DCOLOR,&a,&b,&c,&d) == 1 && a == W_NAME && b == 0x11a1 && c == 0x1101 && d == 0x0f0f);
    old = wind_create_grect(NAME,&rect); CHECK(old > 0);
    CHECK(wind_set(-1,WF_DCOLOR,W_NAME,0x1172,0x1174,0) == 1);
    fresh = mt_wind_create_grect(NAME,&rect,global); CHECK(fresh > 0);
    a = W_NAME;
    CHECK(wind_get(old,WF_COLOR,&a,&b,&c,&d) == 1 && b == 0x11a1 && c == 0x1101);
    CHECK(wind_get(fresh,WF_COLOR,&a,&b,&c,&d) == 1 && b == 0x1172 && c == 0x1174);
    CHECK(wind_set(fresh,WF_COLOR,W_NAME,-1,0x1173,0) == 1);
    CHECK(wind_get(fresh,WF_COLOR,&a,&b,&c,&d) == 1 && b == 0x1172 && c == 0x1173);
    a = -1; b = c = 123;
    CHECK(!wind_get(fresh,WF_COLOR,&a,&b,&c,&d) && a == -1 && b == 123 && c == 123);
    CHECK(!wind_set(fresh,WF_COLOR,19,0,0,0) && !wind_set(0,WF_DCOLOR,-1,0,0,0));
    CHECK(wind_set(0,WF_DCOLOR,W_NAME,0x11a1,0x1101,0) == 1);
    CHECK(wind_set_ptr(fresh,WF_NAME,(void *)"Colour title") == 1);
    CHECK(mt_wind_get_ptr(fresh,WF_NAME,&pointer,global) == 1 && !__builtin_strcmp(pointer,"Colour title"));
    CHECK(wind_open_grect(fresh,&rect) == 1);
    frame_paint_failure = TRUE;
    CHECK(!wind_set(fresh,WF_COLOR,W_NAME,0x1175,0x1176,0));
    frame_paint_failure = FALSE; a = W_NAME;
    CHECK(wind_get(fresh,WF_COLOR,&a,&b,&c,&d) == 1 && b == 0x1172 && c == 0x1173);
    CHECK(wind_calc_grect(WC_WORK,0,&rect,&result) == 1 && result.g_x == 11 && result.g_y == 21);
    CHECK(mt_wind_calc_grect(WC_BORDER,0,&result,&result,global) == 1 && result.g_w == rect.g_w);
    CHECK(mt_wind_set_grect(fresh,WF_CURRXYWH,&rect,global) == 1);
    CHECK(wind_get_ptr(0,WF_SCREEN,&pointer) == 1 && pointer);
    CHECK(mt_wind_get_ptr(0,WF_SCREEN,&again,global) == 1 && pointer == again);
    region_alloc_failure = TRUE;
    /* Existing compatible storage does not allocate again. */
    CHECK(wind_get_ptr(0,WF_SCREEN,&again) == 1 && pointer == again);
    region_alloc_failure = FALSE;
    CHECK(mt_wind_set_ptr_int(0,WF_NEWDESK,&tree,0,global) == 1);
    CHECK(wind_get_ptr(0,WF_NEWDESK,&pointer) == 1 && pointer == &tree);
    CHECK(aes_desktop_registered());
    before = desktop_paints;
    CHECK(evnt_timer(0) == 1 && desktop_paints > before && !(background_state & 2));
    CHECK(background_event_checks(global) == 0);
    background_error = EACCDN; pointer = &rect;
    CHECK(!wind_get_ptr(0,WF_NEWDESK,&pointer) && pointer == &rect);
    CHECK(!wind_set_ptr(0,WF_NEWDESK,NULL)); background_error = 0;
    CHECK(wind_get_ptr(0,WF_NEWDESK,&pointer) == 1 && pointer == &tree);
    CHECK(wind_set_ptr(0,WF_NEWDESK,NULL) == 1 && wind_get_ptr(0,WF_NEWDESK,&pointer) == 1 && !pointer);
    CHECK(evnt_timer(0) == 1 && !display_nesting);
    CHECK(wind_update(BEG_UPDATE) && wind_update(BEG_UPDATE) && wind_update(BEG_MCTRL));
    reset_error = EACCDN;
    CHECK(!mt_wind_new(global) && wind_get(old,WF_KIND,&a,&b,&c,&d)); reset_error = 0;
    CHECK(mt_wind_new(global) == 1 && !display_nesting && !mouse_nesting);
    CHECK(!wind_get(old,WF_KIND,&a,&b,&c,&d) && !wind_get(fresh,WF_KIND,&a,&b,&c,&d));
    CHECK(appl_find(NULL) == gl_apid && wind_create_grect(0,NULL) == -1);
    CHECK(wind_create_grect(0,&rect) > 0 && wind_new() == 1);
    aes_desktop_reset(); background_state = 0;
    return 0;
}
