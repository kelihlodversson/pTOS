#define CW(h,n,e,t) cuis_session_wait(h,n,e,t)
#define CR(h,n) cuis_stream_service(CUIS_STREAM_READ,h,0x20000,n,0,0)
#define CS(h,n) cuis_stream_service(CUIS_STREAM_WRITE,h,0x10000,n,0,0)
static int combined_checks(void)
{
    LONG a, b, window;
    ULONG yields;
    run = &first; a = cuis_service(CUIS_SESSION_START, 0);
    run = &second; b = cuis_service(CUIS_SESSION_START, 0); run = &first;
    CHECK(a >= 0 && b >= 0);
    CHECK(CW(1ULL << 32, 16, 1, 0) == EINVAL && CW(b, 16, 1, 0) == EACCDN);
    CHECK(CW(a, 16, 0, 0) == EINVAL && CW(a, 16, 4096, 0) == EINVAL);
    CHECK(CW(a, 0, 1, 0) == EINVAL && CW(a, 32768, 1, 0) == EINVAL);
    CHECK(CW(a, 16, 2, 1ULL << 32) == EINVAL);
    input_irqs = TRUE; delay_tick_step = 1;
    input_sleeps = 0; yields = delay_yields;
    CHECK(CW(a, 0, CUIS_READY_TIMER, 0) == CUIS_READY_TIMER && !input_sleeps && delay_yields == yields + 1);
    input_ticks = 0xfffffffeUL; input_sleeps = 0;
    CHECK(CW(a,0,CUIS_READY_SERVICE,0) == CUIS_READY_SERVICE && input_sleeps == 2);
    input_sleeps = 0;
    CHECK(CW(a,0,CUIS_READY_SERVICE|CUIS_READY_TIMER,0) == CUIS_READY_TIMER && !input_sleeps);
    CHECK(CS(a, 15) == 15);
    input_ticks = 0xfffffffeUL; input_sleeps = 0;
    CHECK(CW(a, 16, 3, 1) == CUIS_READY_TIMER && input_sleeps == 2);
    CHECK((*find_owner(&first))->stream_bytes == 15);
    CHECK(CS(a, 1) == 1);
    yields = delay_yields;
    CHECK(CW(a, 16, 3, 0) == (CUIS_READY_STREAM | CUIS_READY_TIMER) && delay_yields == yields);
    CHECK(CW(a, 16, 1, ~0ULL) == CUIS_READY_STREAM && (*find_owner(&first))->stream_bytes == 16);
    input_sleeps = 0;
    CHECK(CW(a, ~0ULL, CUIS_READY_TIMER, 6) == CUIS_READY_TIMER && input_sleeps == 3);
    CHECK((*find_owner(&first))->stream_bytes == 16 && CR(a, 16) == 16);
    stream_peer = &second; stream_peer_handle = a; stream_yield_peer = TRUE;
    input_sleeps = 0;
    CHECK(CW(a, 16, 3, 0) == (CUIS_READY_STREAM | CUIS_READY_TIMER) && !input_sleeps && CR(a, 16) == 16);
    /* The transitional opaque queue cannot masquerade as a GEM pipe. */
    CHECK(cuis_message_service(CUIS_MESSAGE_SEND, a, 1, 2, 3, 4, 0) == E_OK);
    input_sleeps = 0;
    CHECK(CW(a, 16, 3, 1) == CUIS_READY_TIMER && input_sleeps == 2);
    CHECK((*find_owner(&first))->message_count == 1);
    stream_peer = &second; stream_peer_handle = a; stream_send_peer = TRUE;
    input_sleeps = 0; stream_change_after = 1;
    CHECK(CW(a, 16, 3, 6) == CUIS_READY_STREAM && input_sleeps == 1 && CR(a, 16) == 16);
    input_ticks = 0xfffffffeUL; input_sleeps = 0; stream_change_after = 2;
    CHECK(CW(a, 16, 3, 1) == (CUIS_READY_STREAM | CUIS_READY_TIMER) && input_sleeps == 2);
    CHECK(CR(a, 16) == 16);
    input_sleeps = 0; stream_change_after = 1;
    CHECK(CW(a, 16, 1, ~0ULL) == CUIS_READY_STREAM && input_sleeps == 1 && CR(a, 16) == 16);
    stream_send_peer = FALSE;
    input_irqs = FALSE; input_sleeps = 0;
    CHECK(CW(a, 0, 2, 1) == CUIS_READY_TIMER && input_sleeps == 2 && !input_irqs);
    input_irqs = TRUE; delay_tick_step = 858993460UL; input_sleeps = 0;
    CHECK(CW(a, 0, 2, 0xffffffffULL) == CUIS_READY_TIMER && input_sleeps == 1);
    delay_tick_step = 1;
    input_pointer_source = TRUE;
    input_pointer_x = 401; input_pointer_y = 209; input_pointer_buttons = 3; input_modifiers = 15;
    CHECK(cuis_state_service(a, 0x1000, 0x1004, 0x1008, 0x100c) == E_OK);
    CHECK(copied[0] == 401 && copied[1] == 209 && copied[2] == 3 && copied[3] == 15);
    {
        BOOL initialized = pointer_initialized;
        pointer_initialized = FALSE; input_pointer_source = FALSE;
        CHECK(cuis_state_service(a, 0x1000, 0x1004, 0x1008, 0x100c) == E_OK);
        CHECK(copied[0] == 401 && copied[1] == 209 && copied[2] == 3);
        pointer_initialized = initialized;
    }
    CHECK(cuis_state_service(b, 0x1000, 0x1004, 0x1008, 0x100c) == EACCDN);
    CHECK(cuis_state_service(1ULL << 32, 0, 0, 0, 0) == EINVAL);
    CHECK(cuis_state_service(a, 0x1000, 0, 0x1008, 0x100c) == EIMBA);
    fail_copy_address = 0x1008;
    CHECK(cuis_state_service(a, 0x1000, 0x1004, 0x1008, 0x100c) == EIMBA);
    fail_copy_address = 0; input_irqs = FALSE;
    CHECK(cuis_state_service(a, 0x1000, 0x1004, 0x1008, 0x100c) == E_OK && !input_irqs);
    input_irqs = TRUE;
    window = cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 10, 10, 0);
    CHECK(window >= 0 && cuis_window_service(CUIS_WINDOW_SHOW, window, 0, 0, 0, 0) == E_OK);
    CHECK(cuis_window_service(CUIS_UPDATE_BEGIN, window, 0, 0, 0, 0) == E_OK);
    CHECK(CW(a, 16, 1, 0) == EACCDN && CW(a, 0, 2, 1) == EACCDN);
    CHECK(CW(a, 0, 2, 0) == CUIS_READY_TIMER && CS(a, 16) == 16);
    CHECK(CW(a, 16, 1, 0) == CUIS_READY_STREAM);
    /* A custom vector leaves the backend's Line-A fields untouched when
     * CUIS consumes a packet. State queries must use its logical state. */
    (void)cuis_pointer_packet((WORD)(5 - pointer_x), (WORD)(5 - pointer_y), 0, NULL, NULL);
    CHECK(cuis_pointer_packet(1, 1, 1, NULL, NULL));
    CHECK(cuis_state_service(a, 0x1000, 0x1004, 0x1008, 0x100c) == E_OK);
    CHECK(copied[0] == 6 && copied[1] == 6 && copied[2] == 1 && copied[3] == 15);
    CHECK(input_pointer_x == 401 && input_pointer_y == 209 && input_pointer_buttons == 3);
    input_pointer_source = TRUE; input_pointer_x = 17; input_pointer_y = 23; input_pointer_buttons = 2;
    CHECK(cuis_state_service(a, 0x1000, 0x1004, 0x1008, 0x100c) == E_OK);
    CHECK(copied[0] == 17 && copied[1] == 23 && copied[2] == 2);
    input_pointer_source = FALSE;
    (void)cuis_pointer_packet(0, 0, 0, NULL, NULL);
    cuis_process_exit(&first); cuis_process_exit(&second);
    CHECK(allocations == frees && !input_bad_idle && input_irqs);
    return 0;
}
#undef CW
#undef CR
#undef CS
