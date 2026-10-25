#define MW(fn, h) cuis_window_service(fn, h, ~0ULL, ~0ULL, ~0ULL, ~0ULL)
#define M(fn, a, b, c, d, e, f) cuis_message_service(fn, a, b, c, d, e, f)
#define MR(h) M(CUIS_MESSAGE_READ, h, 0x1000, 0x1004, 0x1008, 0x100c, 0x1010)
#define MS(h, a) M(CUIS_MESSAGE_SEND, h, a, 0xffffffffUL, 3, 4, ~0ULL)
#define MT(h, timeout) M(CUIS_MESSAGE_WAIT, h, timeout, ~0ULL, ~0ULL, ~0ULL, ~0ULL)
static int message_checks(void)
{
    LONG a, b, window, replacement;
    ULONG i;
    CHECK(!clients && allocations == frees);
    run = &first;
    CHECK(MS(0, 1) == EACCDN);
    a = cuis_service(CUIS_SESSION_START, 0);
    CHECK(a >= 0);
    CHECK(MT(a, 0) == 0); /* No window or focus exists. */
    CHECK(MS(a, 42) == E_OK && MT(a, -1LL) == 1);
    CHECK(MR(a) == 1 && copied[0] == a && copied[1] == 42);
    window = cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 10, 10, 0);
    CHECK(window >= 0 && MW(CUIS_WINDOW_SHOW, window) == E_OK);
    CHECK(M(CUIS_MESSAGE_READ, a, 0, 0, 0, 0, 0) == 0);
    CHECK(MS(a, 1ULL << 32) == EINVAL);
    CHECK(MS(1ULL << 32, 1) == EINVAL);
    CHECK(MS(0x7fffffffUL, 1) == EACCDN);
    run = &second;
    b = cuis_service(CUIS_SESSION_START, 0);
    CHECK(b > a);
    CHECK(MR(a) == EACCDN && MT(a, 0) == EACCDN);
    /* Standard GEM messages need no receiver-side grant or handshake. */
    for (i = 0; i < CUIS_MESSAGE_CAPACITY; i++) CHECK(MS(a, i) == E_OK);
    CHECK(MS(a, 99) == EAGAIN);
    run = &first;
    CHECK(cuis_event_service(window, CUIS_EVENT_MESSAGE, 0) == 1);
    CHECK(cuis_event_service(window, CUIS_EVENT_KEY | CUIS_EVENT_MESSAGE, 0) == 1);
    CHECK(M(CUIS_MESSAGE_READ, a, 0x1000, 0x1004, 0x1008, 0x100c, 0x100001010ULL) == EIMBA);
    CHECK(M(CUIS_MESSAGE_READ, a, 0x1000, 0x1004, 0x1008, 0x100c, 0) == EIMBA);
    fail_copy = TRUE;
    CHECK(MR(a) == EIMBA);
    fail_copy = FALSE;
    fail_copy_address = 0x1010;
    CHECK(MR(a) == EIMBA && copied[0] == b && copied[1] == 0);
    fail_copy_address = 0;
    for (i = 0; i < CUIS_MESSAGE_CAPACITY; i++) {
        CHECK(MR(a) == 1 && copied[0] == b && (ULONG)copied[1] == i);
        CHECK((ULONG)copied[2] == 0xffffffffUL && copied[3] == 3 && copied[4] == 4);
    }
    CHECK(MR(a) == 0 && cuis_event_service(window, CUIS_EVENT_MESSAGE, 0) == 0);
    /* The idle backend models another scheduled client at the sleep boundary. */
    input_message_sender = &second; input_message_destination = a;
    CHECK(cuis_event_service(window, CUIS_EVENT_MESSAGE, -1LL) == 1);
    CHECK(MR(a) == 1 && copied[0] == b && copied[1] == 11 && copied[4] == 44);
    input_message_sender = &second;
    CHECK(MT(a, -1LL) == 1 && MR(a) == 1);
    CHECK(MT(a, -2LL) == EINVAL);
    CHECK(MT(a, 1) == 0);
    CHECK(MW(CUIS_UPDATE_BEGIN, window) == E_OK);
    CHECK(cuis_event_service(window, CUIS_EVENT_MESSAGE, 1) == EACCDN);
    CHECK(MS(a, 123) == E_OK);
    CHECK(cuis_event_service(window, CUIS_EVENT_MESSAGE, -1LL) == 1);
    CHECK(MW(CUIS_UPDATE_END, window) == E_OK && MR(a) == 1);
    run = &second;
    CHECK(MS(a, 88) == E_OK);
    cuis_process_exit(&second);
    run = &first;
    CHECK(MR(a) == 1 && copied[0] == b && copied[1] == 88);
    run = &second;
    replacement = cuis_service(CUIS_SESSION_START, 0);
    CHECK(replacement > b && MR(b) == EACCDN);
    CHECK(MS(a, 99) == E_OK);
    run = &first;
    CHECK(MR(a) == 1 && copied[0] == replacement && copied[1] == 99);
    CHECK(MS(a, 1) == E_OK); /* Exit discards the pending queue. */
    cuis_process_exit(&first); cuis_process_exit(&second);
    CHECK(!clients && allocations == frees);
    run = &first;
    CHECK(cuis_service(CUIS_SESSION_START, 0) > replacement && MR(a) == EACCDN);
    cuis_process_exit(&first);
    return 0;
}
#undef M
#undef MR
#undef MS
#undef MT
#undef MW
