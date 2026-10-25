#define MW(fn, h) cuis_window_service(fn, h, ~0ULL, ~0ULL, ~0ULL, ~0ULL)
#define M(fn, a, b, c, d, e, f) cuis_message_service(fn, a, b, c, d, e, f)
#define MR(h) M(CUIS_MESSAGE_READ, h, 0x1000, 0x1004, 0x1008, 0x100c, 0x1010)
#define MS(h, a) M(CUIS_MESSAGE_SEND, h, a, 0xffffffffUL, 3, 4, ~0ULL)
#define MG(fn, h, peer) M(fn, h, peer, ~0ULL, ~0ULL, ~0ULL, ~0ULL)
static int message_checks(void)
{
    LONG a, b, window, replacement;
    ULONG i;
    static PD grant_owners[CUIS_MESSAGE_GRANTS + 1];
    LONG handles[CUIS_MESSAGE_GRANTS + 1];
    CHECK(!clients && allocations == frees);
    run = &first;
    CHECK(MS(0, 1) == EACCDN);
    a = cuis_service(CUIS_SESSION_START, 0);
    CHECK(a >= 0);
    window = cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 10, 10, 0);
    CHECK(window >= 0 && MW(CUIS_WINDOW_SHOW, window) == E_OK);
    CHECK(M(CUIS_MESSAGE_READ, a, 0, 0, 0, 0, 0) == 0);
    CHECK(MS(a, 1ULL << 32) == EINVAL);
    CHECK(MS(1ULL << 32, 1) == EINVAL);
    CHECK(MG(CUIS_MESSAGE_ALLOW, a, ~0ULL) == EINVAL);
    CHECK(MG(CUIS_MESSAGE_ALLOW, a, 0x7fffffffUL) == EACCDN);
    run = &second;
    b = cuis_service(CUIS_SESSION_START, 0);
    CHECK(b > a);
    CHECK(MS(a, 1) == EACCDN);
    CHECK(MR(a) == EACCDN && MG(CUIS_MESSAGE_ALLOW, a, b) == EACCDN);
    run = &first;
    CHECK(MG(CUIS_MESSAGE_ALLOW, a, b) == E_OK);
    CHECK(MG(CUIS_MESSAGE_ALLOW, a, b) == E_OK && clients->next->grant_count == 1);
    run = &second;
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
    CHECK(MG(CUIS_MESSAGE_DENY, a, b) == E_OK);
    run = &second;
    CHECK(MS(a, 99) == EACCDN); /* Revocation takes precedence over capacity. */
    run = &first;
    for (i = 0; i < CUIS_MESSAGE_CAPACITY; i++) {
        CHECK(MR(a) == 1 && copied[0] == b && (ULONG)copied[1] == i);
        CHECK((ULONG)copied[2] == 0xffffffffUL && copied[3] == 3 && copied[4] == 4);
    }
    CHECK(MR(a) == 0 && cuis_event_service(window, CUIS_EVENT_MESSAGE, 0) == 0);
    CHECK(MG(CUIS_MESSAGE_ALLOW, a, b) == E_OK);
    /* The idle backend models another scheduled client at the sleep boundary. */
    input_message_sender = &second; input_message_destination = a;
    CHECK(cuis_event_service(window, CUIS_EVENT_MESSAGE, -1LL) == 1);
    CHECK(MR(a) == 1 && copied[0] == b && copied[1] == 11 && copied[4] == 44);
    input_message_sender = &second;
    CHECK(MG(CUIS_MESSAGE_WAIT, a, -1LL) == 1 && MR(a) == 1);
    CHECK(MG(CUIS_MESSAGE_WAIT, a, -2LL) == EINVAL);
    CHECK(MG(CUIS_MESSAGE_WAIT, a, 1) == 0);
    CHECK(MW(CUIS_UPDATE_BEGIN, window) == E_OK);
    CHECK(cuis_event_service(window, CUIS_EVENT_MESSAGE, 1) == EACCDN);
    CHECK(MS(a, 123) == E_OK);
    CHECK(cuis_event_service(window, CUIS_EVENT_MESSAGE, -1LL) == 1);
    CHECK(MW(CUIS_UPDATE_END, window) == E_OK && MR(a) == 1);
    run = &second;
    CHECK(MS(a, 88) == E_OK);
    cuis_process_exit(&second);
    run = &first;
    CHECK(!(*find_owner(run))->grant_count);
    CHECK(MR(a) == 1 && copied[0] == b && copied[1] == 88);
    run = &second;
    replacement = cuis_service(CUIS_SESSION_START, 0);
    CHECK(replacement > b && MS(a, 99) == EACCDN);
    run = &first;
    CHECK(MG(CUIS_MESSAGE_DENY, a, b) == E_OK); /* Stale grant removal is harmless. */
    for (i = 0; i <= CUIS_MESSAGE_GRANTS; i++) {
        run = &grant_owners[i];
        handles[i] = cuis_service(CUIS_SESSION_START, 0);
        CHECK(handles[i] >= 0);
        run = &first;
        CHECK(MG(CUIS_MESSAGE_ALLOW, a, handles[i]) == (i < CUIS_MESSAGE_GRANTS ? E_OK : ENSMEM));
    }
    CHECK(MG(CUIS_MESSAGE_DENY, a, handles[0]) == E_OK);
    CHECK(MG(CUIS_MESSAGE_ALLOW, a, handles[CUIS_MESSAGE_GRANTS]) == E_OK);
    for (i = 0; i <= CUIS_MESSAGE_GRANTS; i++) cuis_process_exit(&grant_owners[i]);
    CHECK(!(*find_owner(&first))->grant_count);
    CHECK(MS(a, 1) == E_OK); /* Self-posts need no grant. Exit discards the queue. */
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
#undef MG
#undef MW
