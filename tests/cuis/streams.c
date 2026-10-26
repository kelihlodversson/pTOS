#define SW(h,p,n,b) cuis_stream_service(CUIS_STREAM_WRITE,h,p,n,b,0)
#define SR(h,p,n,b,s) cuis_stream_service(CUIS_STREAM_READ,h,p,n,b,s)
static int stream_checks(void)
{
    LONG a, b;
    ULONG i, before;
    run = &first; a = cuis_service(CUIS_SESSION_START, 0);
    run = &second; b = cuis_service(CUIS_SESSION_START, 0);
    CHECK(a >= 0 && b >= 0);
    CHECK(cuis_directory_service(CUIS_SESSION_NAME, b, 0x20000, 33) == 11);
    CHECK(!memcmp(stream_output, "GemTest.Prg", 12));
    CHECK(cuis_directory_service(CUIS_SESSION_NAME, b, 0x20000, 1) == ERANGE);
    memcpy(stream_input, "GEMTEST ", 8);
    CHECK(cuis_directory_service(CUIS_SESSION_LABEL, a, 0x10000, 8) == EACCDN);
    CHECK(cuis_directory_service(CUIS_SESSION_LABEL, b, 0x10000, 8) == E_OK);
    CHECK(cuis_directory_service(CUIS_LABEL_FIND, 0x10000, 8, 0) == b);
    run = &first;
    CHECK(cuis_directory_service(CUIS_PEER_BY_PID, 2, 0, 0) == b);
    CHECK(cuis_directory_service(CUIS_PEER_PID, b, 0, 0) == 2);
    CHECK(cuis_directory_service(CUIS_SESSION_LABEL, a, 0x10000, 8) == E_OK);
    CHECK(cuis_directory_service(CUIS_LABEL_FIND, 0x10000, 8, 0) == a); /* Oldest duplicate. */
    CHECK(cuis_directory_service(CUIS_SESSION_LABEL, a, 0, 0) == E_OK);
    CHECK(cuis_directory_service(CUIS_LABEL_FIND, 0x10000, 8, 0) == b);
    CHECK(cuis_directory_service(CUIS_LABEL_FIND, 0x10000, 33, 0) == EINVAL);
    CHECK(cuis_directory_service(CUIS_LABEL_FIND, 0, 8, 0) == EIMBA);
    for (i = 0; i < 32768; i++) stream_input[i] = (UBYTE)i;
    CHECK(SW(b, 0, 0, 0) == 0);
    CHECK(SW(b, 0, 32768, 0) == EINVAL);
    CHECK(SW(b, 0, 1, 0) == EIMBA);
    fail_allocation = TRUE; CHECK(SW(b, 0x10000, 16, 0) == ENSMEM); fail_allocation = FALSE;
    fail_at_allocation = allocations + 1;
    CHECK(SW(b, 0x10000, 16, 0) == ENSMEM && !(*find_owner(&first))->pending_write && !(*find_owner(&second))->stream_data);
    fail_at_allocation = 0;
    CHECK(SW(b, 0x10000, 32767, 0) == 32767);
    CHECK(SW(b, 0x10000, 32767, 0) == 32767);
    CHECK(SW(b, 0x10000, 2, 0) == 2);
    CHECK(SW(b, 0x10000, 1, 0) == EAGAIN);
    CHECK(SR(b, 0x20000, 16, 0, 0) == EACCDN);
    run = &second;
    CHECK(SR(b, 0, 16, 0, 0) == EIMBA);
    fail_copy_address = 0x1000;
    CHECK(SR(b, 0x20000, 16, 0, 0x1000) == EIMBA);
    fail_copy_address = 0;
    CHECK(SR(b, 0x20000, 16, 0, 0x1000) == 16 && copied[0] == 1);
    CHECK(!memcmp(stream_output, stream_input, 16));
    run = &first;
    CHECK((*find_owner(&second))->stream_bytes == 65520); /* Partial reads immediately free ring capacity. */
    run = &second;
    CHECK(SR(b, 0x20000, 32767, 0, 0) == 32767); /* Crosses a write boundary. */
    for (i = 0; i < 32751; i++) CHECK(stream_output[i] == (UBYTE)(i + 16));
    for (i = 32751; i < 32767; i++) CHECK(stream_output[i] == (UBYTE)(i - 32751));
    CHECK(SR(b, 0x20000, 32754, 0, 0) == EAGAIN);
    CHECK(SR(b, 0x20000, 32753, 0, 0) == 32753);
    CHECK(SR(b, 0x20000, 1, 0, 0) == EAGAIN);
    run = &first;
    CHECK(SW(b, 0x10000, 32767, 0) == 32767);
    CHECK(SW(b, 0x10000, 32767, 0) == 32767);
    CHECK(SW(b, 0x10000, 2, 0) == 2);
    CHECK(SW(b, 0x10000, 16, 0) == EAGAIN);
    stream_drain_peer = TRUE; stream_peer = &second; stream_peer_handle = b;
    input_sleeps = 0; stream_change_after = 1;
    CHECK(SW(b, 0x10000, 16, 1) == 16 && input_sleeps == 1 && input_irqs);
    {
        CUIS_CLIENT *peer = *find_owner(&second);
        CHECK(peer->stream_data[(peer->stream_head + peer->stream_bytes - 16) % CUIS_STREAM_BYTES] == 0 && stream_input[0] == 99);
    }
    stream_drain_peer = FALSE;
    before = frees; stream_kill_peer = TRUE; input_sleeps = 0; stream_change_after = 1;
    CHECK(SW(b, 0x10000, 16, 1) == EACCDN && frees > before);
    stream_kill_peer = FALSE;
    CHECK(cuis_directory_service(CUIS_PEER_BY_PID, 2, 0, 0) == EFILNF);
    memcpy(stream_input, "GEMTEST ", 8);
    CHECK(cuis_directory_service(CUIS_LABEL_FIND, 0x10000, 8, 0) == EFILNF);
    run = &second; b = cuis_service(CUIS_SESSION_START, 0); run = &first;
    stream_peer_handle = a; stream_send_peer = TRUE;
    input_sleeps = 0; stream_change_after = 1;
    CHECK(SR(a, 0x20000, 16, 1, 0x1000) == 16 && input_sleeps == 1 && copied[0] == 2);
    stream_send_peer = FALSE;
    run = &second;
    CHECK(SW(a, 0x10000, 16, 0) == 16);
    cuis_process_exit(&second); run = &first;
    CHECK(SR(a, 0x20000, 16, 0, 0x1000) == 16 && copied[0] == 2); /* Historical sender identity. */
    CHECK(SW(a, 0x10000, 32767, 0) == 32767 && SR(a, 0x20000, 32767, 0, 0) == 32767);
    CHECK(SW(a, 0x10000, 32731, 0) == 32731 && SR(a, 0x20000, 32731, 0, 0) == 32731);
    CHECK((*find_owner(&first))->stream_head == 65530);
    CHECK(SW(a, 0x10000, 16, 0) == 16);
    fail_copy_address = 0x20006; /* The second copy of a wrapped read fails. */
    CHECK(SR(a, 0x20000, 16, 0, 0) == EIMBA);
    fail_copy_address = 0;
    CHECK(SR(a, 0x20000, 16, 0, 0x1000) == 16 && copied[0] == 1 && !memcmp(stream_input, stream_output, 16));
    CHECK(SW(a, 0x10000, 8, 0) == 8);
    run = &second; b = cuis_service(CUIS_SESSION_START, 0);
    CHECK(b >= 0 && SW(a, 0x10000, 8, 0) == 8);
    cuis_process_exit(&second); run = &first;
    CHECK(SR(a, 0x20000, 4, 0, 0x1000) == 4 && copied[0] == 1);
    CHECK(SR(a, 0x20000, 8, 0, 0x1000) == 8 && copied[0] == 1);
    CHECK(SR(a, 0x20000, 4, 0, 0x1000) == 4 && copied[0] == 2);
    b = cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 10, 10, 0);
    CHECK(b >= 0 && cuis_window_service(CUIS_WINDOW_SHOW, b, 0, 0, 0, 0) == E_OK);
    CHECK(cuis_window_service(CUIS_UPDATE_BEGIN, b, 0, 0, 0, 0) == E_OK);
    CHECK(SR(a, 0x20000, 1, 1, 0) == EACCDN);
    CHECK(SW(a, 0x10000, 32767, 0) == 32767 && SW(a, 0x10000, 32767, 0) == 32767 && SW(a, 0x10000, 2, 0) == 2);
    CHECK(SW(a, 0x10000, 1, 1) == EACCDN && !(*find_owner(&first))->pending_write);
    cuis_process_exit(&first);
    CHECK(allocations == frees && input_irqs && !input_bad_idle);
    return 0;
}
#undef SW
#undef SR
